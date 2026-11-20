# ksysguardd/configure.in.in: the platform part is a static library in
# the subdirectory $(UNAME).  Solaris, Tru64 and Irix are not supported.
add_compile_definitions(
  "KSYSGUARDDRCFILE=\"${KDE3_FULL_SYSCONF_INSTALL_DIR}/ksysguarddrc\""
  OSTYPE_${KSYSGUARDD_UNAME})
include_directories(
  "${CMAKE_CURRENT_SOURCE_DIR}/../CContLib"
  "${CMAKE_CURRENT_SOURCE_DIR}/${KSYSGUARDD_UNAME}")

add_subdirectory(${KSYSGUARDD_UNAME})

kde3_add_executable(ksysguardd
  SOURCES
    Command.c
    conf.c
    ksysguardd.c
    PWUIDCache.c
  LINK
    ksysguardd_${KSYSGUARDD_UNAME}
    ccont
    ${KSYSGUARDD_LIBHOSTS}
)
if(HAVE_DNSSD)
  target_link_libraries(ksysguardd PRIVATE ${DNSSD_LIBRARY})
endif()
