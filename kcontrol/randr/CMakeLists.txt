kde3_add_library(randrinternal OBJECT
  SOURCES
    ktimerdialog.cpp
    randr.cpp
  MOC
    ktimerdialog.h
    randr.h
  LINK
    KDE3::kdeui
    KDE3::kio
    KDE3::kutils
)

kde3_add_module(kcm_randr
  SOURCES
    krandrmodule.cpp
  MOC
    krandrmodule.h
  LINK
    randrinternal
    KDE3::kdeui
    ${LIB_XRANDR}
)

kde3_add_executable(krandrtray
  SOURCES
    main.cpp
    krandrtray.cpp
    krandrapp.cpp
    krandrpassivepopup.cpp
  MOC
    krandrapp.h
    krandrpassivepopup.h
    krandrtray.h
  LINK
    randrinternal
    KDE3::kio
    KDE3::kutils
    ${LIB_XRANDR}
)

install(FILES
  krandrtray.desktop
  DESTINATION ${KDE3_XDG_APPS_INSTALL_DIR})

install(FILES
  randr.desktop
  DESTINATION ${KDE3_APPS_INSTALL_DIR}/.hidden)
