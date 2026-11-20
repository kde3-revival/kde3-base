include_directories(
  "${CMAKE_CURRENT_SOURCE_DIR}/../.."
  "${CMAKE_CURRENT_SOURCE_DIR}/.."
)

kde3_add_module(kio_pop3
  SOURCES
    pop3.cc
  LINK
    KDE3::kio
    ${SASL2_LIBS}
)

install(FILES
  pop3.protocol
  pop3s.protocol
  DESTINATION ${KDE3_SERVICES_INSTALL_DIR})
