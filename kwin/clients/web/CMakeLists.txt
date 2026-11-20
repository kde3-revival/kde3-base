include_directories(
  "${PROJECT_SOURCE_DIR}"
)

kde3_add_module(kwin3_web
  SOURCES
    Web.cpp
    WebButton.cpp
  MOC
    Web.h
  LINK
    KDE3::kdeui
    kdecorations
)

install(FILES
  web.desktop
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/kwin/)
