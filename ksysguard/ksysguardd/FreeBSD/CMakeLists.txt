add_library(ksysguardd_FreeBSD STATIC
  CPU.c
  Memory.c
  ProcessList.c
  apm.c
  diskstat.c
  loadavg.c
  logfile.c
  netdev.c)
target_include_directories(ksysguardd_FreeBSD PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/../../CContLib"
  "${CMAKE_CURRENT_SOURCE_DIR}/..")
