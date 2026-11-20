include_directories(
  "${PROJECT_SOURCE_DIR}/libkonq"
  "${PROJECT_SOURCE_DIR}/kate/lib"
)

kde3_add_library(konqsidebarplugin SHARED VERSION_INFO 3:0:2
  SOURCES
    konqsidebarplugin.cpp
  MOC
    konqsidebarplugin.h
  LINK
    KDE3::kparts
)

kde3_add_module(konq_sidebar
  SOURCES
    konqsidebar.cpp
    sidebar_widget.cpp
  MOC
    konqsidebar.h
    sidebar_widget.h
  LINK
    KDE3::kparts
    KDE3::kio
    konq
    konqsidebarplugin
    KDE3::kutils
)

install(FILES
  konqsidebarplugin.h
  DESTINATION ${KDE3_INCLUDE_INSTALL_DIR})

install(FILES
  konq_sidebartng.desktop
  DESTINATION ${KDE3_SERVICES_INSTALL_DIR})

install(FILES
  konqsidebartng.rc
  DESTINATION ${KDE3_CONFIG_INSTALL_DIR})

install(FILES
  .version
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/konqsidebartng/entries/)

install(FILES
  konqsidebartng.upd
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/kconf_update)

install(PROGRAMS
  move_konqsidebartng_entries.sh
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/kconf_update)

add_subdirectory(trees)
add_subdirectory(web_module)
