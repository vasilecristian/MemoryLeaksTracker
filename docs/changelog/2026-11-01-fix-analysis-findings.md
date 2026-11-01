# 2026-11-01 — Fix the analysis findings

First changelog. Compared with the last commit (0208b32, "Changed the license.").

## Fixes
- Fixed bug: delete read memory before blocks it did not track (STL containers, make_shared). It now never guesses.
- Fixed bug: delete after `mlt::Close()` or at exit freed a wrong address. It now works at any time.
- Fixed bug: `Init()`, `Close()` and new/delete raced on other threads. The shared state is now atomic.
- Fixed bug: `CheckHeapCorruption()` walked the list without a lock.
- Fixed bug: a write just before a block damaged the record and was not reported. It is now reported.
- Fixed bug: a stray `'\0'` written past the end was not reported. Guards now use 0xFD, not 0.
- Fixed bug: addresses were printed cut to 32 bits.
- Fixed bug: a huge `new` could wrap around and return a tiny block. It now throws `std::bad_alloc`.
- Fixed bug: a failed malloc crashed instead of throwing `std::bad_alloc`.
- Fixed bug: a second `Init()` lost all records. It now only changes the settings.
- Fixed bug: one corrupted guard printed up to 256 lines. It now prints one.
- Fixed bug: sizes over 4 GB were cut in the record.
- Fixed bug: the header did not compile as C++17 or later.
- Fixed bug: the release header failed to link when included in two files.
- Fixed bug: the demo did not compile with current libc++ (include order).

## Changes
- Reports go to stderr, not stdout.
- `mlt::BaseLeakTracker` is removed. The global operators already track the same allocations.
- Every allocation now has a small header (16 bytes; more when tracked). Debug builds only.
- A delete with a damaged header, not found in the list, is reported and leaked. It is not freed.
- New: `mlt::GetActiveAllocationCount()` and `mlt::GetCorruptionCount()`.
- New: `MLT_ENABLED` switches the tracker on or off. It defaults to `_DEBUG` or `DEBUG`, as before.
- The guard size is now 16 bytes to 1 MiB, rounded up to 16.

## Build and tests
- New CMake build with presets: debug, release, asan, tsan.
- New unit tests, built as C++11 and C++17. They pass under ASan and TSan.
- The VS2019 and Xcode projects are unchanged and still build.

## Docs
- README explains usage, what is tracked, and the limits.
- CLAUDE.md now describes this repo.
