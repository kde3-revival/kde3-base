kde3_add_library(filetypes OBJECT
  SOURCES
    filetypesview.cpp
    filetypedetails.cpp
    filegroupdetails.cpp
    kservicelistwidget.cpp
    typeslistitem.cpp
    newtypedlg.cpp
    kserviceselectdlg.cpp
  MOC
    filegroupdetails.h
    filetypedetails.h
    filetypesview.h
    kservicelistwidget.h
    kserviceselectdlg.h
  LINK
    KDE3::kio
)

kde3_add_module(kcm_filetypes
  SOURCES
    control.cpp
  LINK
    filetypes
    KDE3::kio
)

kde3_add_executable(keditfiletype
  SOURCES
    keditfiletype.cpp
  MOC
    keditfiletype.h
  LINK
    filetypes
    KDE3::kio
)

install(FILES
  filetypes.desktop
  DESTINATION ${KDE3_XDG_APPS_INSTALL_DIR})
