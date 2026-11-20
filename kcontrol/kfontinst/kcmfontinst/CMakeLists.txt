include_directories(
  "${CMAKE_CURRENT_SOURCE_DIR}/../lib"
  "${CMAKE_CURRENT_SOURCE_DIR}/../../fonts"
)

kde3_add_module(kcm_fontinst
  SOURCES
    KCmFontInst.cpp
    KFileFontIconView.cpp
    KFileFontView.cpp
    PrintDialog.cpp
    SettingsDialog.cpp
  MOC
    KCmFontInst.h
    KFileFontView.h
  LINK
    KDE3::kio
    ${LIBFREETYPE_LIBS}
    ${LIBFONTCONFIG_LIBS}
    libkfontinst
)

install(FILES
  kcmfontinst.desktop
  DESTINATION ${KDE3_XDG_APPS_INSTALL_DIR})

kde3_install_icons(AUTO)

# include_fontinst_tn: FONTINST_PRINT_INC, FONTINST_PRINT_LIB
if(HAVE_XFT)
  target_include_directories(kcm_fontinst PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../viewpart")
  target_link_libraries(kcm_fontinst PRIVATE kfontinstprint)
endif()
