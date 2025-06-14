set(SOURCES aes.c
            aes128ccm.c
            alloc.c
            dcerpc.c
            dcerpc-lsa.c
            dcerpc-srvsvc.c
            errors.c
            init.c
            hmac.c
            hmac-md5.c
            krb5-wrapper.c
            libsmb2.c
            md4c.c
            md5.c
            ntlmssp.c
            pdu.c
	    sha1.c
	    sha224-256.c
	    sha384-512.c
            smb2-cmd-close.c
            smb2-cmd-create.c
            smb2-cmd-echo.c
            smb2-cmd-error.c
            smb2-cmd-flush.c
            smb2-cmd-ioctl.c
            smb2-cmd-logoff.c
            smb2-cmd-negotiate.c
            smb2-cmd-query-directory.c
            smb2-cmd-query-info.c
            smb2-cmd-read.c
            smb2-cmd-session-setup.c
            smb2-cmd-set-info.c
            smb2-cmd-tree-connect.c
            smb2-cmd-tree-disconnect.c
            smb2-cmd-write.c
            smb2-data-file-info.c
            smb2-data-filesystem-info.c
            smb2-data-security-descriptor.c
            smb2-data-reparse-point.c
	    smb2-share-enum.c
	    smb3-seal.c
	    smb2-signing.c
            socket.c
            sync.c
            timestamps.c
            unicode.c
	    usha.c)

add_library(smb2 ${SOURCES})
target_link_libraries(smb2 PUBLIC ${core_DEPENDS} ${CORE_LIBRARIES})
set_target_properties(smb2 PROPERTIES
                           VERSION ${PROJECT_VERSION}
                           SOVERSION ${SOVERSION})
if(NOT MSVC)
  add_definitions("-D_U_=__attribute__((unused))")
else()
  add_definitions("-D_U_=")
  if(CMAKE_SYSTEM_NAME STREQUAL WindowsStore)
    add_definitions("-D_MSC_UWP")
  endif()
endif()

install(TARGETS smb2 EXPORT smb2
                     RUNTIME DESTINATION bin
                     ARCHIVE DESTINATION lib${LIB_SUFFIX}
                     LIBRARY DESTINATION lib${LIB_SUFFIX})
