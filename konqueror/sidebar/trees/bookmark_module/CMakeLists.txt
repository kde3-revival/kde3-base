include_directories(
  "${PROJECT_SOURCE_DIR}/libkonq"
  "${CMAKE_CURRENT_SOURCE_DIR}/.."
  "${CMAKE_CURRENT_SOURCE_DIR}/../.."
)

kde3_add_module(konq_sidebartree_bookmarks
  SOURCES
    bookmark_module.cpp
    bookmark_item.cpp
  MOC
    bookmark_module.h
  LINK
    konqsidebarplugin
    konq_sidebar_tree
)
