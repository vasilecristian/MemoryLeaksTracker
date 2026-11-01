# MemoryLeaksTracker
MemoryLeaksTracker is a small memory leaks detector that use the overloaded operator new and delete.
It also detects writes just before and just after a block (heap corruption). C++11, no dependencies.

## Usage

```cpp
#include <vector>                          // all other includes first
#include "MemoryLeaksTracker/MemoryLT.h"   // always the last include

int main()
{
	mlt::Init(true);              // true: add guard areas to catch heap corruption
	int* value = new int(1);      // recorded with this file and line
	mlt::CheckHeapCorruption();   // optional: check all live blocks now
	mlt::Close();                 // prints the leaks: value was never deleted
}
```

Reports go to stderr:

```
[memory] LEAK: At address 0x600000c04010, size 4, main.cpp:7.
[memory] CORRUPTION: after address 0x600000c04010, size 4, main.cpp:7.
```

`mlt::GetActiveAllocationCount()` and `mlt::GetCorruptionCount()` return the same results as numbers, for tests.

## What is tracked
- Only `new` expressions in files that include `MemoryLT.h` are recorded.
- Not recorded: standard library allocations (containers, `std::make_shared`, ...), `malloc()`, and code that does not include the header.
- The tracker is on when `MLT_ENABLED` is 1. If it is not defined, it is on when `_DEBUG` or `DEBUG` is defined. With CMake it is on in Debug builds only.
- Build the library and your code with the same `MLT_ENABLED` value.

## Limitations
- `MemoryLT.h` defines `new` as a macro, so it must be the last include.
- After it, `new (buffer) T` and `new (std::nothrow) T` do not compile. Wrap such code:
  ```cpp
  #pragma push_macro("new")
  #undef new
  // placement or nothrow new here
  #pragma pop_macro("new")
  ```
- Leaks are printed when `mlt::Close()` runs, or at exit. Static objects destroyed after that may show up as leaks.
- On Windows, memory must be deleted by the same module (exe/dll) that allocated it.

## Build and test

Needs CMake 3.21 or newer. Every platform uses the same three steps: configure, build, test.

macOS / Linux, with Ninja:

```sh
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

Other presets: `release` (universal arm64 + x86_64 on macOS), `asan` (AddressSanitizer + UndefinedBehaviorSanitizer), `tsan` (ThreadSanitizer).

macOS, Xcode project (universal arm64 + x86_64):

```sh
cmake --preset xcode            # then open build/xcode/MemoryLeaksTracker.xcodeproj, or:
cmake --build --preset xcode-debug
ctest --preset xcode-debug
```

Windows, Visual Studio 2022 (x64 or Win32):

```bat
cmake --preset vs2022-x64       :: or vs2022-win32. Then open build\vs2022-x64\MemoryLeaksTracker.sln, or:
cmake --build --preset vs2022-x64-debug
ctest --preset vs2022-x64-debug
```

Replace `debug` with `release` in the build and test steps for a Release build.

VS Code (`.vscode/`), on macOS, Linux and Windows:
- Build (Cmd/Ctrl+Shift+B) configures and builds Debug.
- "Tasks: Run Test Task" runs the unit tests. The ASan and TSan test tasks are macOS/Linux only.
- F5 debugs the unit tests or the demo. Pick the configuration for your OS.
- On Windows the tasks use the `vs2022-x64` presets.
- On macOS the tasks build for the Mac's own architecture, so the debugger can attach.

Notes:
- macOS builds run on macOS 12.2 or later, like the Xcode project in `prj/`.
- On Windows the library uses the static C runtime (`/MT`, `/MTd`), like the projects in `prj/vs2019`. Code that links it must use the same runtime. When this repo is added with `add_subdirectory()`, the parent project's runtime is used instead.
- The Visual Studio 2019 and Xcode projects in `prj/` and `test/MemoryLeakTests/` still work.
