include_directories(
  "${CMAKE_CURRENT_SOURCE_DIR}/.."
)

kde3_add_library(ksplashthemes SHARED VERSION_INFO 0:0:0
  SOURCES
    themeengine.cpp
    objkstheme.cpp
  MOC
    objkstheme.h
    themeengine.h
  LINK
    KDE3::kdeui
    ${LIB_XCURSOR}
)

install(FILES
  themeengine.h
  objkstheme.h
  DESTINATION ${KDE3_INCLUDE_INSTALL_DIR}/ksplash)

install(FILES
  ksplashplugins.desktop
  DESTINATION ${KDE3_SERVICETYPES_INSTALL_DIR})

add_subdirectory(default)
add_subdirectory(standard)
add_subdirectory(redmond)
add_subdirectory(simple)
