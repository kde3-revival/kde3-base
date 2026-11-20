kde3_add_module(kcm_keyboard
  SOURCES
    extension.cpp
    x11helper.cpp
    rules.cpp
    kxkbconfig.cpp
    pixmap.cpp
    kcmlayout.cpp
    kcmlayoutwidget.ui
    kcmmisc.cpp
    kcmmiscwidget.ui
  MOC
    kcmlayout.h
    kcmmisc.h
  LINK
    X11::xkbfile
    ${LIB_XTEST}
    KDE3::kio
)

kde3_add_kdeinit_executable(kxkb
  SOURCES
    extension.cpp
    x11helper.cpp
    rules.cpp
    kxkbconfig.cpp
    pixmap.cpp
    layoutmap.cpp
    kxkb.cpp
    kxkbtraywindow.cpp
    kxkb.skel
  MOC
    kxkb.h
    kxkbtraywindow.h
  LINK
    X11::xkbfile
    X11::X11
    X11::Xext
    KDE3::kdeui
)

install(FILES
  keyboard.desktop
  keyboard_layout.desktop
  DESTINATION ${KDE3_XDG_APPS_INSTALL_DIR})

install(FILES
  kxkb.desktop
  DESTINATION ${KDE3_SERVICES_INSTALL_DIR})

# install-data-local
install(FILES
  kxkb_groups
  DESTINATION ${KDE3_CONFIG_INSTALL_DIR})

add_subdirectory(pics)
