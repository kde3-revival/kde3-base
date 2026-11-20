include_directories(
  "${CMAKE_CURRENT_SOURCE_DIR}/.."
  "${CMAKE_CURRENT_BINARY_DIR}/.."
  "${PROJECT_SOURCE_DIR}/kio"
  "${PROJECT_SOURCE_DIR}"
)

kde3_add_module(kcm_componentchooser
  SOURCES
    componentchooser_ui.ui
    componentconfig_ui.ui
    emailclientconfig_ui.ui
    terminalemulatorconfig_ui.ui
    browserconfig_ui.ui
    componentchooser.cpp
    kcm_componentchooser.cpp
  MOC
    componentchooser.h
    kcm_componentchooser.h
  LINK
    KDE3::kio
)

install(FILES
  componentchooser.desktop
  DESTINATION ${KDE3_XDG_APPS_INSTALL_DIR})

add_subdirectory(componentservices)
