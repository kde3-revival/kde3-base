install(FILES
  northafrica.desktop
  westafrica.desktop
  southafrica.desktop
  centralafrica.desktop
  caribbean.desktop
  oceania.desktop
  centralamerica.desktop
  northamerica.desktop
  southamerica.desktop
  easteurope.desktop
  westeurope.desktop
  centraleurope.desktop
  northeurope.desktop
  southeurope.desktop
  southasia.desktop
  southeastasia.desktop
  eastasia.desktop
  centralasia.desktop
  middleeast.desktop
  DESTINATION ${KDE3_LOCALE_INSTALL_DIR}/l10n)

# install-data-local: entry.desktop and flag.png of every country
file(GLOB _countries LIST_DIRECTORIES true RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
  CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*")
foreach(_c IN LISTS _countries)
  if(IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/${_c}")
    install(FILES
      ${_c}/entry.desktop
      ${_c}/flag.png
      DESTINATION ${KDE3_LOCALE_INSTALL_DIR}/l10n/${_c})
  endif()
endforeach()
