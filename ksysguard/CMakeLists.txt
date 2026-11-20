kde3_install_icons(ksysguard)

add_subdirectory(CContLib)
if(KSYSGUARDD_UNAME)
  add_subdirectory(ksysguardd)
endif()
add_subdirectory(gui)
add_subdirectory(pics)
add_subdirectory(example)
