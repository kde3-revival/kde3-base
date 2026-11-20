include_directories(
  "${CMAKE_CURRENT_SOURCE_DIR}/../libkicker"
)

kde3_add_library(taskmanager SHARED VERSION_INFO 1:0:0
  SOURCES
    tasklmbmenu.cpp
    taskrmbmenu.cpp
    taskmanager.cpp
  MOC
    tasklmbmenu.h
    taskmanager.h
    taskrmbmenu.h
  LINK
    KDE3::kdecore
    ${LIB_XFIXES}
    ${LIB_XRENDER}
    ${LIB_XCOMPOSITE}
    kickermain
)
