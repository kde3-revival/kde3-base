include_directories(
  "${CMAKE_CURRENT_SOURCE_DIR}/.."
  "${PROJECT_SOURCE_DIR}/libkonq"
)

kde3_add_module(konq_aboutpage
  SOURCES
    konq_aboutpage.cc
  MOC
    konq_aboutpage.h
  LINK
    KDE3::khtml
)

install(FILES
  konq_aboutpage.desktop
  DESTINATION ${KDE3_SERVICES_INSTALL_DIR})

install(FILES
  konqaboutpage.desktop
  DESTINATION ${KDE3_SERVICETYPES_INSTALL_DIR})

install(FILES
  box-centre-konqueror.png
  top-right-konqueror.png
  lightning.png
  launch.html
  intro.html
  specs.html
  tips.html
  plugins.html
  plugins_rtl.html
  konq.css
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/konqueror/about)
