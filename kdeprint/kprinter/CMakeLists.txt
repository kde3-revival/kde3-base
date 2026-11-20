add_compile_definitions(_KDEPRINT_COMPILE)

kde3_add_kdeinit_executable(kprinter
  SOURCES
    main.cpp
    printwrapper.cpp
  MOC
    printwrapper.h
  LINK
    KDE3::kdeprint
)
