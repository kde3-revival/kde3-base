add_library(ksysguardd_Linux STATIC
  ProcessList.c
  Memory.c
  stat.c
  netdev.c
  apm.c
  acpi.c
  loadavg.c
  cpuinfo.c
  lmsensors.c
  netstat.c
  diskstat.c
  logfile.c
  i8k.c)
target_compile_definitions(ksysguardd_Linux PRIVATE _GNU_SOURCE)
# AM_CFLAGS = -Wall
target_compile_options(ksysguardd_Linux PRIVATE -Wall)
target_include_directories(ksysguardd_Linux PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/../../CContLib"
  "${CMAKE_CURRENT_SOURCE_DIR}/..")
# supports_i8k: /proc/i8k exists on the build machine (Dell laptops)
if(EXISTS /proc/i8k)
  target_compile_definitions(ksysguardd_Linux PRIVATE HAVE_I8K_SUPPORT)
endif()
