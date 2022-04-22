
#include "MemoryLeaksTracker/MemoryLT.h"

#if defined(_DEBUG) || defined(DEBUG)

/// Override (only for this file) the _HAS_EXCEPTIONS values. Otherwise a compile error is 
/// generated (aka: error C3861: '__uncaught_exception': identifier not found)
#ifdef _HAS_EXCEPTIONS
#undef _HAS_EXCEPTIONS
#endif //_HAS_EXCEPTIONS
#define _HAS_EXCEPTIONS 2

#if defined new
#undef new
#endif

#include <cstdio>
#include <cstdarg>
#include <thread>
#include <iostream>
#include <array>


namespace mlt
{
	void* Alloc(std::size_t size, const char* file, unsigned int line);
	void* AllocShutdown(std::size_t size, const char* file, unsigned int line);
	void Free(void* mem);
	void FreeShutdown(void* mem);

	struct MemoryAllocationRecord;
	class LeakTracker
	{
	public:
		LeakTracker();
		~LeakTracker();
		void* Alloc(std::size_t size, const char* file, unsigned int line);
		void Free(void* p);

		/** Prints all heap and reference leaks to stderr. */
		static void PrintMemoryLeaks();

	private:
		MemoryAllocationRecord* m_memoryAllocations;
		int m_memoryAllocationCount;
		std::recursive_mutex m_m;
		std::size_t m_maxSize;
		std::size_t m_maxLine;
	};

	static AllocFuncPtr s_allocFuncPtr = nullptr;
	static FreeFuncPtr  s_freeFuncPtr = nullptr;

    /** We reserve some memory to hold the mem for s_leakTracker */
    static char s_memleakTracker[sizeof(LeakTracker)] = { 0 };

    /** Is the static pointer for leakTracker */
    static LeakTracker* s_leakTracker = nullptr;

    void Init()
    {
        s_leakTracker = new(s_memleakTracker) LeakTracker;
        s_allocFuncPtr = Alloc;
		s_freeFuncPtr = Free;
    }

    void* Alloc(std::size_t size, const char* file, unsigned int line)
    {
        return s_leakTracker->Alloc(size, file, line);
    }

    /** Allocates memory during shutdown of the memory manager*/
    void* AllocShutdown(std::size_t size, const char* file, unsigned int line)
    {
        return malloc(size);
    }

    void LeakTrackerExit()
    {
		if (s_leakTracker)
		{
			s_leakTracker->PrintMemoryLeaks();

			s_leakTracker->~LeakTracker();

			s_leakTracker = nullptr;
		}
    }

    void FreeShutdown(void* mem)
    {
        free(mem);
    }

    void Free(void* mem)
    {
        s_leakTracker->Free(mem);
    }


    struct MemoryAllocationRecord
    {
        /**address returned to the caller after allocation*/
        unsigned long m_address;         

        /**size of the allocation request*/
		std::size_t m_size;

        /**source file of allocation request*/
        const char* m_file;               

        /**source line of the allocation request*/
        unsigned int m_line;                       

        /**linked list next node*/
        MemoryAllocationRecord* m_next;
        /**linked list prev node*/
        MemoryAllocationRecord* m_prev;
    };


    LeakTracker::LeakTracker() 
        : m_memoryAllocations(0)
        , m_memoryAllocationCount(0)
        , m_maxSize(0)
        , m_maxLine(0)
    {
        atexit(LeakTrackerExit);

        // construct your heap here
    }

    LeakTracker::~LeakTracker()
    {
        s_allocFuncPtr = AllocShutdown;

        // destruct your heap here

        s_freeFuncPtr = FreeShutdown;
    }

    void* LeakTracker::Alloc(std::size_t size, const char* file, unsigned int line)
    {
        /// Allocate memory + size for a MemoryAlloctionRecord
        unsigned char* mem = (unsigned char*)malloc(size + sizeof(MemoryAllocationRecord));

        /// Cast
        MemoryAllocationRecord* rec = (MemoryAllocationRecord*)mem;

        /// Move memory pointer past record
        mem += sizeof(MemoryAllocationRecord);

        m_m.lock();
        rec->m_address = (unsigned long)mem;
        rec->m_size = (unsigned int)size;
        rec->m_file = file;
        rec->m_line = line;
        rec->m_next = m_memoryAllocations;
        rec->m_prev = 0;

        if (m_maxSize < size)
            m_maxSize = size;
        if (m_maxLine < line)
            m_maxLine = line;

        if (m_memoryAllocations)
            m_memoryAllocations->m_prev = rec;
        m_memoryAllocations = rec;
        ++m_memoryAllocationCount;

        m_m.unlock();
        return mem;
    }

    void LeakTracker::Free(void* p)
    {
        if (p == 0)
            return;

        /// Backup passed in pointer to access memory allocation record
        void* mem = ((unsigned char*)p) - sizeof(MemoryAllocationRecord);

        MemoryAllocationRecord* rec = (MemoryAllocationRecord*)mem;

        /// Sanity check: ensure that address in record matches passed in address
        if (rec->m_address != (unsigned long)p)
        {
            //std::cout << ("[memory] CORRUPTION: Attempting to free memory address with invalid memory allocation record (wrong address).\n");
            //std::cout << "A";
            return;
        }

        /// Sanity check: ensure that size is smaller than maximum (tracked)
        if (rec->m_size > m_maxSize)
        {
            //std::cout << ("[memory] CORRUPTION: Attempting to free memory address with invalid memory allocation record (wrong size).\n");
            //std::cout << "S";
            return;
        }

        /// Sanity check: ensure that line is smaller than maximum (tracked)
        if (rec->m_line > m_maxLine)
        {
            //std::cout << ("[memory] CORRUPTION: Attempting to free memory address with invalid memory allocation record (wrong line).\n");
            //std::cout << "L";
            return;
        }

        /// Link this item out
        m_m.lock();
        if (m_memoryAllocations == rec)
            m_memoryAllocations = rec->m_next;
        if (rec->m_prev)
            rec->m_prev->m_next = rec->m_next;
        if (rec->m_next)
            rec->m_next->m_prev = rec->m_prev;
        --m_memoryAllocationCount;
        m_m.unlock();

        /// Free the address from the original alloc location (before mem allocation record)
        free(mem);
    }

    void LeakTracker::PrintMemoryLeaks()
    {
        printf("\n");

		s_leakTracker->m_m.lock();
        /// Dump general heap memory leaks
		if (s_leakTracker->m_memoryAllocationCount == 0)
        {
            printf("[memory] All HEAP allocations successfully cleaned up (no leaks detected).\n");
        }
        else
        {
            printf("[memory] WARNING: %d  HEAP allocations still active in memory.\n", s_leakTracker->m_memoryAllocationCount);

			MemoryAllocationRecord* rec = s_leakTracker->m_memoryAllocations;
            while (rec)
            {
                if (strlen(rec->m_file) > 0)
                {
                    printf("[memory] LEAK: At address %d, size %d, %s:%d.\n", rec->m_address, rec->m_size, rec->m_file, rec->m_line);
                }
                rec = rec->m_next;
            }
        }

		s_leakTracker->m_m.unlock();
    }
} //mlt



#ifdef _MSC_VER
#pragma warning( disable : 4290 )
#endif

void* operator new (std::size_t size, const char* file, int line)
{
	if(mlt::s_allocFuncPtr)
		return mlt::s_allocFuncPtr(size, file, line);
	else
		return malloc(size);
}

void* operator new[](std::size_t size, const char* file, int line)
{
	return operator new (size, file, line);
}

void* operator new (std::size_t size) throw(std::bad_alloc)
{
	return operator new (size, "", 0);
}

void* operator new[](std::size_t size) throw(std::bad_alloc)
{
	return operator new (size, "", 0);
}

void* operator new (std::size_t size, const std::nothrow_t&) throw()
{
	return operator new (size, "", 0);
}

void* operator new[](std::size_t size, const std::nothrow_t&) throw()
{
	return operator new (size, "", 0);
}

void operator delete (void* p) throw()
{
	if(mlt::s_freeFuncPtr)
		mlt::s_freeFuncPtr(p);
	else
		free(p);
}

void operator delete[](void* p) throw()
{
	if(mlt::s_freeFuncPtr)
		mlt::s_freeFuncPtr(p);
	else
		free(p);
}

void operator delete (void* p, const char* file, int line) throw()
{
	if(mlt::s_freeFuncPtr)
		mlt::s_freeFuncPtr(p);
	else
		free(p);
}

void operator delete[](void* p, const char* file, int line) throw()
{
	if(mlt::s_freeFuncPtr)
		mlt::s_freeFuncPtr(p);
	else
		free(p);
}

#ifdef _MSC_VER
#pragma warning( default : 4290 )
#endif


namespace mlt
{
    void* BaseLeakTracker::operator new(size_t size)
	{
		if(mlt::s_allocFuncPtr)
			return mlt::s_allocFuncPtr(size, "", 0);
		else
			return malloc(size);
	}

    void BaseLeakTracker::operator delete(void* p)
	{
		if(mlt::s_freeFuncPtr)
			mlt::s_freeFuncPtr(p);
		else
			free(p);
    }

    void* BaseLeakTracker::operator new[](size_t size)
	{
		if(mlt::s_allocFuncPtr)
			return mlt::s_allocFuncPtr(size, "", 0);
		else
			return malloc(size);
    }

    void BaseLeakTracker::operator delete[](void* p)
	{
		if(mlt::s_freeFuncPtr)
			mlt::s_freeFuncPtr(p);
		else
			free(p);
	};

    void* BaseLeakTracker::operator new(size_t size, const char *file, int line)
	{
		if(mlt::s_allocFuncPtr)
			return mlt::s_allocFuncPtr(size, file, line);
		else
			return malloc(size);
	};

    void BaseLeakTracker::operator delete(void* p, const char *file, int line)
	{
		if(mlt::s_freeFuncPtr)
			mlt::s_freeFuncPtr(p);
		else
			free(p);
	};

    void* BaseLeakTracker::operator new[](size_t size, const char *file, int line)
	{
		if(mlt::s_allocFuncPtr)
			return mlt::s_allocFuncPtr(size, file, line);
		else
			return malloc(size);
	}

    void BaseLeakTracker::operator delete[](void* p, const char *file, int line)
	{
		if(mlt::s_freeFuncPtr)
			mlt::s_freeFuncPtr(p);
		else
			free(p);
	};
}
#endif //_DEBUG
