install(FILES
  installfont.desktop
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/konqueror/servicemenus)

add_subdirectory(lib)
if(HAVE_XFT)
  add_subdirectory(thumbnail)
  add_subdirectory(viewpart)
endif()
add_subdirectory(kcmfontinst)
add_subdirectory(kfile-plugin)
add_subdirectory(kfontinst)
add_subdirectory(kio)
