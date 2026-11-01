// MemoryLTTests.cpp : unit tests for MemoryLeaksTracker.
//
// No test framework: each check that fails is printed, and main() returns non-zero.
// The tests share one process, so they compare counters before and after, never absolute values.
// Run them under AddressSanitizer and ThreadSanitizer too (see the asan/tsan presets).

#include <atomic>
#include <cstddef>
#include <cstdio>
#include <limits>
#include <memory>
#include <new>
#include <thread>
#include <vector>

// Last include: when enabled, it defines `new` as a macro.
#include "MemoryLeaksTracker/MemoryLT.h"

#if MLT_ENABLED

#define MLT_CHECK(condition) Check((condition), #condition, __FILE__, __LINE__)

namespace
{
	int g_FailureCount = 0;

	/** Made before main(), so before mlt::Init(). It must still be deleted safely later. */
	int* g_AllocatedBeforeInit = new int(7);

	void Check(bool condition, const char* expression, const char* file, int line)
	{
		if (!condition)
		{
			++g_FailureCount;
			std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", file, line, expression);
		}
	}

	/** Returns its argument through a volatile, so the compiler cannot see out-of-bounds offsets. */
	std::size_t Opaque(std::size_t value)
	{
		volatile std::size_t copy = value;
		return copy;
	}

	void TestTrackedAllocationIsCounted()
	{
		mlt::Init();
		const std::size_t before = mlt::GetActiveAllocationCount();

		int* value = new int(1);
		int* values = new int[10];
		MLT_CHECK(mlt::GetActiveAllocationCount() == before + 2);

		delete value;
		delete[] values;
		MLT_CHECK(mlt::GetActiveAllocationCount() == before);
	}

	/** Bug: delete read 48 bytes before blocks the tracker did not record (heap-buffer-overflow). */
	void TestUntrackedAllocationsAreFreedSafely()
	{
		mlt::Init(true);
		const std::size_t before = mlt::GetActiveAllocationCount();
		const std::size_t corruptions = mlt::GetCorruptionCount();
		{
			std::vector<int> numbers(1000, 1);
			std::shared_ptr<int> shared = std::make_shared<int>(5);
			MLT_CHECK(mlt::GetActiveAllocationCount() == before);
		}
		MLT_CHECK(mlt::GetCorruptionCount() == corruptions);
	}

	void TestAllocationBeforeInitIsFreedSafely()
	{
		mlt::Init();
		const std::size_t before = mlt::GetActiveAllocationCount();
		const std::size_t corruptions = mlt::GetCorruptionCount();

		delete g_AllocatedBeforeInit;
		g_AllocatedBeforeInit = nullptr;

		MLT_CHECK(mlt::GetActiveAllocationCount() == before);
		MLT_CHECK(mlt::GetCorruptionCount() == corruptions);
	}

	/** Bug: delete after Close() called free() on an address inside the block. */
	void TestDeleteAfterCloseIsSafe()
	{
		mlt::Init(true);
		const std::size_t before = mlt::GetActiveAllocationCount();
		const std::size_t corruptions = mlt::GetCorruptionCount();

		int* value = new int(3);
		MLT_CHECK(mlt::GetActiveAllocationCount() == before + 1);
		mlt::Close(); // reports `value` as a leak
		delete value;

		MLT_CHECK(mlt::GetActiveAllocationCount() == before);
		MLT_CHECK(mlt::GetCorruptionCount() == corruptions);
	}

	/** Bug: a second Init() rebuilt the tracker and lost its records. Blocks must also be freed with
	* the guard setting they were allocated with, not the current one. */
	void TestInitAgainKeepsRecords()
	{
		mlt::Init(false);
		const std::size_t before = mlt::GetActiveAllocationCount();
		const std::size_t corruptions = mlt::GetCorruptionCount();

		int* withoutGuards = new int(1);
		mlt::Init(true, 64);
		int* withGuards = new int(2);
		MLT_CHECK(mlt::GetActiveAllocationCount() == before + 2);

		mlt::Init(false);
		delete withGuards;
		mlt::Init(true, 512);
		delete withoutGuards;

		MLT_CHECK(mlt::GetActiveAllocationCount() == before);
		MLT_CHECK(mlt::GetCorruptionCount() == corruptions);
	}

	void TestOverflowIsDetected()
	{
		mlt::Init(true);
		const std::size_t corruptions = mlt::GetCorruptionCount();

		char* text = new char[4];
		text[Opaque(4)] = 'X';
		delete[] text;

		MLT_CHECK(mlt::GetCorruptionCount() == corruptions + 1);
	}

	/** Bug: the guards were filled with zeros, so writing a '\0' past the end was not detected. */
	void TestOverflowWithZeroByteIsDetected()
	{
		mlt::Init(true);
		const std::size_t corruptions = mlt::GetCorruptionCount();

		char* text = new char[4];
		text[Opaque(4)] = '\0';
		delete[] text;

		MLT_CHECK(mlt::GetCorruptionCount() == corruptions + 1);
	}

	/** Bug: the record sat between the front guard and the payload, so this write damaged the record
	* (its file pointer) and was never reported. */
	void TestUnderflowIntoGuardIsDetected()
	{
		mlt::Init(true);
		const std::size_t before = mlt::GetActiveAllocationCount();
		const std::size_t corruptions = mlt::GetCorruptionCount();

		char* text = new char[8];
		*(text - Opaque(25)) = 1;
		delete[] text;

		MLT_CHECK(mlt::GetCorruptionCount() == corruptions + 1);
		MLT_CHECK(mlt::GetActiveAllocationCount() == before);
	}

	void TestUnderflowIntoHeaderIsDetected()
	{
		for (int heapCheck = 0; heapCheck < 2; ++heapCheck)
		{
			mlt::Init(heapCheck != 0);
			const std::size_t before = mlt::GetActiveAllocationCount();
			const std::size_t corruptions = mlt::GetCorruptionCount();

			char* text = new char[8];
			*(text - Opaque(1)) = 0x55;
			delete[] text; // still found by its address, reported, and freed

			MLT_CHECK(mlt::GetCorruptionCount() == corruptions + 1);
			MLT_CHECK(mlt::GetActiveAllocationCount() == before);
		}
	}

	void TestCheckHeapCorruptionFindsDamageInLiveBlocks()
	{
		mlt::Init(true);
		const std::size_t corruptions = mlt::GetCorruptionCount();

		char* text = new char[4];
		char* pastEnd = text + Opaque(4);
		const char saved = *pastEnd;
		*pastEnd = 'X';
		mlt::CheckHeapCorruption();
		MLT_CHECK(mlt::GetCorruptionCount() == corruptions + 1);

		*pastEnd = saved;
		delete[] text;
		MLT_CHECK(mlt::GetCorruptionCount() == corruptions + 1);
	}

	/** Bug: size plus header could wrap around, giving the caller a tiny block. */
	void TestHugeAllocationThrowsBadAlloc()
	{
		mlt::Init(true);
		bool isThrown = false;
		try
		{
			char* huge = new char[Opaque(std::numeric_limits<std::size_t>::max() - 8)];
			delete[] huge;
		}
		catch (const std::bad_alloc&)
		{
			isThrown = true;
		}
		MLT_CHECK(isThrown);
	}

// The documented way to use placement and nothrow new after MemoryLT.h.
#pragma push_macro("new")
#undef new
	void TestNothrowAndPlacementNewWork()
	{
		mlt::Init();
		const std::size_t corruptions = mlt::GetCorruptionCount();

		int* value = new (std::nothrow) int(4);
		MLT_CHECK(value != nullptr);
		delete value;

		alignas(int) unsigned char buffer[sizeof(int)];
		int* placed = new (buffer) int(5);
		MLT_CHECK(*placed == 5);

		MLT_CHECK(mlt::GetCorruptionCount() == corruptions);
	}
#pragma pop_macro("new")

	/** Bug: Init()/Close() raced with operator new/delete on other threads (ThreadSanitizer). */
	void TestInitAndCloseWhileOtherThreadsAllocate()
	{
		mlt::Init();
		const std::size_t before = mlt::GetActiveAllocationCount();
		const std::size_t corruptions = mlt::GetCorruptionCount();

		const int workerCount = 4;
		const int cycleCount = 20;
		std::atomic<bool> isStopping(false);
		std::vector<std::thread> workers;
		for (int i = 0; i < workerCount; ++i)
		{
			workers.push_back(std::thread([&isStopping]()
			{
				while (!isStopping)
				{
					int* value = new int(1);
					std::vector<int> numbers(16, 2);
					delete value;
				}
			}));
		}

		for (int cycle = 0; cycle < cycleCount; ++cycle)
		{
			mlt::Init(cycle % 2 == 0, 32);
			mlt::CheckHeapCorruption();
			mlt::Close();
		}

		isStopping = true;
		for (std::size_t i = 0; i < workers.size(); ++i)
		{
			workers[i].join();
		}

		mlt::Init();
		MLT_CHECK(mlt::GetActiveAllocationCount() == before);
		MLT_CHECK(mlt::GetCorruptionCount() == corruptions);
	}
} //namespace

int main()
{
	TestTrackedAllocationIsCounted();
	TestUntrackedAllocationsAreFreedSafely();
	TestAllocationBeforeInitIsFreedSafely();
	TestDeleteAfterCloseIsSafe();
	TestInitAgainKeepsRecords();
	TestOverflowIsDetected();
	TestOverflowWithZeroByteIsDetected();
	TestUnderflowIntoGuardIsDetected();
	TestUnderflowIntoHeaderIsDetected();
	TestCheckHeapCorruptionFindsDamageInLiveBlocks();
	TestHugeAllocationThrowsBadAlloc();
	TestNothrowAndPlacementNewWork();
	TestInitAndCloseWhileOtherThreadsAllocate();

	mlt::Close();

	if (g_FailureCount != 0)
	{
		std::fprintf(stderr, "%d check(s) failed.\n", g_FailureCount);
		return 1;
	}
	std::printf("All MemoryLeaksTracker tests passed.\n");
	return 0;
}

#else //!MLT_ENABLED

int main()
{
	std::printf("MemoryLeaksTracker is disabled in this build (MLT_ENABLED=0). Nothing to test.\n");
	return 0;
}

#endif //MLT_ENABLED
