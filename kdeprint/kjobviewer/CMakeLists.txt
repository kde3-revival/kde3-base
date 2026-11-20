add_compile_definitions(_KDEPRINT_COMPILE)

kde3_add_kdeinit_executable(kjobviewer
  SOURCES
    kjobviewer.cpp
    main.cpp
  MOC
    kjobviewer.h
  LINK
    KDE3::kdeprint_management
    KDE3::kdeui
)

install(FILES
  kjobviewerui.rc
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/kjobviewer)

install(FILES
  kjobviewer.desktop
  DESTINATION ${KDE3_XDG_APPS_INSTALL_DIR})

kde3_install_icons(kjobviewer)
