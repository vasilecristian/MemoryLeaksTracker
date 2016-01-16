#ifndef __MEMORYLT_H_INCLUDED__
#define __MEMORYLT_H_INCLUDED__

#if defined(_DEBUG)

#if defined(_WIN32) && !(defined(WINAPI_FAMILY) && ((WINAPI_FAMILY==WINAPI_FAMILY_APP) || (WINAPI_FAMILY==WINAPI_FAMILY_PHONE_APP)))
/** Enable the Stack trace tracking only for Win32|Debug platform (NOT WinPhone or WinStore)*/
#define TRACK_STACK_TRACE
#endif


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
	/** Prints all heap and reference leaks to stderr. */
	extern void PrintMemoryLeaks();

	#if defined(TRACK_STACK_TRACE)
	/** Sets whether stack traces are tracked on memory allocations or not.
	* @param trackStackTrace Whether to track the stack trace on memory allocations.*/
	void SetTrackStackTrace(bool trackStackTrace);

	/** Toggles stack trace tracking on memory allocations.*/
	void ToggleTrackStackTrace();
	#endif //TRACK_STACK_TRACE


	class LeakTracker
	{
	public:

		void* operator new(size_t size);
		void operator delete(void* p);

		void* operator new[](size_t size);
		void operator delete[](void* p);


		void* operator new(size_t size, const char *file, int line);
		void operator delete(void* p, const char *file, int line);

		void* operator new[](size_t size, const char *file, int line);
		void operator delete[](void* p, const char *file, int line);

	protected:

		/** The constructor. */
		LeakTracker(){}

		/** The destructor. */
		virtual ~LeakTracker(){}
	};


    /**
    * CustomAlloc - alloc memory.
    *
    * @param size is a std::size_t representing the amount of memory to be allocated .
    * @param filename is pointer to char (optional), representing the name of file.
    * @param line is an integer (optional).
    * @return  a void pointer to the allocated memory.*/
	void* CustomAlloc( std::size_t size, const char* filename = 0, int line = 0 );


    /**
    * CustomAllocAlign - alloc memory with given alignment.
    *
    * @param size is a std::size_t representing the amount of memory to be allocated.
    * @param align is a std::size_t representing the alignment.
    * @param filename is pointer to char (optional).
    * @param line is an integer (optional).
    * @return a void pointer to the allocated memory.*/
	void* CustomAllocAlign( std::size_t size, std::size_t align, const char* filename = 0, int line = 0 );


    /**
    * CustomFree - free the memory.
    *
    * @param ptr is a void pointer that is the memory to be free.*/
	void  CustomFree( void* ptr );

} //namespace mlt


#if defined(_WIN32)
    #define	NEW new(__FILE__, __LINE__)
    #define PRINTMEMORYLEAKS	atexit(mlt::PrintMemoryLeaks)
#else
    #define NEW	new
	#define PRINTMEMORYLEAKS
#endif//_WIN32



#else //!_DEBUG

#include <iostream>

namespace mlt
{
	class LeakTracker
	{
	};

    void* CustomAlloc(std::size_t size, const char* filename = 0, int line = 0)
    {
        return malloc(size);
    }

    void* CustomAllocAlign(std::size_t size, std::size_t align, const char* filename = 0, int line = 0)
    {
        return malloc(size);
    }

    void CustomFree(void* ptr)
    {
        free(ptr);
    }
}

//just declare the NEW
#define NEW	new  
#define PRINTMEMORYLEAKS

#endif //_DEBUG

#endif //__MEMORYLT_H_INCLUDED__
