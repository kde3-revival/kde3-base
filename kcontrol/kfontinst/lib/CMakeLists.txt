add_compile_definitions(_LARGEFILE64_SOURCE)

# libkfontinst.la (the target name kfontinst is the program)
kde3_add_library(libkfontinst SHARED VERSION_INFO 0:0:0 OUTPUT_NAME kfontinst
  SOURCES
    Misc.cpp
    FcEngine.cpp
  LINK
    KDE3::kdecore
    ${LIBFONTCONFIG_LIBS}
    ${LIBFREETYPE_LIBS}
    KDE3::kio
    ${LIBXFT_LIB}
    X11::X11
)
