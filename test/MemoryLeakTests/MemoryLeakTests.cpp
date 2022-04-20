// MemoryLeakTests.cpp : Defines the entry point for the console application.
//

#include "MemoryLeaksTracker/MemoryLT.h"

#include <thread>
#include <vector>

int a = 0;

class A : public mlt::ILeakTracker
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
    int* test = new int(0);
    delete test;

    std::vector<std::thread> threads;
    for (int i = 0; i < 50; i++)
    {
        threads.push_back(std::thread(testFn1));
    }
    
    for (auto& thread : threads)
    {
        thread.join();
    }

    mlt::LeakTracker::PrintMemoryLeaks();
	return 0;
}


