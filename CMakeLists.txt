cmake_minimum_required(VERSION 3.10)

project(xiso-ftp.exe C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(CURL REQUIRED)

add_library(lzma STATIC
    lib/lzma/Bcj2.c
    lib/lzma/Bra.c
    lib/lzma/Bra86.c
    lib/lzma/Delta.c
    lib/lzma/Lzma2Dec.c
    lib/lzma/LzmaDec.c
    lib/lzma/7zCrc.c
    lib/lzma/7zCrcOpt.c
    lib/lzma/7zAlloc.c
    lib/lzma/7zArcIn.c
    lib/lzma/7zBuf.c
    lib/lzma/7zDec.c
    lib/lzma/7zFile.c
    lib/lzma/7zStream.c
)
target_include_directories(lzma PUBLIC lib/lzma)

add_executable(xiso-ftp.exe src/xiso_ftp.c src/xdvdfs.c src/ftp.c src/utils.c)
target_link_libraries(xiso-ftp.exe CURL::libcurl lzma)