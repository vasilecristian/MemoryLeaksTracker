MemoryLeakTests - demo program for MemoryLeaksTracker
=====================================================

MemoryLeakTests.cpp shows what the tracker reports in a debug build:
  - a write after the end of a block    -> "[memory] CORRUPTION: after address ..."
  - a write before the start of a block -> "[memory] CORRUPTION: before address ..."
  - 50 threads that allocate and delete at the same time
  - comment out "delete pointerToTest;" to see a leak report

It only prints. It has no checks of its own.
The unit tests are in ../UnitTests (build them with CMake, see ../../README.md).

Do not run it in a release build: the tracker is off there, so its
out-of-bounds writes are real heap corruption.
