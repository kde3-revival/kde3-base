kde3_add_executable(kstartupx
  SOURCES
    kstartupx.cpp
  LINK
    KDE3Qt::Qt
    X11::X11
)
