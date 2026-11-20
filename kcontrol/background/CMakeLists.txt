include_directories(
  "${PROJECT_SOURCE_DIR}/libkonq"
)

kde3_add_library(bgnd OBJECT
  SOURCES
    bgrender.cpp
    bgsettings.cpp
  MOC
    bgrender.h
  LINK
    KDE3::kio
    KDE3::knewstuff
)

kde3_add_library(kcmbgnd OBJECT
  SOURCES
    bgdialog.cpp
    bgdialog_ui.ui
    bgwallpaper_ui.ui
    bgwallpaper.cpp
    bgadvanced_ui.ui
    bgadvanced.cpp
    bgmonitor.cpp
  MOC
    bgadvanced.h
    bgdialog.h
    bgmonitor.h
    bgwallpaper.h
  LINK
    KDE3::kio
    KDE3::knewstuff
)

kde3_add_module(kcm_background
  SOURCES
    main.cpp
  MOC
    main.h
  LINK
    kcmbgnd
    bgnd
    KDE3::kio
    KDE3::knewstuff
)

install(FILES
  background.desktop
  DESTINATION ${KDE3_XDG_APPS_INSTALL_DIR})

add_subdirectory(pics)
