include_directories(
  "${PROJECT_SOURCE_DIR}/kate/utils"
)

kde3_add_library(kateutils SHARED VERSION_INFO 0:0:0
  SOURCES
    dockviewbase.cpp
    messageview.cpp
    listboxview.cpp
  MOC
    dockviewbase.h
    listboxview.h
    messageview.h
  LINK
    KDE3::kdeui
    KDE3::kdecore
    KDE3::kio
)

install(FILES
  dockviewbase.h
  messageview.h
  listboxview.h
  DESTINATION ${KDE3_INCLUDE_INSTALL_DIR}/kate/utils)
