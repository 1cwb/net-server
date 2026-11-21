cmake_minimum_required(VERSION 3.8)

project(app_center CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)

file(GLOB_RECURSE APP_CENTER_SRCS ${PROJECT_SOURCE_DIR}/src/*.cpp)

add_executable(app_center ${APP_CENTER_SRCS})

target_include_directories(app_center PRIVATE ${PROJECT_SOURCE_DIR}/src)

target_compile_options(app_center PRIVATE -Wall -Wextra)

target_link_libraries(app_center PRIVATE Threads::Threads)

install(TARGETS app_center DESTINATION bin)
