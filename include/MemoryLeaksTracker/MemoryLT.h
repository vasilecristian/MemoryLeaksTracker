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


namespace mlt
{
	/** Initialize the MemoryLeakTracker*/
	void Init(bool heapCorruptionCheck = false, int buffer = 256);
	void Close();
	void CheckHeapCorruption();

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
	void Init(bool heapCorruptionCheck = false, int buffer = 256){}
	void Close() {}
	void CheckHeapCorruption() {}

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
