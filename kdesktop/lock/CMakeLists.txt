include_directories(
  "${CMAKE_CURRENT_SOURCE_DIR}/.."
  "${CMAKE_CURRENT_BINARY_DIR}/.."
  "${PROJECT_SOURCE_DIR}/kcheckpass"
  "${PROJECT_SOURCE_DIR}/kdmlib"
)

kde3_add_executable(kdesktop_lock
  SOURCES
    lockprocess.cc
    lockdlg.cc
    autologout.cc
    main.cc
  MOC
    autologout.h
    lockdlg.h
    lockprocess.h
    main.h
  LINK
    kdesktopsettings
    dmctl
    KDE3::kio
    ${LIB_XF86MISC}
    ${GLLIB}
)

kdebase_install_pam_service("${KSCREENSAVER_PAM_SERVICE}")
