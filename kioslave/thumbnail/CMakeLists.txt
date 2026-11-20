kde3_add_module(kio_thumbnail
  SOURCES
    thumbnail.cpp
  LINK
    KDE3::kio
)

kde3_add_module(imagethumbnail
  SOURCES
    imagecreator.cpp
  LINK
    KDE3::kio
)

kde3_add_module(textthumbnail
  SOURCES
    textcreator.cpp
  LINK
    KDE3::kio
)

kde3_add_module(htmlthumbnail
  SOURCES
    htmlcreator.cpp
  MOC
    htmlcreator.h
  LINK
    KDE3::khtml
)

kde3_add_module(djvuthumbnail
  SOURCES
    djvucreator.cpp
  LINK
    KDE3::kdecore
)

# have_xcursor: CURSOR_MODULE, CURSOR_DATAFILE
if(HAVE_XCURSOR)
kde3_add_module(cursorthumbnail
  SOURCES
    cursorcreator.cpp
  LINK
    KDE3::kdecore
    ${LIB_XCURSOR}
)

install(FILES
  cursorthumbnail.desktop
  DESTINATION ${KDE3_SERVICES_INSTALL_DIR})
endif()

# include_EXR_MODULES: EXR_MODULE, EXR_DATAFILE
if(LIB_EXR)
kde3_add_module(exrthumbnail
  SOURCES
    exrcreator.cpp
  LINK
    KDE3::kdecore
    ${LIB_EXR}
)
install(FILES
  exrthumbnail.desktop
  DESTINATION ${KDE3_SERVICES_INSTALL_DIR})
endif()

install(FILES
  thumbnail.protocol
  DESTINATION ${KDE3_SERVICES_INSTALL_DIR})

install(FILES
  thumbcreator.desktop
  DESTINATION ${KDE3_SERVICETYPES_INSTALL_DIR})

install(FILES
  imagethumbnail.desktop
  textthumbnail.desktop
  htmlthumbnail.desktop
  djvuthumbnail.desktop
  DESTINATION ${KDE3_SERVICES_INSTALL_DIR})
