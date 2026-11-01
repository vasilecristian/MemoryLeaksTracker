// ReleaseStubsHelper.cpp : second file that includes MemoryLT.h, for ReleaseStubsTest.cpp.

#include "MemoryLeaksTracker/MemoryLT.h"

void CallStubsFromHelper()
{
	mlt::Init();
	mlt::CheckHeapCorruption();
	mlt::Close();
}
