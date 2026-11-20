include_directories(
  "${PROJECT_SOURCE_DIR}/libkonq"
  "${PROJECT_SOURCE_DIR}/kcontrol/input"
)

kde3_add_module(kcm_konqhtml
  SOURCES
    advancedTabDialog.cpp
    htmlopts.cpp
    jsopts.cpp
    appearance.cpp
    khttpoptdlg.cpp
    policydlg.cpp
    main.cpp
    jspolicies.cpp
    policies.cpp
    domainlistview.cpp
    advancedTabOptions.ui
    filteropts.cpp
  MOC
    advancedTabDialog.h
    appearance.h
    domainlistview.h
    filteropts.h
    htmlopts.h
    jsopts.h
    jspolicies.h
    khttpoptdlg.h
    main.h
    policydlg.h
  LINK
    KDE3::khtml
    KDE3::kdeui
)

install(FILES
  khtml_behavior.desktop
  khtml_java_js.desktop
  khtml_fonts.desktop
  khtml_filter.desktop
  DESTINATION ${KDE3_XDG_APPS_INSTALL_DIR})

# install-data-local: hide the module of older KDE versions
install(FILES uninstall.desktop
  DESTINATION ${KDE3_APPS_INSTALL_DIR}/Settings/WebBrowsing RENAME nsplugin.desktop)
install(FILES uninstall.desktop
  DESTINATION ${KDE3_APPS_INSTALL_DIR}/Settings/WebBrowsing RENAME khtml_appearance.desktop)
