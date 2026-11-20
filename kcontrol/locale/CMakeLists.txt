kde3_add_module(kcm_locale
  SOURCES
    main.cpp
    toplevel.cpp
    kcmlocale.cpp
    klocalesample.cpp
    localenum.cpp
    localemon.cpp
    localetime.cpp
    localeother.cpp
  MOC
    kcmlocale.h
    klocalesample.h
    localemon.h
    localenum.h
    localeother.h
    localetime.h
    toplevel.h
  LINK
    KDE3::kio
)

install(FILES
  language.desktop
  DESTINATION ${KDE3_XDG_APPS_INSTALL_DIR})

add_subdirectory(default)
add_subdirectory(pics)
