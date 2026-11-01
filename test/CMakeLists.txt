# Unit tests, built twice: as C++11 and as C++17. The header must compile in both, and
# C++14 and later call the sized operator delete.
foreach(standard 11 17)
	set(target MemoryLTTests_cpp${standard})
	add_executable(${target} UnitTests/MemoryLTTests.cpp)
	target_link_libraries(${target} PRIVATE MemoryLeaksTracker)
	set_target_properties(${target} PROPERTIES CXX_STANDARD ${standard})
	add_test(NAME ${target} COMMAND ${target})
endforeach()

# The disabled header (MLT_ENABLED=0), included from two files to catch duplicate symbols.
add_executable(MemoryLTReleaseStubsTest UnitTests/ReleaseStubsTest.cpp UnitTests/ReleaseStubsHelper.cpp)
target_include_directories(MemoryLTReleaseStubsTest PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_compile_definitions(MemoryLTReleaseStubsTest PRIVATE MLT_ENABLED=0)
add_test(NAME MemoryLTReleaseStubsTest COMMAND MemoryLTReleaseStubsTest)

# The demo program. Built only, not run as a test: it writes out of bounds on purpose, which is
# real heap corruption when the tracker is disabled.
add_executable(MemoryLeakDemo MemoryLeakTests/MemoryLeakTests.cpp)
target_link_libraries(MemoryLeakDemo PRIVATE MemoryLeaksTracker)
