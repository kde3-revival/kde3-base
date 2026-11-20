include_directories(
  "${PROJECT_SOURCE_DIR}/libkonq"
  "${PROJECT_SOURCE_DIR}/konqueror"
)

kde3_add_module(konq_iconview
  SOURCES
    konq_iconview.cc
  MOC
    konq_iconview.h
  LINK
    konq
)

install(FILES
  konq_iconview.desktop
  konq_multicolumnview.desktop
  DESTINATION ${KDE3_SERVICES_INSTALL_DIR})

install(FILES
  konq_iconview.rc
  konq_multicolumnview.rc
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/konqiconview)
