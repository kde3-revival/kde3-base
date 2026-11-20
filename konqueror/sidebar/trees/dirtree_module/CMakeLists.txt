include_directories(
  "${PROJECT_SOURCE_DIR}/libkonq"
  "${CMAKE_CURRENT_SOURCE_DIR}/.."
  "${CMAKE_CURRENT_SOURCE_DIR}/../.."
)

kde3_add_module(konq_sidebartree_dirtree
  SOURCES
    dirtree_module.cpp
    dirtree_item.cpp
  MOC
    dirtree_module.h
  LINK
    konqsidebarplugin
    konq_sidebar_tree
)
