cmake_minimum_required(VERSION 3.4.1)
include_directories(./
                    ../freetype-2.6
                    )

file(GLOB SOURCES lib/*.c)

add_library(
             fribid 
             SHARED
             ${SOURCES}
             )

