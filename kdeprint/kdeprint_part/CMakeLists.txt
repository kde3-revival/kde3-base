add_compile_definitions(_KDEPRINT_COMPILE)

kde3_add_module(libkdeprint_part
  SOURCES
    printpart.cpp
  MOC
    printpart.h
  LINK
    KDE3::kdeprint_management
    KDE3::kparts
)

install(FILES
  kdeprint_part.desktop
  DESTINATION ${KDE3_SERVICES_INSTALL_DIR})

install(FILES
  kdeprint_part.rc
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/kdeprint_part)
