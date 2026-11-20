include_directories(
  "${CMAKE_CURRENT_SOURCE_DIR}/../lib"
  "${CMAKE_CURRENT_SOURCE_DIR}/../../fonts"
)

kde3_add_module(fontthumbnail
  SOURCES
    FontThumbnail.cpp
  LINK
    ${LIBFREETYPE_LIBS}
    KDE3::kdecore
    KDE3::kio
    libkfontinst
)

install(FILES
  fontthumbnail.desktop
  DESTINATION ${KDE3_SERVICES_INSTALL_DIR})
