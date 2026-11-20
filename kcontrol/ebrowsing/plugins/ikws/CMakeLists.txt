kde3_add_module(libkuriikwsfilter
  SOURCES
    kuriikwsfiltereng.cpp
    kuriikwsfilter.skel
    kuriikwsfilter.cpp
    searchprovider.cpp
  MOC
    kuriikwsfilter.h
  LINK
    KDE3::kio
    KDE3::kdeui
)

kde3_add_module(libkurisearchfilter
  SOURCES
    kurisearchfilter.cpp
    kurisearchfilter.skel
    kuriikwsfiltereng.cpp
    ikwsopts.cpp
    searchproviderdlg.cpp
    searchprovider.cpp
    searchproviderdlg_ui.ui
    ikwsopts_ui.ui
  MOC
    ikwsopts.h
    kurisearchfilter.h
    searchproviderdlg.h
  LINK
    KDE3::kio
    KDE3::kdeui
)

install(FILES
  kuriikwsfilter.upd
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/kconf_update)

install(FILES
  kuriikwsfilter.desktop
  kurisearchfilter.desktop
  DESTINATION ${KDE3_SERVICES_INSTALL_DIR})

install(FILES
  searchprovider.desktop
  DESTINATION ${KDE3_SERVICETYPES_INSTALL_DIR})

add_subdirectory(searchproviders)
