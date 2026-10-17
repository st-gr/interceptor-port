cmake_minimum_required(VERSION 3.16)
project(vamiga_ref CXX)
# VAMIGA_SRC points at a vAmiga checkout; its Core builds as the VACore library.
add_subdirectory(${VAMIGA_SRC}/Core ${CMAKE_BINARY_DIR}/core EXCLUDE_FROM_ALL)
add_executable(ref ref.cpp)
target_link_libraries(ref PRIVATE VACore)
