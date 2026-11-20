kde3_add_module(dockbar_panelextension
  SOURCES
    dockcontainer.cpp
    dockbarextension.cpp
  MOC
    dockbarextension.h
    dockcontainer.h
  LINK
    KDE3::kdeui
)

install(FILES
  dockbarextension.desktop
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/kicker/extensions)
