// ReleaseStubsTest.cpp : checks the disabled build of MemoryLT.h (MLT_ENABLED=0).
//
// Bug: the disabled functions were defined in the header without `inline`, so including the
// header from two .cpp files failed to link (duplicate symbols). This file and
// ReleaseStubsHelper.cpp both include it.

#include <cstdio>

#include "MemoryLeaksTracker/MemoryLT.h"

void CallStubsFromHelper();

int main()
{
	mlt::Init(true, 64);
	CallStubsFromHelper();
	mlt::CheckHeapCorruption();

	const bool isOk = mlt::GetActiveAllocationCount() == 0 && mlt::GetCorruptionCount() == 0;
	mlt::Close();

	std::printf(isOk ? "Release stubs test passed.\n" : "Release stubs test FAILED.\n");
	return isOk ? 0 : 1;
}
