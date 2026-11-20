add_compile_definitions(QT_NO_CAST_ASCII QT_CLEAN_NAMESPACE QT_NO_COMPAT)

kde3_add_executable(kstart
  SOURCES
    kstart.cpp
  MOC
    kstart.h
  LINK
    KDE3::kdecore
)
