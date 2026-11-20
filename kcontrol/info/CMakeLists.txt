# COMPILE_GL_INFO
if(HAVE_GL)
  add_compile_definitions(INFO_OPENGL_AVAILABLE)
endif()

kde3_add_module(kcm_info
  SOURCES
    main.cpp
    memory.cpp
    opengl.cpp
  MOC
    memory.h
  LINK
    KDE3::kdeui
    ${LIBDEVINFO}
    ${GLLIB}
)

install(FILES
  memory.desktop
  processor.desktop
  dma.desktop
  interrupts.desktop
  ioports.desktop
  opengl.desktop
  pci.desktop
  sound.desktop
  devices.desktop
  scsi.desktop
  partitions.desktop
  xserver.desktop
  cdinfo.desktop
  DESTINATION ${KDE3_XDG_APPS_INSTALL_DIR})
