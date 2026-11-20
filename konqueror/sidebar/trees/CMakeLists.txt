include_directories(
  "${PROJECT_SOURCE_DIR}/libkonq"
  "${PROJECT_SOURCE_DIR}/konqueror"
  "${CMAKE_CURRENT_SOURCE_DIR}/.."
)

kde3_add_library(konq_sidebar_tree OBJECT
  SOURCES
    konq_sidebartree.cpp
    konq_sidebartreeitem.cpp
    konq_sidebartreetoplevelitem.cpp
  MOC
    konq_sidebartree.h
  LINK
    konq
    konqsidebarplugin
)

kde3_add_module(konqsidebar_tree
  SOURCES
    konqsidebar_tree.cpp
  MOC
    konqsidebar_tree.h
  LINK
    konq
    konqsidebarplugin
    konq_sidebar_tree
)

add_subdirectory(init)
add_subdirectory(dirtree_module)
add_subdirectory(history_module)
add_subdirectory(bookmark_module)
