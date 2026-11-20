include_directories(
  "${CMAKE_CURRENT_SOURCE_DIR}/../../lib"
)

kde3_add_module(kwin3_default
  SOURCES
    kdedefault.cpp
  LINK
    KDE3::kdecore
    kdecorations
)

add_subdirectory(config)
