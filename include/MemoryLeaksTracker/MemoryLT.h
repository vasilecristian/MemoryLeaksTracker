#ifndef __MEMORYLT_H_INCLUDED__
#define __MEMORYLT_H_INCLUDED__

#define MY_DEBUG
#if defined(MY_DEBUG)

#if defined(_WIN32) && !(defined(WINAPI_FAMILY) && ((WINAPI_FAMILY==WINAPI_FAMILY_APP) || (WINAPI_FAMILY==WINAPI_FAMILY_PHONE_APP)))
/** Enable the Stack trace tracking only for Win32|Debug platform (NOT WinPhone or WinStore)*/
#define TRACK_STACK_TRACE
#endif

#include <atomic>
#include <thread>
#include <mutex>

#if defined(_WIN32)
/// Is for Win32 and all windows platforms (including WinPhone & WinStore)
#include <new>
#include <exception>

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
#endif //_WIN32

namespace mlt
{
    typedef void*(*AllocFuncPtr)(unsigned int, const char*, int);
    typedef void(*FreeFuncPtr)(void*);

    extern AllocFuncPtr s_allocFuncPtr;
    extern FreeFuncPtr  s_FreeFuncPtr;

    static void* AllocInit(unsigned int size, const char* file, int line);
    static void* AllocInit2(unsigned int size, const char* file, int line);
    static void* AllocNormal(unsigned int size, const char* file, int line);
    static void* AllocShutdown(unsigned int size, const char* file, int line);
    static void FreeNormal(void* mem);
    static void FreeShutdown(void* mem);

    struct MemoryAllocationRecord;
    class LeakTracker
    {
        MemoryAllocationRecord* m_memoryAllocations;
        int m_memoryAllocationCount;
        std::mutex m_m;
        std::atomic<unsigned int> m_maxSize;
        std::atomic<unsigned int> m_maxLine;
      
    public:
        LeakTracker();
        ~LeakTracker();
        void* DebugAlloc(std::size_t size, const char* file, int line);
        void DebugFree(void* p);

        /** Prints all heap and reference leaks to stderr. */
        void PrintMemoryLeaks();

        #if defined(TRACK_STACK_TRACE)
        void PrintStackTrace(MemoryAllocationRecord* rec);

        /** Sets whether stack traces are tracked on memory allocations or not.
        * @param trackStackTrace Whether to track the stack trace on memory allocations.*/
        void SetTrackStackTrace(bool trackStackTrace);

        /** Toggles stack trace tracking on memory allocations.*/
        void ToggleTrackStackTrace();
        #endif //TRACK_STACK_TRACE

    };

	

	//class ILeakTracker
	//{
	//public:

	//	void* operator new(size_t size);
	//	void operator delete(void* p);

	//	void* operator new[](size_t size);
	//	void operator delete[](void* p);


	//	void* operator new(size_t size, const char *file, int line);
	//	void operator delete(void* p, const char *file, int line);

	//	void* operator new[](size_t size, const char *file, int line);
	//	void operator delete[](void* p, const char *file, int line);

	//protected:

	//	/** The constructor. */
 //       ILeakTracker(){}

	//	/** The destructor. */
 //       virtual ~ILeakTracker(){}
	//};

} //namespace mlt


#if defined(_WIN32)
    #define	NEW new(__FILE__, __LINE__)
#else
    #define NEW	new
#endif//_WIN32



#else //!MY_DEBUG

#include <iostream>

namespace mlt
{
	class LeakTracker
	{
	};
    sdfsdfasdf
    void PrintMemoryLeaks(){}
}

//just declare the NEW
#define NEW	new  

#endif //MY_DEBUG

#endif //__MEMORYLT_H_INCLUDED__
