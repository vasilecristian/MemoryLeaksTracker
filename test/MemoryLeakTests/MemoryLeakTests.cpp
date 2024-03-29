// MemoryLeakTests.cpp : Defines the entry point for the console application.
//

#include "MemoryLeaksTracker/MemoryLT.h"

#include <thread>
#include <vector>


int a = 0;

class A : public mlt::BaseLeakTracker
{
public:
    A() :m_a(0), m_b(0)
    {
    }
    int m_a;
    int m_b;
};

A aa;
static A* hh = new A();

void testFn1()
{
    for (int i = 0; i < 100000; i++)
    {
        int* test = new int(0);
        delete test;
    }
}


int main(int argc, const char* argv[])
{
	mlt::Init(true);
	
    int* pointerToTest = new int(0);

    // This will generate a heap corruption in a memory space after the "pointerToTest"'s allocated memory!
    char* corruption1 = (char*)pointerToTest + 5;
    *corruption1 = 1;

    // This will generate a heap corruption in a memory space before the "pointerToTest"'s allocated memory!
    char* corruption2 = (char*)pointerToTest - 25;
    *corruption2 = 1;

    // Commenting this delete call will generate a leak!
    delete pointerToTest;

    mlt::CheckHeapCorruption();

    std::vector<std::thread> threads;
    for (int i = 0; i < 50; i++)
    {
        threads.push_back(std::thread(testFn1));
    }
    
    for (auto& thread : threads)
    {
        thread.join();
    }

    mlt::Close();
	return 0;
}

