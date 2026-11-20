include_directories(
  "${PROJECT_SOURCE_DIR}/libkonq"
)

# dcop_DCOPIDLNG = true: dcop.h has a k_dcop_hidden section
kde3_use_dcopidlng(dcop.h)

kde3_add_kdeinit_executable(keditbookmarks
  SOURCES
    main.cpp
    listview.cpp
    toplevel.cpp
    actionsimpl.cpp
    commands.cpp
    importers.cpp
    dcop.skel
    dcop.cpp
    bookmarkiterator.cpp
    testlink.cpp
    favicons.cpp
    updater.cpp
    exporters.cpp
    ${KDE3_INCLUDE_DIR}/kbookmarknotifier.stub
    bookmarkinfo.cpp
    kebsearchline.cpp
    settings.kcfgc
  MOC
    actionsimpl.h
    bookmarkinfo.h
    bookmarkiterator.h
    dcop.h
    favicons.h
    importers.h
    kebsearchline.h
    listview.h
    testlink.h
    toplevel.h
    updater.h
  LINK
    konq
)

kde3_add_executable(kbookmarkmerger
  SOURCES
    kbookmarkmerger.cpp
  LINK
    KDE3::kio
)

install(FILES
  keditbookmarksui.rc
  keditbookmarks-genui.rc
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/keditbookmarks)

install(FILES
  keditbookmarks.kcfg
  DESTINATION ${KDE3_KCFG_INSTALL_DIR})

kde3_install_icons(AUTO)

# install-data-local: hide the entry of older KDE versions
install(FILES uninstall.desktop
  DESTINATION ${KDE3_APPS_INSTALL_DIR}/Internet RENAME keditbookmarks.desktop)
