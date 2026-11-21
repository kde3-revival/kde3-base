# The PCF fonts are made from the BDF sources with bdftopcf, which the
# build needs, like the autotools build did; KDEBASE_KONSOLE_FONTS=OFF
# leaves the fonts out instead (the option is in ConfigureChecks.cmake).
if(NOT KDEBASE_KONSOLE_FONTS)
  return()
endif()
find_program(BDFTOPCF_EXECUTABLE bdftopcf)
find_program(MKFONTDIR_EXECUTABLE mkfontdir)
mark_as_advanced(BDFTOPCF_EXECUTABLE MKFONTDIR_EXECUTABLE)
if(NOT BDFTOPCF_EXECUTABLE)
  message(FATAL_ERROR "bdftopcf (X.Org's font utilities) was not found; it is needed "
    "for Konsole's console fonts.  Install it, or configure with "
    "-DKDEBASE_KONSOLE_FONTS=OFF to build without the fonts.")
endif()

set(_fonts)
foreach(_font console8x16 9x15)
  set(_pcf "${CMAKE_CURRENT_BINARY_DIR}/${_font}.pcf")
  add_custom_command(OUTPUT "${_pcf}.gz"
    COMMAND ${BDFTOPCF_EXECUTABLE} -o "${_pcf}" "${CMAKE_CURRENT_SOURCE_DIR}/${_font}.bdf"
    COMMAND gzip --best -f "${_pcf}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${_font}.bdf"
    COMMENT "Generating ${_font}.pcf.gz"
    VERBATIM)
  list(APPEND _fonts "${_pcf}.gz")
endforeach()
add_custom_target(konsole_fonts ALL DEPENDS ${_fonts})

install(FILES
  ${_fonts}
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/konsole/fonts)

# fontconfig does not search the KDE data dir, so Xft (and so Konsole's
# "Linux" and "Unicode" fonts) did not find the fonts; the autotools
# build left that to an "Install Bitmap Fonts?" dialog copying them to
# fonts:/Personal.  Install a fontconfig file naming the directory, for
# packages (or the administrator) to link into /etc/fonts/conf.d.
if(IS_ABSOLUTE "${KDE3_DATA_INSTALL_DIR}")
  set(KONSOLE_FONT_DIR "${KDE3_DATA_INSTALL_DIR}/konsole/fonts")
else()
  set(KONSOLE_FONT_DIR "${CMAKE_INSTALL_PREFIX}/${KDE3_DATA_INSTALL_DIR}/konsole/fonts")
endif()
configure_file(60-kde3-konsole-fonts.conf.in
  "${CMAKE_CURRENT_BINARY_DIR}/60-kde3-konsole-fonts.conf" @ONLY)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/60-kde3-konsole-fonts.conf"
  DESTINATION ${KDE3_DATA_ROOT_INSTALL_DIR}/fontconfig/conf.avail)

# install-data-hook: -mkfontdir $(DESTDIR)$(fontdir)
if(MKFONTDIR_EXECUTABLE)
  kde3_install_time_path(_fontdir "${KDE3_DATA_INSTALL_DIR}/konsole/fonts")
  install(CODE "execute_process(COMMAND \"${MKFONTDIR_EXECUTABLE}\"
    \"\$ENV{DESTDIR}${_fontdir}\")")
endif()
