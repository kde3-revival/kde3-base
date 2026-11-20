add_compile_definitions(QT_NO_CAST_ASCII)

kde3_add_executable(kreadconfig
  SOURCES
    kreadconfig.cpp
  LINK
    KDE3::kdecore
)

kde3_add_executable(kwriteconfig
  SOURCES
    kwriteconfig.cpp
  LINK
    KDE3::kdecore
)
