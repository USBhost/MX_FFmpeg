list(APPEND CORE_LIBRARIES ${POPT_LIBRARY})

set(SOURCES smb2-cat-async
            smb2-cat-sync
            smb2-ftruncate-sync
            smb2-ls-async
            smb2-ls-sync
            smb2-put-async
            smb2-put-sync
            smb2-raw-stat-async
            smb2-raw-getsd-async
            smb2-readlink
            smb2-share-enum
            smb2-stat-sync
            smb2-truncate-sync)

foreach(TARGET ${SOURCES})
  add_executable(${TARGET} ${TARGET}.c)
  target_link_libraries(${TARGET} ${CORE_LIBRARIES})
  add_dependencies(${TARGET} smb2)
endforeach()

add_definitions(-Werror "-D_U_=__attribute__((unused))")
