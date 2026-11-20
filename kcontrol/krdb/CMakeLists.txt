kde3_add_library(krdb OBJECT
  SOURCES
    krdb.cpp
  LINK
    KDE3::kdecore
)

install(FILES
  kcmdisplayrc.upd
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/kconf_update)

add_subdirectory(themes)
add_subdirectory(kcs)
add_subdirectory(ad)
add_subdirectory(pics)
