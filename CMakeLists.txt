set(CMAKE_POLICY_VERSION_MINIMUM 3.10 CACHE STRING "Force minimum policy version for dependencies" FORCE)

cmake_minimum_required(VERSION 3.24)
project(MyChess)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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

set(SOURCES 
    GUI/src/main.cpp
)

add_executable(${PROJECT_NAME} ${SOURCES})

target_link_libraries(${PROJECT_NAME} PRIVATE
    SFML::Graphics
    SFML::Window
    SFML::System
    SFML::Audio
)

target_include_directories(${PROJECT_NAME} PRIVATE
    bitboards
    data
    GUI/include
)