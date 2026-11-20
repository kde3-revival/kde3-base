include_directories(
  "${CMAKE_CURRENT_SOURCE_DIR}/../../backend"
  "${CMAKE_CURRENT_SOURCE_DIR}/.."
  "${CMAKE_CURRENT_BINARY_DIR}/../.."
  "${PROJECT_SOURCE_DIR}/kdmlib"
)

# noinst_LIBRARIES = libkdmthemer.a
kde3_add_library(kdmthemer STATIC
  SOURCES
    kdmthemer.cpp
    kdmitem.cpp
    kdmpixmap.cpp
    kdmrect.cpp
    kdmlabel.cpp
    kdmlayout.cpp
  MOC
    kdmitem.h
    kdmlabel.h
    kdmpixmap.h
    kdmrect.h
    kdmthemer.h
  LINK
    KDE3::kdeui
)
# libkdmthemer_a_COMPILE_FIRST = ../../config.ci
add_dependencies(kdmthemer kdm_config_ci)
