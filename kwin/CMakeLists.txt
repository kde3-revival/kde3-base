include_directories(
  "${CMAKE_CURRENT_SOURCE_DIR}/lib"
)

kde3_add_kdeinit_executable(kwin
  SOURCES
    workspace.cpp
    client.cpp
    placement.cpp
    atoms.cpp
    utils.cpp
    layers.cpp
    main.cpp
    popupinfo.cpp
    tabbox.cpp
    options.cpp
    plugins.cpp
    events.cpp
    KWinInterface.skel
    killwindow.cpp
    geometrytip.cpp
    sm.cpp
    group.cpp
    bridge.cpp
    manage.cpp
    notifications.cpp
    activation.cpp
    useractions.cpp
    geometry.cpp
    rules.cpp
    overview.cpp
  MOC
    client.h
    geometrytip.h
    main.h
    overview.h
    popupinfo.h
    sm.h
    tabbox.h
    utils.h
    workspace.h
  LINK
    KDE3::kdeui
    kdecorations
    ${LIB_KWIN_OVERVIEW}
)

install(FILES
  KWinInterface.h
  DESTINATION ${KDE3_INCLUDE_INSTALL_DIR})

install(FILES
  eventsrc
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/kwin)

install(FILES
  kwin.kcfg
  DESTINATION ${KDE3_KCFG_INSTALL_DIR})

kde3_install_icons(kwin)

add_subdirectory(lib)
add_subdirectory(killer)
add_subdirectory(kcmkwin)
add_subdirectory(pics)
add_subdirectory(clients)
add_subdirectory(oldheaders)
add_subdirectory(data)
if(KDE3_BUILD_KOMPMGR)
  add_subdirectory(kompmgr)
endif()
