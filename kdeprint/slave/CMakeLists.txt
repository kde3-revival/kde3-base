add_compile_definitions(_KDEPRINT_COMPILE)

kde3_add_module(kio_print
  SOURCES
    kio_print.cpp
  MOC
    kio_print.h
  LINK
    KDE3::kio
    KDE3::kdeprint
)

install(FILES
  print.protocol
  printdb.protocol
  DESTINATION ${KDE3_SERVICES_INSTALL_DIR})

add_subdirectory(mimetypes)
add_subdirectory(templates)
