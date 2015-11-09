cmake_minimum_required(VERSION 2.8.0)

project(libmodplug)
add_definitions(-DMODPLUG_BUILD)

include (CheckFunctionExists)

include_directories(AFTER
  src
  src/libmodplug
  ${PROJECT_BINARY_DIR}
  )

if (WIN32)
  add_definitions(-D_USE_MATH_DEFINES)
  add_definitions(-DNOMINMAX)
endif()

if (WIN32 AND NOT (MINGW OR MSYS))
  set(MSINTTYPES_PATH "$ENV{MSINTTYPES_PATH}" CACHE PATH "search path for inttypes.h and stdint.h")

  find_path(STDINT_INCLUDE_DIR
    stdint.h
    PATHS
    ${MSINTTYPES_PATH})

  if (STDINT_INCLUDE_DIR)
    add_definitions(-DHAVE_STDINT_H)
    include_directories(AFTER "${STDINT_INCLUDE_DIR}")
  endif()

  find_path(INTTYPES_INCLUDE_DIR
    inttypes.h
    PATHS
    ${MSINTTYPES_PATH})

  if (INTTYPES_INCLUDE_DIR)
    add_definitions(-DHAVE_INTTYPES_H)
    include_directories(AFTER "${INTTYPES_INCLUDE_DIR}")
  endif()

  if (NOT STDINT_INCLUDE_DIR OR NOT INTTYPES_INCLUDE_DIR)
    message(WARNING
      "Compilation may fail if inttypes.h is not natively supported by the compiler."
      "You can get inttypes.h from http://code.google.com/p/msinttypes/")
  endif()
endif()

check_function_exists("setenv" HAVE_SETENV)
check_function_exists("sinf" HAVE_SINF)

# Allow the developer to select if Dynamic or Static libraries are built
option(BUILD_SHARED_LIBS "Build Shared Library (DLL)" OFF)

# Set the LIB_TYPE variable to STATIC
set(LIB_TYPE STATIC)

if (BUILD_SHARED_LIBS)
  # User wants to build Dynamic Libraries,
  # so change the LIB_TYPE variable to CMake keyword 'SHARED'
  set (LIB_TYPE SHARED)
  add_definitions(-DDLL_EXPORT)
else (BUILD_SHARED_LIBS)
  add_definitions(-DMODPLUG_STATIC)
endif (BUILD_SHARED_LIBS)

add_library(modplug ${LIB_TYPE}
  src/libmodplug/it_defs.h
  src/libmodplug/sndfile.h
  src/libmodplug/stdafx.h

  src/fastmix.cpp
  src/load_669.cpp
  src/load_abc.cpp
  src/load_amf.cpp
  src/load_ams.cpp
  src/load_dbm.cpp
  src/load_dmf.cpp
  src/load_dsm.cpp
  src/load_far.cpp
  src/load_it.cpp
  src/load_j2b.cpp
  src/load_mdl.cpp
  src/load_med.cpp
  src/load_mid.cpp
  src/load_mod.cpp
  src/load_mt2.cpp
  src/load_mtm.cpp
  src/load_okt.cpp
  src/load_pat.cpp
  src/load_pat.h
  src/load_psm.cpp
  src/load_ptm.cpp
  src/load_s3m.cpp
  src/load_stm.cpp
  src/load_ult.cpp
  src/load_umx.cpp
  src/load_wav.cpp
  src/load_xm.cpp
  src/mmcmp.cpp
  src/modplug.cpp
  src/modplug.h
  src/snd_dsp.cpp
  src/snd_flt.cpp
  src/snd_fx.cpp
  src/sndfile.cpp
  src/sndmix.cpp
  src/tables.h
  )

# install the library:
install(TARGETS modplug DESTINATION lib)

# incstall the headers:
install(FILES
  src/libmodplug/it_defs.h
  src/libmodplug/sndfile.h
  src/libmodplug/stdafx.h
  src/modplug.h

  DESTINATION
  include/libmodplug
  )

set(VERSION "0.8.8.5")

if(HAVE_SETENV)
  add_definitions(-DHAVE_SETENV)
endif(HAVE_SETENV)
if(HAVE_SINF)
  add_definitions(-DHAVE_SINF)
endif(HAVE_SINF)

if (NOT WIN32)
  set(prefix "${CMAKE_INSTALL_PREFIX}")
  set(exec_prefix "${CMAKE_INSTALL_PREFIX}")
  set(libdir "${CMAKE_INSTALL_PREFIX}/lib")
  set(includedir "${CMAKE_INSTALL_PREFIX}/include")
  configure_file(libmodplug.pc.in libmodplug.pc)

  # install pkg-config file:
  install(FILES "${PROJECT_BINARY_DIR}/libmodplug.pc"
    DESTINATION lib/pkgconfig
  )
endif (NOT WIN32)
