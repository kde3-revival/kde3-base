include_directories(
  "${CMAKE_CURRENT_SOURCE_DIR}/../shared"
)

kde3_add_executable(khotkeys_update
  DESTINATION ${KDE3_LIB_INSTALL_DIR}/kconf_update_bin
  SOURCES
    update.cpp
  LINK
    khotkeys_shared
    KDE3::kdecore
)
