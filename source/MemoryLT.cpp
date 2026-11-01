//--------------------------------------------------------------------------------
// MemoryLT.cpp
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
#include "MemoryLeaksTracker/MemoryLT.h"

#if MLT_ENABLED

/// Override (only for this file) the _HAS_EXCEPTIONS values. Otherwise a compile error is
/// generated (aka: error C3861: '__uncaught_exception': identifier not found)
#ifdef _HAS_EXCEPTIONS
#undef _HAS_EXCEPTIONS
#endif //_HAS_EXCEPTIONS
#define _HAS_EXCEPTIONS 2

/// The `new` macro from MemoryLT.h must not apply to this file.
#if defined new
#undef new
#endif

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <mutex>
#include <new>

/// Every block allocated by the operators in this file starts with a header, so delete never has
/// to guess where a block starts. There are two layouts:
///
/// Plain block: allocations without file/line (standard library, ...) or made while not recording.
///   | padding | BlockPrefix | payload |
///   ^ malloc()              ^ returned to the caller
///
/// Tracked block: allocations made through the `new` macro while recording.
///   | AllocationRecord | front guard (G + padding) | BlockPrefix | payload | back guard (G) |
///   ^ malloc()                                                  ^ returned to the caller
///
/// G is the guard size (0 when the heap corruption check is off). Each block stores its own G, so
/// changing the settings never changes how existing blocks are freed.
/// BlockPrefix sits right before the payload, so a write even 1 byte before the block damages it
/// and is detected. AllocationRecord sits behind the front guard, out of reach of small underflows.

namespace
{
	/** Payload alignment. All header and guard sizes are multiples of it, so the payload keeps the
	* alignment that malloc() gives (up to 16 bytes), the same as the default operator new. */
	constexpr std::size_t k_Alignment = 16;

	/** Largest guard size accepted by mlt::Init(). */
	constexpr std::size_t k_MaxGuardSize = 1024 * 1024;

	/** Byte stored in the guard areas. Not zero, so a stray '\0' written past the end is caught too. */
	constexpr unsigned char k_GuardFill = 0xFD;

	/** Block kinds. Stored in BlockPrefix::m_Check, XOR-ed with the guard size. */
	constexpr std::uint32_t k_PlainBlockMagic = 0x4D4C5450;   // "MLTP"
	constexpr std::uint32_t k_TrackedBlockMagic = 0x4D4C5454; // "MLTT"

	/** Written into BlockPrefix::m_Check on delete, so a second delete of the block is rejected. */
	constexpr std::uint32_t k_DeletedBlockMagic = 0x4D4C5444; // "MLTD"

	/** Stored right before every payload. */
	struct BlockPrefix
	{
		std::uint32_t m_GuardSize; ///< bytes in each guard area; 0 when the block has no guards
		std::uint32_t m_Check;     ///< block magic XOR m_GuardSize, so damage to either field is detected
	};

	/** Bookkeeping for a tracked block. Members are in ascending size. m_Address is last, so it is
	* the first member that a write before the block would reach. */
	struct AllocationRecord
	{
		unsigned int m_Line;       ///< source line of the allocation
		std::uint32_t m_GuardSize; ///< bytes in each guard area
		std::size_t m_Size;        ///< bytes requested by the caller
		const char* m_File;        ///< source file of the allocation
		AllocationRecord* m_Next;  ///< next record in the tracker list
		AllocationRecord* m_Prev;  ///< previous record in the tracker list
		void* m_Address;           ///< payload address returned to the caller
	};

	constexpr std::size_t RoundUp(std::size_t value, std::size_t multiple)
	{
		return (value + multiple - 1) / multiple * multiple;
	}

	constexpr std::size_t k_PrefixAreaSize = RoundUp(sizeof(BlockPrefix), k_Alignment);
	constexpr std::size_t k_RecordAreaSize = RoundUp(sizeof(AllocationRecord), k_Alignment);

	/** Bytes of the prefix area in front of the BlockPrefix. In tracked blocks with guards they are
	* filled and checked as part of the front guard. */
	constexpr std::size_t k_PrefixPadding = k_PrefixAreaSize - sizeof(BlockPrefix);

	/** The tracked blocks that are still alive. */
	class Tracker
	{
	public:
		void Add(AllocationRecord* record);
		void Remove(AllocationRecord* record);

		/** Removes the record of the tracked block at `address`.
		* @return the record, or nullptr when no tracked block has that address. */
		AllocationRecord* RemoveByAddress(const void* address);

		std::size_t GetActiveCount();
		void PrintLeaks();
		void CheckAllBlocks();

	private:
		void RemoveLocked(AllocationRecord* record);

		AllocationRecord* m_Head = nullptr;
		std::size_t m_ActiveCount = 0;
		std::mutex m_Mutex; ///< guards m_Head, m_ActiveCount, and the m_Next/m_Prev links
	};

	/// Shared settings. Atomic, because every operator new/delete reads them while Init()/Close()
	/// may change them on another thread. Constant-initialized, so they are valid before main().
	std::atomic<bool> g_IsRecording(false);
	std::atomic<std::uint32_t> g_GuardSize(0);
	std::atomic<std::size_t> g_CorruptionCount(0);
	std::atomic<bool> g_IsExitReportRegistered(false);

	/** @return the tracker. It is built on first use and never destroyed, so blocks can still be
	* deleted while static objects are destroyed at exit. */
	Tracker& GetTracker()
	{
		alignas(Tracker) static unsigned char s_Storage[sizeof(Tracker)];
		static Tracker* s_Tracker = ::new (s_Storage) Tracker();
		return *s_Tracker;
	}

	BlockPrefix* GetPrefix(void* payload)
	{
		return reinterpret_cast<BlockPrefix*>(static_cast<unsigned char*>(payload) - sizeof(BlockPrefix));
	}

	bool IsFilledWithGuard(const unsigned char* begin, std::size_t size)
	{
		for (std::size_t i = 0; i < size; ++i)
		{
			if (begin[i] != k_GuardFill)
			{
				return false;
			}
		}
		return true;
	}

	/** Counts and prints one damaged area next to a tracked block. */
	void ReportCorruption(const char* where, const AllocationRecord& record)
	{
		++g_CorruptionCount;
		std::fprintf(stderr, "[memory] CORRUPTION: %s address %p, size %zu, %s:%u.\n",
			where, record.m_Address, record.m_Size, record.m_File, record.m_Line);
	}

	/** Reports damage around a tracked block: in its BlockPrefix and in its guard areas. */
	void CheckBlock(const AllocationRecord& record)
	{
		const BlockPrefix* prefix = GetPrefix(record.m_Address);
		if (prefix->m_GuardSize != record.m_GuardSize || (prefix->m_Check ^ prefix->m_GuardSize) != k_TrackedBlockMagic)
		{
			ReportCorruption("header before", record);
		}

		if (record.m_GuardSize == 0)
		{
			return;
		}

		const unsigned char* frontGuard = reinterpret_cast<const unsigned char*>(&record) + k_RecordAreaSize;
		if (!IsFilledWithGuard(frontGuard, record.m_GuardSize + k_PrefixPadding))
		{
			ReportCorruption("before", record);
		}

		const unsigned char* backGuard = static_cast<const unsigned char*>(record.m_Address) + record.m_Size;
		if (!IsFilledWithGuard(backGuard, record.m_GuardSize))
		{
			ReportCorruption("after", record);
		}
	}

	void Tracker::Add(AllocationRecord* record)
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		record->m_Prev = nullptr;
		record->m_Next = m_Head;
		if (m_Head != nullptr)
		{
			m_Head->m_Prev = record;
		}
		m_Head = record;
		++m_ActiveCount;
	}

	void Tracker::Remove(AllocationRecord* record)
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		RemoveLocked(record);
	}

	AllocationRecord* Tracker::RemoveByAddress(const void* address)
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		AllocationRecord* record = m_Head;
		while (record != nullptr && record->m_Address != address)
		{
			record = record->m_Next;
		}
		if (record != nullptr)
		{
			RemoveLocked(record);
		}
		return record;
	}

	void Tracker::RemoveLocked(AllocationRecord* record)
	{
		if (m_Head == record)
		{
			m_Head = record->m_Next;
		}
		if (record->m_Prev != nullptr)
		{
			record->m_Prev->m_Next = record->m_Next;
		}
		if (record->m_Next != nullptr)
		{
			record->m_Next->m_Prev = record->m_Prev;
		}
		--m_ActiveCount;
	}

	std::size_t Tracker::GetActiveCount()
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		return m_ActiveCount;
	}

	void Tracker::PrintLeaks()
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		if (m_ActiveCount == 0)
		{
			std::fprintf(stderr, "\n[memory] All HEAP allocations successfully cleaned up (no leaks detected).\n");
			return;
		}

		std::fprintf(stderr, "\n[memory] WARNING: %zu HEAP allocations still active in memory.\n", m_ActiveCount);
		for (const AllocationRecord* record = m_Head; record != nullptr; record = record->m_Next)
		{
			std::fprintf(stderr, "[memory] LEAK: At address %p, size %zu, %s:%u.\n",
				record->m_Address, record->m_Size, record->m_File, record->m_Line);
		}
	}

	void Tracker::CheckAllBlocks()
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		for (const AllocationRecord* record = m_Head; record != nullptr; record = record->m_Next)
		{
			CheckBlock(*record);
		}
	}

	/** Allocates a block in one of the layouts described at the top of this file.
	* @return the payload, or nullptr when out of memory or when the size is too large. */
	void* AllocateBlock(std::size_t size, const char* file, unsigned int line)
	{
		const bool isTracked = file != nullptr && g_IsRecording.load();
		const std::uint32_t guardSize = isTracked ? g_GuardSize.load() : 0;
		const std::size_t headerSize = isTracked ? k_RecordAreaSize + guardSize + k_PrefixAreaSize : k_PrefixAreaSize;
		const std::size_t overhead = headerSize + guardSize;
		if (size > std::numeric_limits<std::size_t>::max() - overhead)
		{
			return nullptr;
		}

		unsigned char* base = static_cast<unsigned char*>(std::malloc(overhead + size));
		if (base == nullptr)
		{
			return nullptr;
		}

		unsigned char* payload = base + headerSize;
		const std::uint32_t magic = isTracked ? k_TrackedBlockMagic : k_PlainBlockMagic;
		::new (GetPrefix(payload)) BlockPrefix{ guardSize, magic ^ guardSize };
		if (!isTracked)
		{
			return payload;
		}

		AllocationRecord* record = ::new (base) AllocationRecord();
		record->m_Line = line;
		record->m_GuardSize = guardSize;
		record->m_Size = size;
		record->m_File = file;
		record->m_Address = payload;
		if (guardSize > 0)
		{
			std::memset(base + k_RecordAreaSize, k_GuardFill, guardSize + k_PrefixPadding);
			std::memset(payload + size, k_GuardFill, guardSize);
		}

		GetTracker().Add(record);
		return payload;
	}

	/** Handles a delete whose block header is damaged. A tracked block is still found by its address
	* and freed. Anything else (a write before an untracked block, a double delete, or memory that did
	* not come from these operators) is reported and leaked on purpose, because freeing it could crash. */
	void FreeDamagedBlock(void* payload)
	{
		AllocationRecord* record = GetTracker().RemoveByAddress(payload);
		if (record == nullptr)
		{
			++g_CorruptionCount;
			std::fprintf(stderr, "[memory] CORRUPTION: delete of address %p with a damaged header "
				"(write before the block, double delete, or memory not allocated with new). The block is leaked.\n", payload);
			return;
		}

		CheckBlock(*record);
		std::free(record);
	}

	void FreeBlock(void* payload)
	{
		if (payload == nullptr)
		{
			return;
		}

		BlockPrefix* prefix = GetPrefix(payload);
		const std::uint32_t guardSize = prefix->m_GuardSize;
		const std::uint32_t magic = prefix->m_Check ^ guardSize;
		if (magic == k_PlainBlockMagic && guardSize == 0)
		{
			prefix->m_Check = k_DeletedBlockMagic;
			std::free(static_cast<unsigned char*>(payload) - k_PrefixAreaSize);
			return;
		}

		if (magic == k_TrackedBlockMagic && guardSize <= k_MaxGuardSize)
		{
			unsigned char* base = static_cast<unsigned char*>(payload) - k_PrefixAreaSize - guardSize - k_RecordAreaSize;
			AllocationRecord* record = reinterpret_cast<AllocationRecord*>(base);
			if (record->m_Address == payload)
			{
				CheckBlock(*record);
				GetTracker().Remove(record);
				prefix->m_Check = k_DeletedBlockMagic;
				std::free(base);
				return;
			}
		}

		FreeDamagedBlock(payload);
	}

	[[noreturn]] void ThrowBadAlloc()
	{
#if defined(__cpp_exceptions) || defined(__EXCEPTIONS) || defined(_CPPUNWIND)
		throw std::bad_alloc();
#else
		std::abort();
#endif
	}

	/** Allocates like the standard operator new: on failure it calls the new-handler and retries,
	* and throws std::bad_alloc when no new-handler is installed. */
	void* AllocateOrThrow(std::size_t size, const char* file, unsigned int line)
	{
		for (;;)
		{
			void* payload = AllocateBlock(size, file, line);
			if (payload != nullptr)
			{
				return payload;
			}

			std::new_handler handler = std::get_new_handler();
			if (handler == nullptr)
			{
				ThrowBadAlloc();
			}
			handler();
		}
	}
} //namespace


namespace mlt
{
	void Init(bool heapCorruptionCheck, int buffer)
	{
		std::size_t guardSize = 0;
		if (heapCorruptionCheck)
		{
			const std::size_t requested = buffer > 0 ? static_cast<std::size_t>(buffer) : k_Alignment;
			guardSize = RoundUp(requested < k_MaxGuardSize ? requested : k_MaxGuardSize, k_Alignment);
		}
		g_GuardSize = static_cast<std::uint32_t>(guardSize);

		if (!g_IsExitReportRegistered.exchange(true))
		{
			std::atexit(Close);
		}
		g_IsRecording = true;
	}

	void Close()
	{
		if (g_IsRecording.exchange(false))
		{
			GetTracker().PrintLeaks();
		}
	}

	void CheckHeapCorruption()
	{
		GetTracker().CheckAllBlocks();
	}

	std::size_t GetActiveAllocationCount()
	{
		return GetTracker().GetActiveCount();
	}

	std::size_t GetCorruptionCount()
	{
		return g_CorruptionCount.load();
	}
} //namespace mlt


/// Replacements of the global allocation functions. All of them use the block layouts above, so
/// every pointer that reaches operator delete has a header.

void* operator new(std::size_t size, const char* file, int line)
{
	return AllocateOrThrow(size, file, static_cast<unsigned int>(line));
}

void* operator new[](std::size_t size, const char* file, int line)
{
	return AllocateOrThrow(size, file, static_cast<unsigned int>(line));
}

void* operator new(std::size_t size)
{
	return AllocateOrThrow(size, nullptr, 0);
}

void* operator new[](std::size_t size)
{
	return AllocateOrThrow(size, nullptr, 0);
}

void* operator new(std::size_t size, const std::nothrow_t&) noexcept
{
	return AllocateBlock(size, nullptr, 0);
}

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept
{
	return AllocateBlock(size, nullptr, 0);
}

void operator delete(void* p) noexcept
{
	FreeBlock(p);
}

void operator delete[](void* p) noexcept
{
	FreeBlock(p);
}

void operator delete(void* p, const std::nothrow_t&) noexcept
{
	FreeBlock(p);
}

void operator delete[](void* p, const std::nothrow_t&) noexcept
{
	FreeBlock(p);
}

/// Sized forms. Code built as C++14 or later calls them on delete; same symbols as in C++11.
void operator delete(void* p, std::size_t) noexcept
{
	FreeBlock(p);
}

void operator delete[](void* p, std::size_t) noexcept
{
	FreeBlock(p);
}

void operator delete(void* p, const char*, int) noexcept
{
	FreeBlock(p);
}

void operator delete[](void* p, const char*, int) noexcept
{
	FreeBlock(p);
}

#endif //MLT_ENABLED
