cmake_minimum_required(VERSION 3.20)
project(andersen)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_COMPILER "clang++-17")

find_package(LLVM 17 REQUIRED CONFIG)
set(LLVM_USE_ZSTD OFF)
set(LLVM_LINK_COMPONENTS Core Support Analysis IRReader)

include_directories(${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})
llvm_map_components_to_libnames(LLVM_LIBS ${LLVM_LINK_COMPONENTS})

find_package(PkgConfig REQUIRED)
pkg_check_modules(GRAPHVIZ REQUIRED libcgraph libgvc)

set(ANDERSEN_FILES
    Andersen.cpp
    AndersenInfo.cpp
    ConstraintCollect.cpp
    ConstraintOptimize.cpp
    ConstraintSolving.cpp
    ExternalLibrary.cpp
    NodeFactory.cpp
    NodeMap.cxx
)

add_library(Andersen SHARED ${ANDERSEN_FILES})
target_include_directories(Andersen PUBLIC andersen ${CMAKE_CURRENT_SOURCE_DIR} ${GRAPHVIZ_INCLUDE_DIRS})
target_link_libraries(Andersen PUBLIC ${LLVM_LIBS} ${GRAPHVIZ_LIBRARIES})

add_executable(main main.cxx)
target_link_libraries(main Andersen m)
target_include_directories(main PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
