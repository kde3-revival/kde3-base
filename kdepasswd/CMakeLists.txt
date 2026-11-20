include_directories(
  "${PROJECT_SOURCE_DIR}/libkonq"
)

kde3_add_executable(kdepasswd
  SOURCES
    kdepasswd.cpp
    passwd.cpp
    passwddlg.cpp
  MOC
    passwddlg.h
  LINK
    KDE3::kio
)

install(FILES
  kdepasswd.desktop
  DESTINATION ${KDE3_XDG_APPS_INSTALL_DIR})

add_subdirectory(kcm)
