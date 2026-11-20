include_directories(
  "${PROJECT_SOURCE_DIR}/kwin/lib"
)

add_compile_definitions(KCMRULES)

kde3_add_library(kwinrules OBJECT
  SOURCES
    ruleswidget.cpp
    ruleslist.cpp
    kwinsrc.cpp
    detectwidget.cpp
    ruleswidgetbase.ui
    ruleslistbase.ui
    detectwidgetbase.ui
    editshortcutbase.ui
  MOC
    detectwidget.h
    ruleslist.h
    ruleswidget.h
  LINK
    KDE3::kdeui
)

kde3_add_module(kcm_kwinrules
  SOURCES
    kcm.cpp
  MOC
    kcm.h
  LINK
    kwinrules
)

kde3_add_kdeinit_executable(kwin_rules_dialog
  SOURCES
    main.cpp
  LINK
    kwinrules
)

install(FILES
  kwinrules.desktop
  DESTINATION ${KDE3_XDG_APPS_INSTALL_DIR})
