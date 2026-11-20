add_compile_definitions(QT_NO_CAST_ASCII)

kde3_add_executable(drkonqi
  SOURCES
    krashdcopinterface.skel
    main.cpp
    debugger.cpp
    krashconf.cpp
    drbugreport.cpp
    backtrace.cpp
    toplevel.cpp
  MOC
    backtrace.h
    debugger.h
    drbugreport.h
    krashconf.h
    toplevel.h
  LINK
    KDE3::kdeui
    KDE3::kio
)

add_subdirectory(presets)
add_subdirectory(pics)
add_subdirectory(debuggers)
