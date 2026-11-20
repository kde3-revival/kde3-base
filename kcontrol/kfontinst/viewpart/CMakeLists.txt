include_directories(
  "${CMAKE_CURRENT_SOURCE_DIR}/../lib"
  "${CMAKE_CURRENT_SOURCE_DIR}/../../fonts"
)

kde3_add_library(kfontinstprint OBJECT
  SOURCES
    KfiPrint.cpp
  LINK
    KDE3::kdeprint
    libkfontinst
)

kde3_add_module(libkfontviewpart
  SOURCES
    FontViewPart.cpp
    FontViewPartFactory.cpp
    FontPreview.cpp
  MOC
    FontPreview.h
    FontViewPart.h
    FontViewPartFactory.h
  LINK
    KDE3::kparts
    kfontinstprint
    libkfontinst
)

kde3_add_executable(kfontview
  SOURCES
    FontViewerApp.cpp
  MOC
    FontViewerApp.h
  LINK
    KDE3::kparts
)

install(FILES
  kfontviewpart.desktop
  DESTINATION ${KDE3_SERVICES_INSTALL_DIR})

install(FILES
  kfontview.desktop
  DESTINATION ${KDE3_XDG_APPS_INSTALL_DIR})

install(FILES
  kfontviewpart.rc
  kfontviewui.rc
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/kfontview)
