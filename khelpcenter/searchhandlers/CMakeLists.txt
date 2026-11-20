install(FILES
  htdig.desktop
  man.desktop
  docbook.desktop
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/khelpcenter/searchhandlers)

# configure.in.in: AC_OUTPUT(khelpcenter/searchhandlers/khc_*.pl)
set(prefix "${CMAKE_INSTALL_PREFIX}")
foreach(_script khc_htdig.pl khc_docbookdig.pl)
  configure_file(${_script}.in ${_script} @ONLY)
endforeach()

install(PROGRAMS
  ${CMAKE_CURRENT_BINARY_DIR}/khc_htdig.pl
  khc_htsearch.pl
  khc_mansearch.pl
  ${CMAKE_CURRENT_BINARY_DIR}/khc_docbookdig.pl
  DESTINATION ${KDE3_BIN_INSTALL_DIR})

install(FILES
  htdig_long.html
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/khelpcenter/searchhandlers/htdig)
