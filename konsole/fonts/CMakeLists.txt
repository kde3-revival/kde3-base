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

# install-data-hook: -mkfontdir $(DESTDIR)$(fontdir)
if(MKFONTDIR_EXECUTABLE)
  kde3_install_time_path(_fontdir "${KDE3_DATA_INSTALL_DIR}/konsole/fonts")
  install(CODE "execute_process(COMMAND \"${MKFONTDIR_EXECUTABLE}\"
    \"\$ENV{DESTDIR}${_fontdir}\")")
endif()
