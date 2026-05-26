set(CMAKE_POLICY_VERSION_MINIMUM 3.10 CACHE STRING "Force minimum policy version for dependencies" FORCE)

cmake_minimum_required(VERSION 3.24)
project(MyChess)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(MyChess_UCI UCI/uci_main.cpp)

find_package(Threads REQUIRED)
target_link_libraries(MyChess_UCI PRIVATE Threads::Threads)
target_include_directories(MyChess_UCI PRIVATE
    bitboards
    data
)

include(fetchcontent)

set(SFML_BUILD_DOC OFF CACHE BOOL "Don't build SFML documentation" FORCE)
set(SFML_BUILD_EXAMPLES OFF CACHE BOOL "Don't build SFML examples" FORCE)
set(SFML_SHARED_LIBS OFF CACHE BOOL "Don't build SFML as shared libraries" FORCE)

message(STATUS "Fetching SFML from GitHub...")
FetchContent_Declare(
    SFML
    GIT_REPOSITORY https://github.com/SFML/SFML.git
    GIT_TAG 3.0.0
)

FetchContent_MakeAvailable(SFML)

add_executable(MyChess_GUI GUI/src/main.cpp)

target_link_libraries(MyChess_GUI PRIVATE
    SFML::Graphics
    SFML::Window
    SFML::System
    SFML::Audio
)

target_include_directories(MyChess_GUI PRIVATE
    bitboards
    data
)

if(MINGW)
    target_link_options(MyChess_GUI PRIVATE -static-libgcc -static-libstdc++ -static)
    target_link_options(MyChess_UCI PRIVATE -static-libgcc -static-libstdc++ -static)
endif()

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build." FORCE)
endif()

if(MSVC)
    add_compile_options(/O2 /Oi /Ot /GL)
else()
    add_compile_options(-O3 -march=native -flto)
endif()