cmake_minimum_required(VERSION 3.10)

project(xiso-ftp.exe C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(CURL REQUIRED)

add_executable(xiso-ftp.exe src/xiso_ftp.c src/xdvdfs.c src/ftp.c)

target_link_libraries(xiso-ftp.exe CURL::libcurl)