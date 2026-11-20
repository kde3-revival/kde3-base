include_directories(
  "${CMAKE_CURRENT_SOURCE_DIR}/../.."
  "${CMAKE_CURRENT_SOURCE_DIR}/.."
)

kde3_add_module(kio_smtp
  SOURCES
    smtp.cc
    request.cc
    response.cc
    capabilities.cc
    command.cc
    transactionstate.cc
  LINK
    KDE3::kio
    ${SASL2_LIBS}
)

install(FILES
  smtp.protocol
  smtps.protocol
  DESTINATION ${KDE3_SERVICES_INSTALL_DIR})
