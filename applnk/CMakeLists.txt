install(FILES
  kde-settings.menu
  kde-information.menu
  kde-screensavers.menu
  DESTINATION ${KDE3_XDG_MENU_INSTALL_DIR})

install(FILES
  kde-essential.menu
  DESTINATION ${KDE3_XDG_MENU_INSTALL_DIR}/applications-merged)

install(FILES
  kde-main.directory
  kde-development.directory
  kde-editors.directory
  kde-edutainment.directory
  kde-games-arcade.directory
  kde-games-board.directory
  kde-games-card.directory
  kde-games-kids.directory
  kde-games-roguelikes.directory
  kde-games-strategy.directory
  kde-games.directory
  kde-graphics.directory
  kde-information.directory
  kde-internet-terminal.directory
  kde-internet.directory
  kde-more.directory
  kde-multimedia.directory
  kde-office.directory
  kde-science.directory
  kde-settings-accessibility.directory
  kde-settings-components.directory
  kde-settings-desktop.directory
  kde-settings-looknfeel.directory
  kde-settings-network.directory
  kde-settings-peripherals.directory
  kde-settings-hardware.directory
  kde-settings-power.directory
  kde-settings-security.directory
  kde-settings-sound.directory
  kde-settings-system.directory
  kde-settings-webbrowsing.directory
  kde-settings.directory
  kde-settingsmenu.directory
  kde-system-screensavers.directory
  kde-system-terminal.directory
  kde-system.directory
  kde-toys.directory
  kde-unknown.directory
  kde-utilities-xutils.directory
  kde-utilities.directory
  kde-utilities-accessibility.directory
  kde-utilities-desktop.directory
  kde-utilities-peripherals.directory
  kde-utilities-file.directory
  kde-utilities-pim.directory
  kde-development-translation.directory
  kde-development-webdevelopment.directory
  DESTINATION ${KDE3_XDG_DIRECTORY_INSTALL_DIR})

add_subdirectory(compat)

# install-data-local: remove the .desktop files of older KDE versions
file(STRINGS old_desktop_files _old_desktop_files)
kdebase_install_time_path(_xdgappsdir "${KDE3_XDG_APPS_INSTALL_DIR}")
install(CODE "
  foreach(_f ${_old_desktop_files})
    set(_p \"\$ENV{DESTDIR}${_xdgappsdir}/\${_f}\")
    if(EXISTS \"\${_p}\")
      file(REMOVE \"\${_p}\")
      message(STATUS \"Deleting \${_p}\")
    endif()
  endforeach()")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS old_desktop_files)

install(FILES hidden.directory
  DESTINATION ${KDE3_APPS_INSTALL_DIR}/.hidden RENAME .directory)
