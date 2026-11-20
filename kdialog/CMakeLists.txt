add_compile_definitions(QT_NO_CAST_ASCII QT_NO_ASCII_CAST)

kde3_add_executable(kdialog
  SOURCES
    kdialog.cpp
    widgets.cpp
    klistboxdialog.cpp
    progressdialog.cpp
    progressdialogiface.skel
  MOC
    klistboxdialog.h
    progressdialog.h
  LINK
    KDE3::kio
)
