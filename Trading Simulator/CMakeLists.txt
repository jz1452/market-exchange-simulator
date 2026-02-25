cmake_minimum_required(VERSION 3.15)
project(HFT_Middleware VERSION 1.0 LANGUAGES CXX)

# Use modern C++20 for string_view, concepts, etc.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Aggressive compiler warnings
set(CMAKE_CXX_FLAGS "-Wall -Wextra -Wpedantic -Werror -O3 -march=native")

# Header directories
include_directories(include)

# Publisher Executable (The Market Data Feed)
add_executable(publisher src/publisher.cpp)
# Link against pthread if needed by OS (macOS/Linux)
find_package(Threads REQUIRED)
target_link_libraries(publisher Threads::Threads)

# Subscriber Executable (The Trading Algorithm Node)
add_executable(subscriber src/subscriber.cpp)
target_link_libraries(subscriber Threads::Threads)
