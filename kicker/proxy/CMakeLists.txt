include_directories(
  "${CMAKE_CURRENT_SOURCE_DIR}/../libkicker"
)

kde3_add_kdeinit_executable(appletproxy
  SOURCES
    appletproxy.cpp
  MOC
    appletproxy.h
  LINK
    KDE3::kdeui
    kickermain
)

kde3_add_kdeinit_executable(extensionproxy
  SOURCES
    extensionproxy.cpp
  MOC
    extensionproxy.h
  LINK
    KDE3::kdeui
    kickermain
)

kde3_add_executable(extensiondebugger
  NO_INSTALL
  SOURCES
    extensiondebugger.cpp
  MOC
    extensiondebugger.h
  LINK
    kickermain
)
