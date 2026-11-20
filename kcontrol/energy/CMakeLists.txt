kde3_add_module(kcm_energy
  SOURCES
    energy.cpp
  MOC
    energy.h
  LINK
    ${DPMSLIB}
    KDE3::kdeui
    KDE3::kio
)

install(FILES
  energy.desktop
  DESTINATION ${KDE3_APPS_INSTALL_DIR}/.hidden)

add_subdirectory(pics)
