kde3_add_module(kcm_kio
  SOURCES
    policydlg.cpp
    kcookiesmain.cpp
    kcookiespolicies.cpp
    main.cpp
    smbrodlg.cpp
    useragentdlg.cpp
    kproxydlgbase.cpp
    kmanualproxydlg.cpp
    kenvvarproxydlg.cpp
    kproxydlg.cpp
    kcookiesmanagement.cpp
    fakeuaprovider.cpp
    uagentproviderdlg.cpp
    netpref.cpp
    cache.cpp
    ksaveioconfig.cpp
    socks.cpp
    socksbase.ui
    kproxydlg_ui.ui
    kcookiesmanagementdlg_ui.ui
    kcookiespoliciesdlg_ui.ui
    policydlg_ui.ui
    useragentdlg_ui.ui
    uagentproviderdlg_ui.ui
    manualproxy_ui.ui
    envvarproxy_ui.ui
    cache_ui.ui
  MOC
    cache.h
    kcookiesmain.h
    kcookiesmanagement.h
    kcookiespolicies.h
    kenvvarproxydlg.h
    kmanualproxydlg.h
    kproxydlg.h
    main.h
    netpref.h
    policydlg.h
    smbrodlg.h
    socks.h
    uagentproviderdlg.h
    useragentdlg.h
  LINK
    KDE3::kio
    KDE3::kutils
)

install(FILES
  socks.upd
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/kconf_update)

install(FILES
  uasprovider.desktop
  DESTINATION ${KDE3_SERVICETYPES_INSTALL_DIR})

install(FILES
  cookies.desktop
  useragent.desktop
  cache.desktop
  lanbrowser.desktop
  netpref.desktop
  proxy.desktop
  DESTINATION ${KDE3_XDG_APPS_INSTALL_DIR})

install(FILES
  smb.desktop
  DESTINATION ${KDE3_APPS_INSTALL_DIR}/.hidden)

add_subdirectory(uasproviders)

# install-data-local: hide the module of older KDE versions
install(FILES uninstall.desktop
  DESTINATION ${KDE3_APPS_INSTALL_DIR}/Settings/WebBrowsing RENAME smb.desktop)
