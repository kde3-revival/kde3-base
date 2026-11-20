install(FILES
  applets.desktop
  contact.desktop
  faq.desktop
  info.desktop
  links.desktop
  support.desktop
  userguide.desktop
  welcome.desktop
  kicmodules.desktop
  kcontrolmodules.desktop
  konqplugins.desktop
  kioslaves.desktop
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/khelpcenter/plugins)

add_subdirectory(Applications)
add_subdirectory(Manpages)
add_subdirectory(Tutorials)
add_subdirectory(Scrollkeeper)
