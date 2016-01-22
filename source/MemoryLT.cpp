
#include "MemoryLeaksTracker/MemoryLT.h"

#if defined(_DEBUG)

/// Override (only for this file) the _HAS_EXCEPTIONS values. Otherwise a compile error is 
/// generated (aka: error C3861: '__uncaught_exception': identifier not found)
#ifdef _HAS_EXCEPTIONS
#undef _HAS_EXCEPTIONS
#endif //_HAS_EXCEPTIONS
#define _HAS_EXCEPTIONS 2


#include <cstdio>
#include <cstdarg>
#include <thread>
#include <iostream>
#include <array>


#if defined(TRACK_STACK_TRACE)
    #include <windows.h>
    #include <dbghelp.h>
    #pragma comment(lib,"dbghelp.lib")

    #define MAX_STACK_FRAMES 16
#endif //TRACK_STACK_TRACE

namespace mlt
{
    ///----
    AllocFuncPtr s_allocFuncPtr = AllocInit;
    FreeFuncPtr  s_FreeFuncPtr = FreeNormal;

    /** We reserve some memory to hold the mem for s_leakTracker */
    static char s_memleakTracker[sizeof(LeakTracker)] = { 0 };

    /** Is the static pointer for leakTracker*/
    static LeakTracker* s_leakTracker = nullptr;


    /** Implement our first allocation function that constructs 
    * our heap on first use.*/
    static void* AllocInit(unsigned int size, const char* file, unsigned int line)
    {
        s_allocFuncPtr = AllocInit2;
        s_leakTracker = new(s_memleakTracker) LeakTracker;
        s_allocFuncPtr = AllocNormal;

        return AllocNormal(size, file, line);
    }

    /** This gets called during the initialization of s_leakTracker only */
    static void* AllocInit2(unsigned int size, const char* file, unsigned int line)
    {
        return malloc(size);
    }

    /** The normal allocator that runs after heap construction, and before
    * its eventual destruction. */
    static void* AllocNormal(unsigned int size, const char* file, unsigned int line)
    {
        return s_leakTracker->Alloc(size, file, line);
    }

    /** Allocates memory during shutdown of the memory manager*/
    static void* AllocShutdown(unsigned int size, const char* file, unsigned int line)
    {
        return malloc(size);
    }

    static void LeakTrackerExit()
    {
        s_leakTracker->PrintMemoryLeaks();

        s_leakTracker->~LeakTracker();

        // leakCheckReport();
    }

    static void FreeShutdown(void* mem)
    {
        free(mem);
    }

    static void FreeNormal(void* mem)
    {
        s_leakTracker->Free(mem);
    }
    ///----


    struct MemoryAllocationRecord
    {
        /**address returned to the caller after allocation*/
        unsigned long m_address;         

        /**size of the allocation request*/
        unsigned int m_size;              

        /**source file of allocation request*/
        const char* m_file;               

        /**source line of the allocation request*/
        unsigned int m_line;                       

        /**linked list next node*/
        MemoryAllocationRecord* m_next;
        /**linked list prev node*/
        MemoryAllocationRecord* m_prev;

        #if defined(TRACK_STACK_TRACE)
        bool m_trackStackTrace;
        std::array<unsigned int, MAX_STACK_FRAMES> m_pc;
        #endif
    };


	std::atomic<bool> LeakTracker::s_trackStackTrace = { false };

    LeakTracker::LeakTracker() 
        : m_memoryAllocations(0)
        , m_memoryAllocationCount(0)
        , m_maxSize(0)
        , m_maxLine(0)
    {
        atexit(LeakTrackerExit);

        // construct your heap here
    }

    LeakTracker::~LeakTracker()
    {
        s_allocFuncPtr = AllocShutdown;

        // destruct your heap here

        s_FreeFuncPtr = FreeShutdown;
    }

    void* LeakTracker::Alloc(std::size_t size, const char* file, unsigned int line)
    {
        /// Allocate memory + size for a MemoryAlloctionRecord
        unsigned char* mem = (unsigned char*)malloc(size + sizeof(MemoryAllocationRecord));

        /// Cast
        MemoryAllocationRecord* rec = (MemoryAllocationRecord*)mem;

        /// Move memory pointer past record
        mem += sizeof(MemoryAllocationRecord);

        m_m.lock();
        rec->m_address = (unsigned long)mem;
        rec->m_size = (unsigned int)size;
        rec->m_file = file;
        rec->m_line = line;
        rec->m_next = m_memoryAllocations;
        rec->m_prev = 0;

        if (m_maxSize < size)
            m_maxSize = size;
        if (m_maxLine < line)
            m_maxLine = line;


        #if defined(TRACK_STACK_TRACE)
        /// Capture the stack frame (up to MAX_STACK_FRAMES) if we 
        /// are running on Windows and the user has enabled it.
        rec->m_trackStackTrace = s_trackStackTrace;
        if (rec->m_trackStackTrace)
        {
            static bool initialized = false;
            if (!initialized)
            {
                if (!SymInitialize(GetCurrentProcess(), NULL, true))
                    std::cout << ("Stack trace tracking will not work.\n");
                initialized = true;
            }

            /// Get the current context (state of EBP, EIP, ESP registers).
            static CONTEXT context;
            RtlCaptureContext(&context);

            static STACKFRAME64 stackFrame;
            memset(&stackFrame, 0, sizeof(STACKFRAME64));

            /// Initialize the stack frame based on the machine architecture.
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

            /// Walk up the stack and store the program counters.
            for (auto& pc : rec->m_pc)
            {
                pc = (unsigned int)stackFrame.AddrPC.Offset;
                if (!StackWalk64(machineType, GetCurrentProcess(), GetCurrentThread(), &stackFrame,
                                 &context, NULL, SymFunctionTableAccess64, SymGetModuleBase64, NULL))
                {
                    break;
                }
            }
        }
        #endif //TRACK_STACK_TRACE

        if (m_memoryAllocations)
            m_memoryAllocations->m_prev = rec;
        m_memoryAllocations = rec;
        ++m_memoryAllocationCount;

        m_m.unlock();
        return mem;
    }

    void LeakTracker::Free(void* p)
    {
        if (p == 0)
            return;

        /// Backup passed in pointer to access memory allocation record
        void* mem = ((unsigned char*)p) - sizeof(MemoryAllocationRecord);

        MemoryAllocationRecord* rec = (MemoryAllocationRecord*)mem;

        /// Sanity check: ensure that address in record matches passed in address
        if (rec->m_address != (unsigned long)p)
        {
            //std::cout << ("[memory] CORRUPTION: Attempting to free memory address with invalid memory allocation record (wrong address).\n");
            //std::cout << "A";
            return;
        }

        /// Sanity check: ensure that size is smaller than maximum (tracked)
        if (rec->m_size > m_maxSize)
        {
            //std::cout << ("[memory] CORRUPTION: Attempting to free memory address with invalid memory allocation record (wrong size).\n");
            //std::cout << "S";
            return;
        }

        /// Sanity check: ensure that line is smaller than maximum (tracked)
        if (rec->m_line > m_maxLine)
        {
            //std::cout << ("[memory] CORRUPTION: Attempting to free memory address with invalid memory allocation record (wrong line).\n");
            //std::cout << "L";
            return;
        }

        /// Link this item out
        m_m.lock();
        if (m_memoryAllocations == rec)
            m_memoryAllocations = rec->m_next;
        if (rec->m_prev)
            rec->m_prev->m_next = rec->m_next;
        if (rec->m_next)
            rec->m_next->m_prev = rec->m_prev;
        --m_memoryAllocationCount;
        m_m.unlock();

        /// Free the address from the original alloc location (before mem allocation record)
        free(mem);
    }

    void LeakTracker::PrintMemoryLeaks()
    {
        printf("\n");

		s_leakTracker->m_m.lock();
        /// Dump general heap memory leaks
		if (s_leakTracker->m_memoryAllocationCount == 0)
        {
            printf("[memory] All HEAP allocations successfully cleaned up (no leaks detected).\n");
        }
        else
        {
            printf("[memory] WARNING: %d  HEAP allocations still active in memory.\n", m_memoryAllocationCount);

			MemoryAllocationRecord* rec = s_leakTracker->m_memoryAllocations;
            while (rec)
            {
                #if defined(TRACK_STACK_TRACE)
                if (rec->m_trackStackTrace)
                {
                    printf("[memory] LEAK: At address %d, size %d:\n", rec->m_address, rec->m_size);
					s_leakTracker->PrintStackTrace(rec);
                }
                else
                {
					if (rec->m_file && strlen(rec->m_file) > 0)
                    {
                        printf("[memory] LEAK: At address %d, size %d, %s:%d.\n", rec->m_address, rec->m_size, rec->m_file, rec->m_line);
                    }
                }
                #else //!TRACK_STACK_TRACE
                if (strlen(rec->m_file) > 0)
                {
                    printf("[memory] LEAK: At address %d, size %d, %s:%d.\n", rec->m_address, rec->m_size, rec->m_file, rec->m_line);
                }
                #endif //TRACK_STACK_TRACE
                rec = rec->m_next;
            }
        }

		s_leakTracker->m_m.unlock();
    }


    #if defined(TRACK_STACK_TRACE)
    void LeakTracker::PrintStackTrace(MemoryAllocationRecord* rec)
    {
        const unsigned int bufferSize = 512;

        /// Resolve the program counter to the corresponding function names.
        unsigned int pc;
        for (int i = 0; i < MAX_STACK_FRAMES; i++)
        {
            /// Check to see if we are at the end of the stack trace.
            pc = rec->m_pc[i];
            if (pc == 0)
                break;

            /// Get the function name.
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

                /// Check if we need to go further up the stack.
                if (strncmp(symbol->Name, "operator new", 12) == 0)
                {
                    /// In operator new or new[], keep going...
                }
                else
                {
                    /// Get the file and line number.
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

    void LeakTracker::SetTrackStackTrace(bool trackStackTrace)
    {
        s_trackStackTrace = trackStackTrace;
    }
    #endif //TRACK_STACK_TRACE
}



#ifdef _MSC_VER
#pragma warning( disable : 4290 )
#endif

void* operator new (std::size_t size, const char* file, int line)
{
    return mlt::s_allocFuncPtr(size, file, line);
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
    mlt::s_FreeFuncPtr(p);
}

void operator delete[](void* p) throw()
{
    mlt::s_FreeFuncPtr(p);
}

void operator delete (void* p, const char* file, int line) throw()
{
    mlt::s_FreeFuncPtr(p);
}

void operator delete[](void* p, const char* file, int line) throw()
{
    mlt::s_FreeFuncPtr(p);
}

#ifdef _MSC_VER
#pragma warning( default : 4290 )
#endif


namespace mlt
{
    void* ILeakTracker::operator new(size_t size)
	{
        return mlt::s_allocFuncPtr(size, "", 0);
	}

    void ILeakTracker::operator delete(void* p)
	{
        mlt::s_FreeFuncPtr(p);
    }

    void* ILeakTracker::operator new[](size_t size)
	{
        return mlt::s_allocFuncPtr(size, "", 0);
    }

    void ILeakTracker::operator delete[](void* p)
	{
        mlt::s_FreeFuncPtr(p);
	};

    void* ILeakTracker::operator new(size_t size, const char *file, int line)
	{
        return mlt::s_allocFuncPtr(size, file, line);
	};

    void ILeakTracker::operator delete(void* p, const char *file, int line)
	{
        mlt::s_FreeFuncPtr(p);
	};

    void* ILeakTracker::operator new[](size_t size, const char *file, int line)
	{
        return mlt::s_allocFuncPtr(size, file, line);
	}

    void ILeakTracker::operator delete[](void* p, const char *file, int line)
	{
        mlt::s_FreeFuncPtr(p);
	};
}

#endif //_DEBUG