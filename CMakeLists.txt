# Minimum version of CMake that can parse this file
cmake_minimum_required(VERSION 3.16)

# Declare the project using C++ language
project(Pixels150 LANGUAGES CXX)

# Find libpng along with everything it needs
find_package(PNG REQUIRED)

# define our final executable, and compile it with C++17
add_executable(pixels150 Pixels150.cpp)
target_compile_features(pixels150 PRIVATE cxx_std_17)

# link libpng to our program
target_link_libraries(pixels150 PRIVATE PNG::PNG)

# extra compile options
if(MSVC)
  target_compile_options(pixels150 PRIVATE /W4)
else()
  target_compile_options(pixels150 PRIVATE -Wall -Wextra -Wpedantic)
endif()
