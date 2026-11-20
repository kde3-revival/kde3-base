add_compile_definitions(LDAP_DEPRECATED)

kde3_add_module(kio_ldap
  SOURCES
    kio_ldap.cpp
  LINK
    KDE3::kio
    ${LDAP_LIBS}
    KDE3::kabc
)

install(FILES
  ldap.protocol
  ldaps.protocol
  DESTINATION ${KDE3_SERVICES_INSTALL_DIR})
