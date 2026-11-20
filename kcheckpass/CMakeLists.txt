kde3_add_executable(kcheckpass
  SOURCES
    kcheckpass.c
    checkpass_etcpasswd.c
    checkpass_pam.c
    checkpass_shadow.c
    checkpass_osfc2passwd.c
    checkpass_aix.c
  LINK
    ${PASSWDLIBS}
)
# $(KDE_USE_FPIE) / $(KDE_USE_PIE): a setuid program
set_target_properties(kcheckpass PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_options(kcheckpass PRIVATE -pie)

kdebase_install_pam_service("${KCHECKPASS_PAM_SERVICE}")
kde3_install_setuid_root(kcheckpass)
