# fishcode.h: fish.pl as a C string (fish.lo: fishcode.h)
add_custom_command(OUTPUT fishcode.h
  COMMAND ${CMAKE_COMMAND} "-DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/fish.pl"
          "-DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/fishcode.h"
          -P "${CMAKE_CURRENT_SOURCE_DIR}/fishcode.cmake"
  DEPENDS fish.pl fishcode.cmake
  COMMENT "Generating fishcode.h"
  VERBATIM)

kde3_add_module(kio_fish
  SOURCES
    fish.cpp
    "${CMAKE_CURRENT_BINARY_DIR}/fishcode.h"
  LINK
    KDE3::kio
)

install(FILES
  fish.protocol
  nxfish.protocol
  DESTINATION ${KDE3_SERVICES_INSTALL_DIR})
