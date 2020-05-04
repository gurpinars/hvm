cmake_minimum_required(VERSION 3.5)
project(hvm)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} --std=gnu11")


set(SOURCE_FILES
       hvm.c
        )
add_executable(hvm ${SOURCE_FILES})