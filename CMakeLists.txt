cmake_minimum_required(VERSION 3.16)
project(modern_shell VERSION 1.0.0 LANGUAGES CXX)

# C++20 required
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Add compiler warnings
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic -Werror)
endif()

# Find required libraries
find_package(Threads REQUIRED)
find_library(READLINE_LIBRARY NAMES readline)
find_path(READLINE_INCLUDE_DIR NAMES readline/readline.h)

# Source files
set(SOURCES
    src/core/shell.cpp
    src/core/tokenizer.cpp
    src/core/parser.cpp
    src/core/environment.cpp
    src/core/executor.cpp
    src/config/config_manager.cpp
    src/plugins/plugin_manager.cpp
    src/main.cpp
)

# Add executable
add_executable(modern_shell ${SOURCES})

# Include directories
target_include_directories(modern_shell PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${READLINE_INCLUDE_DIR}
)

# Link libraries
target_link_libraries(modern_shell PRIVATE 
    Threads::Threads
    ${READLINE_LIBRARY}
    dl
)

# Install target
install(TARGETS modern_shell DESTINATION bin)

# Tests
option(BUILD_TESTS "Build the tests" ON)
if(BUILD_TESTS)
    enable_testing()
    
    # Find GTest
    find_package(GTest REQUIRED)
    
    # Test source files
    set(TEST_SOURCES
        test/tokenizer_test.cpp
        test/parser_test.cpp
        test/environment_test.cpp
    )
    
    # Add test executable
    add_executable(shell_tests ${TEST_SOURCES})
    
    # Include directories for tests
    target_include_directories(shell_tests PRIVATE 
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${GTEST_INCLUDE_DIRS}
    )
    
    # Link test libraries
    target_link_libraries(shell_tests PRIVATE 
        GTest::GTest
        GTest::Main
        Threads::Threads
        dl
    )
    
    # Add to test suite
    add_test(NAME shell_tests COMMAND shell_tests)
endif()