# libxml2-config.cmake
# --------------------
#
# Libxml2 cmake module.
# THis module sets the following variables:
#
# ::
#
#   LIBXML2_INCLUDE_DIRS      - Directory where libxml2 headers are located.
#   LIBXML2_LIBRARIES         - xml2 libraries to link against.
#   LIBXML2_VERSION_MAJOR     - The major version of libxml2.
#   LIBXML2_VERSION_MINOR     - The minor version of libxml2.
#   LIBXML2_VERSION_PATCH     - The patch version of libxml2.
#   LIBXML2_VERSION_STRING    - version number as a string (ex: "2.3.4")
#   LIBXML2_MODULES           - whether libxml2 as dso support

get_filename_component(_libxml2_rootdir ${CMAKE_CURRENT_LIST_DIR}/../../../ ABSOLUTE)

set(LIBXML2_VERSION_MAJOR  2)
set(LIBXML2_VERSION_MINOR  9)
set(LIBXML2_VERSION_MICRO  7)
set(LIBXML2_VERSION_STRING "2.9.7")
set(LIBXML2_INSTALL_PREFIX ${_libxml2_rootdir})
set(LIBXML2_INCLUDE_DIRS   ${_libxml2_rootdir}/include ${_libxml2_rootdir}/include/libxml2)
set(LIBXML2_LIBRARY_DIR    ${_libxml2_rootdir}/lib)
set(LIBXML2_LIBRARIES      -L${LIBXML2_LIBRARY_DIR} -lxml2)

if(0)
  find_package(Threads REQUIRED)
  list(APPEND LIBXML2_LIBRARIES ${CMAKE_THREAD_LIBS_INIT})
endif()

if(0)
  find_package(LibLZMA REQUIRED)
  list(APPEND LIBXML2_LIBRARIES    ${LIBLZMA_LIBRARIES})
  list(APPEND LIBXML2_INCLUDE_DIRS ${LIBLZMA_INCLUDE_DIRS})
endif()

if(0)
  find_package(ZLIB REQUIRED)
  list(APPEND LIBXML2_LIBRARIES    ${ZLIB_LIBRARIES})
  list(APPEND LIBXML2_INCLUDE_DIRS ${ZLIB_INCLUDE_DIRS})
endif()

list(APPEND LIBXML2_LIBRARIES   -lm  )

# whether libxml2 has dso support
set(LIBXML2_MODULES 1)

mark_as_advanced( LIBXML2_INCLUDE_DIRS LIBXML2_LIBRARIES )
