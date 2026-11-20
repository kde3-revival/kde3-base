include_directories(
  "${PROJECT_SOURCE_DIR}/libkonq"
)

kde3_add_module(kcm_useraccount
  SOURCES
    main_widget.ui
    chfnprocess.cpp
    main.cpp
    chfacedlg.cpp
    settings.kcfgc
    pass.kcfgc
  MOC
    chfacedlg.h
    main.h
  LINK
    konq
)

install(FILES
  kcm_useraccount.kcfg
  kcm_useraccount_pass.kcfg
  DESTINATION ${KDE3_KCFG_INSTALL_DIR})

install(FILES
  kcm_useraccount.desktop
  DESTINATION ${KDE3_XDG_APPS_INSTALL_DIR})

add_subdirectory(pics)
