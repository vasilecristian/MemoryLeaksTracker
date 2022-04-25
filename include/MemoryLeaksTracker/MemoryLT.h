//--------------------------------------------------------------------------------
// MemoryLT.h
//
// Copyright (c) 2022, Cristian Vasile
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//  * Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
//  * Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
// ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL CRISTIAN VASILE BE LIABLE FOR ANY
// DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
// (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
// LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
// ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// Author: Cristian Vasile
//--------------------------------------------------------------------------------
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
