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
    compositing.cpp
    effects.cpp
    render.cpp
    render_xrender.cpp
    shadow.cpp
    compositor.cpp
    screendesktops.cpp
    screens.cpp
  MOC
    client.h
    compositing.h
    compositor.h
    effects.h
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
    ${LIB_KWIN_GLES}
)
# monitor identities (EDID) for the desktops per monitor
if(X11_Xrandr_FOUND)
  target_link_libraries(kdeinit_kwin PRIVATE X11::Xrandr)
  target_compile_definitions(kdeinit_kwin PRIVATE KWIN_HAVE_XRANDR)
endif()
if(KWIN_HAVE_GLES)
  target_sources(kdeinit_kwin PRIVATE render_gles.cpp)
endif()

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
option(KWIN_COMPOSITOR_TESTS "Build the compositor benchmark (kwin/tests)" OFF)
if(KWIN_COMPOSITOR_TESTS)
  add_subdirectory(tests)
endif()
