include_directories(
  "${CMAKE_CURRENT_SOURCE_DIR}/../lib"
  "${CMAKE_CURRENT_SOURCE_DIR}/../../fonts"
)

# -DOS_$(UNAME) (the kfontinst configure check)
add_compile_definitions(OS_${CMAKE_SYSTEM_NAME})

kde3_add_executable(kfontinst
  SOURCES
    Main.cpp
    FontEngine.cpp
    Fontmap.cpp
    GetPid.c
    XConfig.cpp
  LINK
    kxftconfig
    ${LIBFONTCONFIG_LIBS}
    ${LIBFREETYPE_LIBS}
    ZLIB::ZLIB
    KDE3::kio
    libkfontinst
)
