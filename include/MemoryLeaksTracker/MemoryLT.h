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
#ifndef MEMORYLT_H_INCLUDED
#define MEMORYLT_H_INCLUDED

/// Usage notes:
///  - Include this header LAST in every .cpp file. When the tracker is enabled it defines `new`
///    as a macro, which breaks any header included after it that uses placement new (most
///    standard library headers do).
///  - Only `new` expressions in files that include this header are recorded with their file and
///    line. Standard library allocations (containers, std::make_shared, ...), malloc(), and code
///    that does not include this header are not recorded.
///  - Because `new` is a macro, `new (buffer) T` and `new (std::nothrow) T` do not compile after
///    this header. Wrap such code like this:
///        #pragma push_macro("new")
///        #undef new
///        ...
///        #pragma pop_macro("new")
///  - The tracker is enabled when MLT_ENABLED is 1. When MLT_ENABLED is not defined, it is enabled
///    if _DEBUG or DEBUG is defined. Build the library and the code that uses it with the same value.

#include <cstddef>

#if !defined(MLT_ENABLED)
#if defined(_DEBUG) || defined(DEBUG)
#define MLT_ENABLED 1
#else
#define MLT_ENABLED 0
#endif
#endif //!MLT_ENABLED

#if MLT_ENABLED

#include <new>

/** Allocation forms used by the `new` macro below. They record the file and line of the allocation. */
void* operator new(std::size_t size, const char* file, int line);
void* operator new[](std::size_t size, const char* file, int line);

/** Called only when a constructor throws inside a `new(file, line)` expression. */
void operator delete(void* p, const char* file, int line) noexcept;
void operator delete[](void* p, const char* file, int line) noexcept;

namespace mlt
{
	/** Starts recording the allocations made through the `new` macro. Thread-safe.
	* Calling it again only changes the settings used for new allocations; recorded ones are kept.
	* @param heapCorruptionCheck when true, every recorded allocation gets a guard area on each side,
	*        checked on delete and by CheckHeapCorruption().
	* @param buffer size in bytes of each guard area. Rounded up to a multiple of 16, at least 16,
	*        at most 1 MiB. */
	void Init(bool heapCorruptionCheck = false, int buffer = 256);

	/** Prints the recorded allocations that are still alive and stops recording new ones.
	* Recorded memory can still be deleted safely after Close(). Called automatically at exit
	* if the program does not call it. Thread-safe. */
	void Close();

	/** Checks every recorded allocation that is still alive and reports damage around it. Thread-safe. */
	void CheckHeapCorruption();

	/** @return the number of recorded allocations that are still alive. */
	std::size_t GetActiveAllocationCount();

	/** @return the number of heap corruptions and invalid deletes detected since the program started. */
	std::size_t GetCorruptionCount();

} //namespace mlt

#define new new(__FILE__, __LINE__)

#else //!MLT_ENABLED

namespace mlt
{
	inline void Init(bool = false, int = 256) {}
	inline void Close() {}
	inline void CheckHeapCorruption() {}
	inline std::size_t GetActiveAllocationCount() { return 0; }
	inline std::size_t GetCorruptionCount() { return 0; }

	class LeakTracker
	{
	public:
		static void PrintMemoryLeaks() {}
	};
} //namespace mlt

#endif //MLT_ENABLED

#endif //MEMORYLT_H_INCLUDED
