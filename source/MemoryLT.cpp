
#include "MemoryLeaksTracker/MemoryLT.h"
#if defined(_DEBUG)

/// Override (only for this file) the _HAS_EXCEPTIONS values. Otherwise a compile error is 
/// generated (aka: error C3861: '__uncaught_exception': identifier not found)
#ifdef _HAS_EXCEPTIONS
#undef _HAS_EXCEPTIONS
#endif //_HAS_EXCEPTIONS
#define _HAS_EXCEPTIONS 2


#include <new>
#include <exception>
#include <cstdio>
#include <cstdarg>
#include <thread>
#include <mutex>
#include <iostream>


#if defined(TRACK_STACK_TRACE)
    #include <windows.h>
    #include <dbghelp.h>
    #pragma comment(lib,"dbghelp.lib")

    #define MAX_STACK_FRAMES 16
    bool __trackStackTrace = false;
#endif //TRACK_STACK_TRACE


struct MemoryAllocationRecord
{
	unsigned long m_address;          // address returned to the caller after allocation
	unsigned int m_size;              // size of the allocation request
	const char* m_file;               // source file of allocation request
	int m_line;                       // source line of the allocation request
	MemoryAllocationRecord* m_next;
	MemoryAllocationRecord* m_prev;
	#if defined(TRACK_STACK_TRACE)
	bool m_trackStackTrace;
	unsigned int m_pc[MAX_STACK_FRAMES];
	#endif
};

MemoryAllocationRecord* __memoryAllocations = 0;
int __memoryAllocationCount = 0;

static std::mutex& GetMemoryAllocationMutex()
{
	static std::mutex m;
	return m;
}

namespace mlt
{
	void* DebugAlloc(std::size_t size, const char* file, int line);
	void DebugFree(void* p);
}

#if defined(_WIN32)
/// Is for Win32 and all windows platforms (including WinPhone & WinStore)

#ifdef _MSC_VER
#pragma warning( disable : 4290 )
#endif

void* operator new (std::size_t size, const char* file, int line)
{
	return mlt::DebugAlloc(size, file, line);
}

void* operator new[](std::size_t size, const char* file, int line)
{
	return operator new (size, file, line);
}

void* operator new (std::size_t size) throw(std::bad_alloc)
{
	return operator new (size, "", 0);
}

void* operator new[](std::size_t size) throw(std::bad_alloc)
{
	return operator new (size, "", 0);
}

void* operator new (std::size_t size, const std::nothrow_t&) throw()
{
	return operator new (size, "", 0);
}

void* operator new[](std::size_t size, const std::nothrow_t&) throw()
{
	return operator new (size, "", 0);
}

void operator delete (void* p) throw()
{
    mlt::DebugFree(p);
}

void operator delete[](void* p) throw()
{
    mlt::DebugFree(p);
}

void operator delete (void* p, const char* file, int line) throw()
{
    mlt::DebugFree(p);
}

void operator delete[](void* p, const char* file, int line) throw()
{
    mlt::DebugFree(p);
}

#ifdef _MSC_VER
#pragma warning( default : 4290 )
#endif

#endif //WIN32

namespace mlt
{
	void* DebugAlloc(std::size_t size, const char* file, int line)
	{
		// Allocate memory + size for a MemoryAlloctionRecord
		unsigned char* mem = (unsigned char*)malloc(size + sizeof(MemoryAllocationRecord));

		MemoryAllocationRecord* rec = (MemoryAllocationRecord*)mem;

		// Move memory pointer past record
		mem += sizeof(MemoryAllocationRecord);

		std::lock_guard<std::mutex> lock(GetMemoryAllocationMutex());
		rec->m_address = (unsigned long)mem;
		rec->m_size = (unsigned int)size;
		rec->m_file = file;
		rec->m_line = line;
		rec->m_next = __memoryAllocations;
		rec->m_prev = 0;

		// Capture the stack frame (up to MAX_STACK_FRAMES) if we 
		// are running on Windows and the user has enabled it.
		#if defined(TRACK_STACK_TRACE)
		rec->m_trackStackTrace = __trackStackTrace;
		if (rec->m_trackStackTrace)
		{
			static bool initialized = false;
			if (!initialized)
			{
				if (!SymInitialize(GetCurrentProcess(), NULL, true))
					std::cout << ("Stack trace tracking will not work.\n");
				initialized = true;
			}

			// Get the current context (state of EBP, EIP, ESP registers).
			static CONTEXT context;
			RtlCaptureContext(&context);

			static STACKFRAME64 stackFrame;
			memset(&stackFrame, 0, sizeof(STACKFRAME64));

			// Initialize the stack frame based on the machine architecture.
			#if defined(_M_IX86)
			static const DWORD machineType = IMAGE_FILE_MACHINE_I386;
			stackFrame.AddrPC.Offset = context.Eip;
			stackFrame.AddrPC.Mode = AddrModeFlat;
			stackFrame.AddrFrame.Offset = context.Ebp;
			stackFrame.AddrFrame.Mode = AddrModeFlat;
			stackFrame.AddrStack.Offset = context.Esp;
			stackFrame.AddrStack.Mode = AddrModeFlat;
			#elif defined (_M_X64)
			static const DWORD machineType = IMAGE_FILE_MACHINE_AMD64;
			stackFrame.AddrPC.Offset = context.Rip;
			stackFrame.AddrPC.Mode = AddrModeFlat;
			stackFrame.AddrFrame.Offset = context.Rdi;
			stackFrame.AddrFrame.Mode = AddrModeFlat;
			stackFrame.AddrStack.Offset = context.Rsp;
			stackFrame.AddrStack.Mode = AddrModeFlat;
			#else
			#error "Machine architecture not supported!"
			#endif

			// Walk up the stack and store the program counters.
			memset(rec->m_pc, 0, sizeof(rec->m_pc));
			for (int i = 0; i < MAX_STACK_FRAMES; i++)
			{
                rec->m_pc[i] = (unsigned int)stackFrame.AddrPC.Offset;
				if (!StackWalk64(machineType, GetCurrentProcess(), GetCurrentThread(), &stackFrame,
					&context, NULL, SymFunctionTableAccess64, SymGetModuleBase64, NULL))
				{
					break;
				}
			}
		}
		#endif //TRACK_STACK_TRACE

		if (__memoryAllocations)
			__memoryAllocations->m_prev = rec;
		__memoryAllocations = rec;
		++__memoryAllocationCount;

		return mem;
	}

	void DebugFree(void* p)
	{
		if (p == 0)
			return;

		// Backup passed in pointer to access memory allocation record
		void* mem = ((unsigned char*)p) - sizeof(MemoryAllocationRecord);

		MemoryAllocationRecord* rec = (MemoryAllocationRecord*)mem;

		// Sanity check: ensure that address in record matches passed in address
		if (rec->m_address != (unsigned long)p)
		{
			//std::cout << ("[memory] CORRUPTION: Attempting to free memory address with invalid memory allocation record.\n");
			return;
		}

		// Link this item out
		std::lock_guard<std::mutex> lock(GetMemoryAllocationMutex());
		if (__memoryAllocations == rec)
			__memoryAllocations = rec->m_next;
		if (rec->m_prev)
			rec->m_prev->m_next = rec->m_next;
		if (rec->m_next)
			rec->m_next->m_prev = rec->m_prev;
		--__memoryAllocationCount;

		// Free the address from the original alloc location (before mem allocation record)
		free(mem);
	}

	#if defined(TRACK_STACK_TRACE)
	void PrintStackTrace(MemoryAllocationRecord* rec)
	{
		const unsigned int bufferSize = 512;

		// Resolve the program counter to the corresponding function names.
		unsigned int pc;
		for (int i = 0; i < MAX_STACK_FRAMES; i++)
		{
			// Check to see if we are at the end of the stack trace.
			pc = rec->m_pc[i];
			if (pc == 0)
				break;

			// Get the function name.
			unsigned char buffer[sizeof(IMAGEHLP_SYMBOL64) + bufferSize];
			IMAGEHLP_SYMBOL64* symbol = (IMAGEHLP_SYMBOL64*)buffer;
			DWORD64 displacement;
			memset(symbol, 0, sizeof(IMAGEHLP_SYMBOL64) + bufferSize);
			symbol->SizeOfStruct = sizeof(IMAGEHLP_SYMBOL64);
			symbol->MaxNameLength = bufferSize;
			if (!SymGetSymFromAddr64(GetCurrentProcess(), pc, &displacement, symbol))
			{
				std::cout << ("[memory] STACK TRACE: <unknown location>\n");
			}
			else
			{
				symbol->Name[bufferSize - 1] = '\0';

				// Check if we need to go further up the stack.
				if (strncmp(symbol->Name, "operator new", 12) == 0)
				{
					// In operator new or new[], keep going...
				}
				else
				{
					// Get the file and line number.
					if (pc != 0)
					{
						IMAGEHLP_LINE64 line;
						DWORD displacement;
						memset(&line, 0, sizeof(line));
						line.SizeOfStruct = sizeof(line);
						if (!SymGetLineFromAddr64(GetCurrentProcess(), pc, &displacement, &line))
						{
							std::cout << "[memory] STACK TRACE: " << symbol->Name << " - <unknown file>:<unknown line number>\n";
						}
						else
						{
							const char* file = strrchr(line.FileName, '\\');
							if (!file)
								file = line.FileName;
							else
								file++;

							std::cout << "[memory] STACK TRACE: " << symbol->Name << " - " << file << ":" << line.LineNumber << "\n";
						}
					}
				}
			}
		}
	}
	#endif //TRACK_STACK_TRACE

	extern void PrintMemoryLeaks()
	{
		// Dump general heap memory leaks
		if (__memoryAllocationCount == 0)
		{
			std::cout << ("[memory] All HEAP allocations successfully cleaned up (no leaks detected).\n");
		}
		else
		{
			std::cout << "[memory] WARNING: ";
			std::cout << __memoryAllocationCount;
			std::cout << " HEAP allocations still active in memory.\n";

			MemoryAllocationRecord* rec = __memoryAllocations;
			while (rec)
			{
				#if defined(TRACK_STACK_TRACE)
				if (rec->m_trackStackTrace)
				{
					std::cout << "[memory] LEAK: At address ";
					std::cout << rec->m_address;
					std::cout << ", size ";
					std::cout << rec->m_size << ":\n";
					PrintStackTrace(rec);
				}
				else
				{
					if (strlen(rec->m_file) > 0)
					{
						std::cout << "[memory] LEAK: At address " << rec->m_address;
						std::cout << ", size " << rec->m_size;
                        std::cout << ", in " << rec->m_file;
                        std::cout << ":" << rec->m_line << " .\n";
					}
				}
				#else
				if (strlen(rec->m_file) > 0)
				{
                    std::cout << "[memory] LEAK: At address " << rec->m_address;
                    std::cout << ", size " << rec->m_size;
                    std::cout << ", in " << rec->m_file;
                    std::cout << ":" << rec->m_line << " .\n";
				}
				#endif
				rec = rec->m_next;
			}
		}
	}

	#if defined(TRACK_STACK_TRACE)
	void SetTrackStackTrace(bool trackStackTrace)
	{
		__trackStackTrace = trackStackTrace;
	}

	void ToggleTrackStackTrace()
	{
		__trackStackTrace = !__trackStackTrace;
	}
	#endif




	void* CustomAlloc(size_t size, const char* filename, int line)
	{
        return mlt::DebugAlloc(size, filename, line);
	}

	void* CustomAllocAlign(size_t size, size_t align, const char* filename, int line)
	{
		return mlt::DebugAlloc(size, filename, line);
	}

	void CustomFree(void* ptr)
	{
        mlt::DebugFree(ptr);
	}




	void* LeakTracker::operator new(size_t size)
	{
		return mlt::DebugAlloc(size, "", 0);
	}

	void LeakTracker::operator delete(void* p)
	{
		mlt::DebugFree(p);
	}

	void* LeakTracker::operator new[](size_t size)
	{
		return mlt::DebugAlloc(size, "", 0);
	}

	void LeakTracker::operator delete[](void* p)
	{
		mlt::DebugFree(p);
	};

	void* LeakTracker::operator new(size_t size, const char *file, int line)
	{
		return mlt::DebugAlloc(size, "", 0);
	};

	void LeakTracker::operator delete(void* p, const char *file, int line)
	{
        mlt::DebugFree(p);
	};

	void* LeakTracker::operator new[](size_t size, const char *file, int line)
	{
        return mlt::DebugAlloc(size, "", 0);;
	}

	void LeakTracker::operator delete[](void* p, const char *file, int line)
	{
        mlt::DebugFree(p);
	};
}

#endif //_DEBUG