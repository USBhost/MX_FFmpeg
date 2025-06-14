cmake_minimum_required(VERSION 3.10.2)

project("ffmpeg-index")

SET ( ff_base_dir	${CMAKE_SOURCE_DIR} )

include_directories(${ff_base_dir}
                    )

SET ( ff_src_dir	${ff_base_dir} )

FILE ( GLOB_RECURSE	ff_source_files
        ${ff_src_dir}/libavcodec/*.c
        ${ff_src_dir}/libavcodec/*/*.c
        ${ff_src_dir}/libavdevice/*.c
        ${ff_src_dir}/libavfilter/*.c
        ${ff_src_dir}/libavformat/*.c
        ${ff_src_dir}/libavresample/*.c
        ${ff_src_dir}/libavresample/*/*.c
        ${ff_src_dir}/libavutil/*.c
        ${ff_src_dir}/libavutil/*/*.c
        ${ff_src_dir}/libpostproc/*.c
        ${ff_src_dir}/libswresample/*.c
        ${ff_src_dir}/libswresample/*/*.c
        ${ff_src_dir}/libswscale/*.c
        ${ff_src_dir}/libswscale/*/*.c
)
LIST ( SORT			ff_source_files )


# this creates the static library (.a)
ADD_LIBRARY				( ffmpeg STATIC ${ff_source_files} )


#AUX_SOURCE_DIRECTORY(./ SOURCES)
#file(GLOB SOURCES1 ./libavcodec/*.c
##                   ./libavcodec/*/*.c
#                   ./libavdevice/*.c
#                   ./libavfilter/*.c
#                   ./libavformat/*.c
#[[                   ./libavresample/*.c
                   ./libavresample/*/*.c
                   ./libavutil/*.c
                   ./libavutil/*/*.c
                   ./libpostproc/*.c
                   ./libswresample/*.c
                   ./libswresample/*/*.c
                   ./libswscale/*.c
                   ./libswscale/*/*.c)]]
#[[add_library(
             ffmpeg.mx 
             SHARED
             ${SOURCES1}
             )]]

