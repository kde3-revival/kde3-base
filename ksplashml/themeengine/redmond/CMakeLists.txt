include_directories(
  "${PROJECT_SOURCE_DIR}/ksplashml/themeengine"
)

kde3_add_module(ksplashredmond
  SOURCES
    previewredmond.cpp
    themeredmond.cpp
  MOC
    previewredmond.h
    themeredmond.h
  LINK
    KDE3::kdeui
    KDE3::kio
    ksplashthemes
)

install(FILES
  ksplashredmond.desktop
  DESTINATION ${KDE3_SERVICES_INSTALL_DIR})

install(FILES
  Theme.rc
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/ksplash/Themes/Redmond)

add_subdirectory(pics)
