cmake_minimum_required(VERSION 3.10)
project(moodberryd)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Output binary path
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Find YAML-CPP
find_package(yaml-cpp REQUIRED)

# Source file(s)
add_executable(moodberryd
    src/main.cpp
)

# Link dependencies
target_link_libraries(moodberryd
    yaml-cpp
)

# Installation step (optional)
install(TARGETS moodberryd DESTINATION bin)
