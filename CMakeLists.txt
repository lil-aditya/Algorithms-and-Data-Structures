cmake_minimum_required(VERSION 3.10)

# Project name and language
project(ADIPE VERSION 1.0 LANGUAGES CXX)

# Require C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Generate compile_commands.json automatically for clangd IDE integration
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# All source files
set(SOURCES
    src/main.cpp
    src/Node.cpp
    src/graph_routing.cpp
    src/hashing.cpp
    src/hashmap.cpp
    src/number_theory.cpp
    src/priority_engine.cpp
    src/rsa_signature.cpp
    src/logger.cpp
    src/packet_store.cpp
)

# Create the main executable target
add_executable(dsa_project ${SOURCES})

# Add the include directory so compiler finds our .hpp and 3rd party headers
target_include_directories(dsa_project PRIVATE include)

# Platform-specific definitions and libraries
if(WIN32)
    # Prevent Windows headers from causing conflicts with httplib/standard C++
    target_compile_definitions(dsa_project PRIVATE
        _WIN32_WINNT=0x0A00
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
    # Windows requires explicitly linking the WinSock2 library for networking
    target_link_libraries(dsa_project PRIVATE ws2_32)
endif()

# We heavily use multithreading (server thread, worker thread)
find_package(Threads REQUIRED)
target_link_libraries(dsa_project PRIVATE Threads::Threads)

# Compiler flags for warnings and optimization
if(MSVC)
    target_compile_options(dsa_project PRIVATE /W4 /O2)
else()
    target_compile_options(dsa_project PRIVATE -Wall -Wextra -O2 -g)
endif()
