# logind.cpp is ksmserver's: the module asks logind whether sleeping and
# hibernating are possible, to offer them as the default shutdown option
include_directories(
  "${PROJECT_SOURCE_DIR}/ksmserver"
)

kde3_add_module(kcm_smserver
  SOURCES
    kcmsmserver.cpp
    smserverconfigdlg.ui
    smserverconfigimpl.cpp
    ${PROJECT_SOURCE_DIR}/ksmserver/logind.cpp
  MOC
    kcmsmserver.h
    smserverconfigimpl.h
  LINK
    KDE3::kdeui
)

if(HAVE_LOGIND_SLEEP)
  target_link_libraries(kcm_smserver PRIVATE PkgConfig::GIO)
endif()

install(FILES
  kcmsmserver.desktop
  DESTINATION ${KDE3_XDG_APPS_INSTALL_DIR})
