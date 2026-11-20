add_library(ksysguardd_OpenBSD STATIC
  cpu.c
  memory.c)
# AM_CFLAGS = -Wall
target_compile_options(ksysguardd_OpenBSD PRIVATE -Wall)
target_include_directories(ksysguardd_OpenBSD PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/../../CContLib"
  "${CMAKE_CURRENT_SOURCE_DIR}/..")
