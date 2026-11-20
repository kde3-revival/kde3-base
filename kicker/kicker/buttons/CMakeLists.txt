include_directories(
  "${CMAKE_CURRENT_SOURCE_DIR}/../core"
  "${CMAKE_CURRENT_SOURCE_DIR}/../../libkicker"
  "${CMAKE_CURRENT_BINARY_DIR}/../../libkicker"
  "${CMAKE_CURRENT_SOURCE_DIR}/../ui"
  "${PROJECT_SOURCE_DIR}/libkonq"
)

kde3_add_library(kicker_buttons OBJECT
  SOURCES
    servicebutton.cpp
    bookmarksbutton.cpp
    browserbutton.cpp
    desktopbutton.cpp
    extensionbutton.cpp
    kbutton.cpp
    nonkdeappbutton.cpp
    servicemenubutton.cpp
    urlbutton.cpp
    windowlistbutton.cpp
  MOC
    bookmarksbutton.h
    browserbutton.h
    desktopbutton.h
    extensionbutton.h
    kbutton.h
    nonkdeappbutton.h
    servicebutton.h
    servicemenubutton.h
    urlbutton.h
    windowlistbutton.h
  LINK
    konq
    KDE3::kdeui
    ${LIB_XTEST}
)

install(FILES
  bookmarks.desktop
  browser.desktop
  desktop.desktop
  exec.desktop
  kmenu.desktop
  windowlist.desktop
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/kicker/builtins)
