#ifndef __MEMORYLT_H_INCLUDED__
#define __MEMORYLT_H_INCLUDED__

#if defined(_DEBUG) || defined(DEBUG)


#include <atomic>
#include <mutex>


#if defined(_WIN32)
/// Is for Win32 and all windows platforms (including WinPhone & WinStore)
#include <new>
#include <exception>
#endif //_WIN32

/**
* Global overrides of the new and delete operators for memory tracking.*/
#ifdef _MSC_VER
#pragma warning( disable : 4290 ) // C++ exception specification ignored.
#endif
void* operator new (std::size_t size, const char* file, int line);
void* operator new[](std::size_t size, const char* file, int line);
void* operator new (std::size_t size) throw(std::bad_alloc);
void* operator new[](std::size_t size) throw(std::bad_alloc);
void* operator new (std::size_t size, const std::nothrow_t&) throw();
void* operator new[](std::size_t size, const std::nothrow_t&) throw();
void operator delete (void* p) throw();
void operator delete[](void* p) throw();
void operator delete (void* p, const char* file, int line) throw();
void operator delete[](void* p, const char* file, int line) throw();
#ifdef _MSC_VER
#pragma warning( default : 4290 )
#endif

void* RecordAlloc(const char* file, int line);

namespace mlt
{
    typedef void*(*AllocFuncPtr)(std::size_t, const char*, unsigned int);
    typedef void(*FreeFuncPtr)(void*);

    extern AllocFuncPtr s_allocFuncPtr;
    extern FreeFuncPtr  s_freeFuncPtr;

	void Init();
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

	

	class BaseLeakTracker
	{
	public:
		void* operator new(std::size_t size);
		void operator delete(void* p);

		void* operator new[](std::size_t size);
		void operator delete[](void* p);

		void* operator new(std::size_t size, const char *file, int line);
		void operator delete(void* p, const char *file, int line);

		void* operator new[](std::size_t size, const char *file, int line);
		void operator delete[](void* p, const char *file, int line);
	};

} //namespace mlt

#define	new new(__FILE__, __LINE__)

#else //!_DEBUG

namespace mlt
{
	void Init(){}

    class BaseLeakTracker
    {
    };

    class LeakTracker
    {
    public:
		static void PrintMemoryLeaks(){}
    };
}

#endif //_DEBUG

#endif //__MEMORYLT_H_INCLUDED__
