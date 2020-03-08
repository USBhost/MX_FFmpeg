cmake_minimum_required(VERSION 3.2)

project(libsmb2
        LANGUAGES C
        VERSION 3.0.0)

set(SOVERSION 1 CACHE STRING "" FORCE)

set(INSTALL_LIB_DIR "${CMAKE_INSTALL_PREFIX}/lib${LIB_SUFFIX}" CACHE PATH "Installation directory for libraries")
set(INSTALL_INC_DIR "${CMAKE_INSTALL_PREFIX}/include" CACHE PATH "Installation directory for headers")
set(INSTALL_PKGCONFIG_DIR "${CMAKE_INSTALL_PREFIX}/lib${LIB_SUFFIX}/pkgconfig" CACHE PATH "Installation directory for pkgconfig (.pc) files")
set(INSTALL_CMAKE_DIR "${CMAKE_INSTALL_PREFIX}/lib${LIB_SUFFIX}/cmake/libsmb2" CACHE PATH "Installation directory for cmake (.cmake) files")

option(BUILD_SHARED_LIBS "Build shared libraries" ON)
option(ENABLE_EXAMPLES "Build example programs" OFF)

list(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake/Modules)

find_package(GSSAPI)
find_package(OpenSSL)

if(GSSAPI_FOUND)
  add_definitions(-DHAVE_LIBKRB5)
endif()

if(OPENSSL_FOUND)
  add_definitions(-DHAVE_OPENSSL_LIBS)
endif()

include(cmake/ConfigureChecks.cmake)

include_directories(${CMAKE_CURRENT_BINARY_DIR}
                    include
                    include/smb2
                    ${OPENSSL_INCLUDE_DIR})

set(core_DEPENDS ${GSSAPI_LIBRARIES} CACHE STRING "" FORCE)

if(CMAKE_SYSTEM_NAME MATCHES Windows)
  list(APPEND CORE_LIBRARIES ws2_32.lib)
  add_definitions(-DWIN32_LEAN_AND_MEAN -D_CRT_NONSTDC_NO_DEPRECATE -D_CRT_SECURE_NO_WARNINGS -DHAVE_SOCKADDR_IN6)
  include_directories(include/msvc)
elseif(CMAKE_SYSTEM_NAME STREQUAL Solaris)
  find_library(SOCKET_LIBRARY socket)
  find_library(NSL_LIBRARY nsl)
  list(APPEND CORE_LIBRARIES ${SOCKET_LIBRARY} ${NSL_LIBRARY})
endif()

if(ENABLE_EXAMPLES)
  add_subdirectory(examples)
endif()

add_subdirectory(lib)

include(CMakePackageConfigHelpers)
write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/libsmb2-config-version.cmake
                                 VERSION ${PROJECT_VERSION}
                                 COMPATIBILITY AnyNewerVersion)

configure_file(cmake/libsmb2.pc.cmake
               ${CMAKE_CURRENT_BINARY_DIR}/libsmb2.pc @ONLY)

install(DIRECTORY include/smb2
        DESTINATION ${INSTALL_INC_DIR})

install(FILES cmake/FindSMB2.cmake
              ${CMAKE_CURRENT_BINARY_DIR}/libsmb2-config-version.cmake
        DESTINATION ${INSTALL_CMAKE_DIR})

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/libsmb2.pc
        DESTINATION ${INSTALL_PKGCONFIG_DIR})
