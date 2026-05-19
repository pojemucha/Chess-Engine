cmake_minimum_required(VERSION 3.10)
project(ChessEngine)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(SFML 2.5 COMPONENTS graphics audio window system REQUIRED)

include_directories(
    bitboards
    data
    GUI/include
)

set(SOURCES 
    GUI/src/main.cpp
)

add_executable(ChessEngine ${SOURCES})

target_link_libraries(ChessEngine sfml-graphics sfml-audio sfml-window sfml-system)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(ChessEngine PRIVATE -fconstexpr-ops-limit=5000000000000)
endif()