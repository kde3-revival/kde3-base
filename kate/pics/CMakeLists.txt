install(FILES
  sessionchooser.png
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/kate/pics)

# KDE_ICON = AUTO: the icons of this directory (am_edit skipped
# sessionchooser.png with "unknown icon type")
kde3_install_icons(kate kwrite kate2 kwrite2)

add_subdirectory(actions)
