kde3_add_executable(kdesud
  SOURCES
    kdesud.cpp
    repo.cpp
    lexer.cpp
    handler.cpp
    secure.cpp
  LINK
    KDE3::kdecore
    KDE3::kdesu
)
# $(KDE_USE_FPIE) / $(KDE_USE_PIE): a setgid program
set_target_properties(kdesud PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_options(kdesud PRIVATE -pie)

install(CODE "
  message(STATUS \"For security reasons, kdesud is installed setgid ${KDESUD_NOGROUP}.\")
  message(STATUS \"Kdesud is the daemon that implements the password caching.\")
  message(STATUS \"You should NOT use the password caching feature if kdesud is\")
  message(STATUS \"not installed setgid ${KDESUD_NOGROUP}.\")")
kdebase_install_setgid(kdesud "${KDESUD_NOGROUP}"
  "The password caching feature is disabled.")
