
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
    typedef void* (*AllocFuncPtr)(std::size_t, const char*, unsigned int);
    typedef void(*FreeFuncPtr)(void*);

    /** Allocates memory for usual usage*/
	void* Alloc(std::size_t size, const char* file, unsigned int line);

    /** Free memory for usual usage*/
	void Free(void* mem);

    struct MemoryAllocationRecord
    {
        /**address returned to the caller after allocation*/
        void* m_address;

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

	class LeakTracker
	{
	public:
		LeakTracker();
		~LeakTracker();
		void* Alloc(std::size_t size, const char* file, unsigned int line);
		void Free(void* p);

		/** Prints all heap and reference leaks to stderr. */
		static void PrintMemoryLeaks();
        static void CheckHeapCorruption();

        void CheckHeapCorruptionAtAddress(void* address);

	private:
		MemoryAllocationRecord* m_memoryAllocations;
		int m_memoryAllocationCount;
		std::recursive_mutex m_m;
		std::size_t m_maxSize;
		std::size_t m_maxLine;
	};

    static std::mutex m_initMutex;
    static bool s_heapCorruptionEnabled = false;
    static int s_heapCorruptionBuferSize = 2048;
	static AllocFuncPtr s_allocFuncPtr = nullptr;
	static FreeFuncPtr  s_freeFuncPtr = nullptr;

    /** We reserve some memory to hold the mem for s_leakTracker */
    static char s_memleakTracker[sizeof(LeakTracker)] = { 0 };

    /** Is the static pointer for leakTracker */
    static LeakTracker* s_leakTracker = nullptr;


    void Init(bool heapCorruptionCheck, int buffer)
    {
        std::lock_guard<std::mutex> lk(m_initMutex);
        s_heapCorruptionEnabled = heapCorruptionCheck;
        s_heapCorruptionBuferSize = buffer;
        s_leakTracker = new(s_memleakTracker) LeakTracker;
        s_allocFuncPtr = Alloc;
		s_freeFuncPtr = Free;
    }

    void Close()
    {
        std::lock_guard<std::mutex> lk(m_initMutex);
        if (s_leakTracker)
        {
            s_leakTracker->PrintMemoryLeaks();
            s_allocFuncPtr = nullptr;
            s_freeFuncPtr = nullptr;
            //delete s_leakTracker;
            s_leakTracker = nullptr;
        }
    }

    void CheckHeapCorruption()
    {
        if (!s_leakTracker)
            return;

        s_leakTracker->CheckHeapCorruption();
    }

    void* Alloc(std::size_t size, const char* file, unsigned int line)
    {
        return s_leakTracker->Alloc(size, file, line);
    }

    void LeakTrackerExit()
    {
        s_allocFuncPtr = nullptr;
        s_freeFuncPtr = nullptr;

		if (s_leakTracker)
		{
			s_leakTracker->PrintMemoryLeaks();
            s_leakTracker = nullptr;
		}
    }

    void Free(void* mem)
    {
        s_leakTracker->Free(mem);
    }


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
    }

    void* LeakTracker::Alloc(std::size_t size, const char* file, unsigned int line)
    {
        if (file == nullptr)
        {
            return malloc(size);
        }

        unsigned char* mem = nullptr;
        MemoryAllocationRecord* rec = nullptr;
        void* payloadAddr = nullptr;

        if (s_heapCorruptionEnabled)
        {
            /// Allocate memory in this format:
            /// | heap corruption buffer | MemoryAllocationRecord | allocated memory of size | heap corruption buffe |
            /// ^                        ^                        ^
            /// mem                      rec                      payloadAddr
            mem = (unsigned char*)calloc(sizeof(MemoryAllocationRecord) + size + s_heapCorruptionBuferSize *2, 1);
            rec = (MemoryAllocationRecord*)(mem + s_heapCorruptionBuferSize);

            /// Move memory pointer past record
            payloadAddr = mem + s_heapCorruptionBuferSize + sizeof(MemoryAllocationRecord);
        }
        else
        {
            /// Allocate memory + size for a MemoryAlloctionRecord
            /// | MemoryAllocationRecord | allocated memory of size |
            /// ^                        ^
            /// mem = rec                payloadAddr
            mem = (unsigned char*)malloc(sizeof(MemoryAllocationRecord) + size);
            rec = (MemoryAllocationRecord*)mem;

            /// Move memory pointer past record
            payloadAddr = mem + sizeof(MemoryAllocationRecord);
        }

        

        m_m.lock();
        rec->m_address = payloadAddr;
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
        return payloadAddr;
    }

    void LeakTracker::Free(void* payloadAddr)
    {
        if (payloadAddr == 0)
            return;

        unsigned char* mem = nullptr;
        MemoryAllocationRecord* rec = nullptr;

        if (s_heapCorruptionEnabled)
        {
            /// Backup passed in pointer to access memory allocation record
            mem = ((unsigned char*)payloadAddr) - sizeof(MemoryAllocationRecord) - s_heapCorruptionBuferSize;

            rec = (MemoryAllocationRecord*)(((unsigned char*)payloadAddr) - sizeof(MemoryAllocationRecord));
        }
        else
        {
            /// Backup passed in pointer to access memory allocation record
            mem = ((unsigned char*)payloadAddr) - sizeof(MemoryAllocationRecord);

            rec = (MemoryAllocationRecord*)mem;
        }

        /// Sanity check: ensure that address in record matches passed in address
        if (rec->m_address != payloadAddr)
        {
            // This case could be a memory corruption, but most of the cases are memory allocations that was not tracked (because file was null).
            free(payloadAddr);
            return;
        }

        /// Sanity check: ensure that size is smaller than maximum (tracked)
        if (rec->m_size > m_maxSize)
        {
            std::cout << ("[memory] CORRUPTION: Attempting to free memory address with invalid memory allocation record (wrong size).\n");
            //std::cout << "S";
            return;
        }

        /// Sanity check: ensure that line is smaller than maximum (tracked)
        if (rec->m_line > m_maxLine)
        {
            std::cout << ("[memory] CORRUPTION: Attempting to free memory address with invalid memory allocation record (wrong line).\n");
            //std::cout << "L";
            return;
        }

        if (s_heapCorruptionEnabled)
        {
            CheckHeapCorruptionAtAddress(payloadAddr);
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
                    printf("[memory] LEAK: At address %#010x, size %zd, %s:%d.\n", (unsigned long)rec->m_address, rec->m_size, rec->m_file, rec->m_line);
                }
                rec = rec->m_next;
            }
        }

		s_leakTracker->m_m.unlock();
    }

    void LeakTracker::CheckHeapCorruptionAtAddress(void* address)
    {
        /// Backup passed in pointer to access memory allocation record
        unsigned char* mem = ((unsigned char*)address) - sizeof(MemoryAllocationRecord) - s_heapCorruptionBuferSize;

        MemoryAllocationRecord* rec = (MemoryAllocationRecord*)(((unsigned char*)address) - sizeof(MemoryAllocationRecord));

        unsigned char* addrStart = mem;
        unsigned char* addrEnd = addrStart + s_heapCorruptionBuferSize;
        for (; addrStart < addrEnd; addrStart++)
        {
            if (*addrStart != 0)
            {
                printf("[memory] CORRUPTION: before address %#010x, size %zd, %s:%d.\n", (unsigned long)rec->m_address, rec->m_size, rec->m_file, rec->m_line);
                break;
            }
        }

        addrStart = (unsigned char*)address + rec->m_size;
        addrEnd = addrStart + s_heapCorruptionBuferSize;
        for (; addrStart < addrEnd; addrStart++)
        {
            if (*addrStart != 0)
            {
                printf("[memory] CORRUPTION: after address %#010x, size %zd, %s:%d.\n", (unsigned long)rec->m_address, rec->m_size, rec->m_file, rec->m_line);
            }
        }
    }

    void LeakTracker::CheckHeapCorruption()
    {
        if (!s_heapCorruptionEnabled)
            return;

        MemoryAllocationRecord* rec = s_leakTracker->m_memoryAllocations;
                      
        while (rec)
        {
            s_leakTracker->CheckHeapCorruptionAtAddress(rec->m_address);

            rec = rec->m_next;
        }
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
	return operator new (size, nullptr, 0);
}

void* operator new[](std::size_t size) throw(std::bad_alloc)
{
	return operator new (size, nullptr, 0);
}

void* operator new (std::size_t size, const std::nothrow_t&) throw()
{
	return operator new (size, nullptr, 0);
}

void* operator new[](std::size_t size, const std::nothrow_t&) throw()
{
	return operator new (size, nullptr, 0);
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
			return mlt::s_allocFuncPtr(size, nullptr, 0);
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
			return mlt::s_allocFuncPtr(size, nullptr, 0);
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
