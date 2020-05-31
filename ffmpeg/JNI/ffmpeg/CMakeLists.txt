cmake_minimum_required(VERSION 3.4.1)
include_directories(./
                    ../openssl-1.0.2j/include
                    ../libass-0.13.0/libass
                    ../libsmb2/include
                    ../zvbi-0.2.35/src
                    )
#AUX_SOURCE_DIRECTORY(./ SOURCES)
file(GLOB SOURCES1 ./libavcodec/*.c
                   ./libavcodec/*/*.c
                   ./libavdevice/*.c
                   ./libavfilter/*.c
                   ./libavformat/*.c
                   ./libavresample/*.c
                   ./libavresample/*/*.c
                   ./libavutil/*.c
                   ./libavutil/*/*.c
                   ./libpostproc/*.c
                   ./libswresample/*.c
                   ./libswresample/*/*.c
                   ./libswscale/*.c
                   ./libswscale/*/*.c)
add_library(
             ffmpeg.mx 
             SHARED
             ${SOURCES1}
             )

