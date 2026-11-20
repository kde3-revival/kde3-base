# Configure checks for config.h, following configure.in.in, the
# subdirectories' configure.in.in and admin/acinclude.m4.in.  Only what the
# sources actually test is checked.

include(CheckIncludeFile)
include(CheckIncludeFiles)
include(CheckSymbolExists)
include(CheckFunctionExists)
include(CheckLibraryExists)
include(CheckStructHasMember)
include(CheckTypeSize)
include(CheckCSourceCompiles)
include(CheckCXXSourceCompiles)
include(CMakePushCheckState)

find_package(PkgConfig REQUIRED)

# ---- headers (configure.in.in) -------------------------------------------
foreach(_h
    crypt.h lastlog.h libutil.h limits.h mntent.h paths.h pty.h string.h
    sys/ioctl.h sys/loadavg.h sys/select.h sys/socket.h sys/sockio.h
    sys/stat.h sys/time.h sys/types.h sys/wait.h termio.h termios.h
    unistd.h util.h fstab.h)
  string(TOUPPER "HAVE_${_h}" _var)
  string(REGEX REPLACE "[/.]" "_" _var "${_var}")
  check_include_file("${_h}" ${_var})
endforeach()

# AC_HEADER_TIME
check_c_source_compiles("
#include <sys/types.h>
#include <sys/time.h>
#include <time.h>
int main(void) { struct tm *tp = 0; return tp != 0; }" TIME_WITH_SYS_TIME)

# ---- functions (configure.in.in) -----------------------------------------
foreach(_f
    nice initgroups grantpt getpt getpassphrase getpeereid getdomainname
    setpriority mkstemp)
  string(TOUPPER "HAVE_${_f}" _var)
  check_function_exists(${_f} ${_var})
endforeach()
# pw_encrypt (configure.in.in), with libcrypt (KDE_PASSWDLIBS) when needed
check_function_exists(pw_encrypt HAVE_PW_ENCRYPT)

check_cxx_source_compiles("#include <sys/socket.h>
int main() { struct ucred cred; (void)cred; return 0; }" HAVE_STRUCT_UCRED)

# openpty (kdesu, fish: -lutil)
check_function_exists(openpty HAVE_OPENPTY)
set(KDE3_LIBUTIL "")
if(NOT HAVE_OPENPTY)
  check_library_exists(util openpty "" HAVE_OPENPTY_IN_LIBUTIL)
  if(HAVE_OPENPTY_IN_LIBUTIL)
    set(HAVE_OPENPTY 1)
    set(KDE3_LIBUTIL util)
  endif()
endif()

# res_init (LIBRESOLV)
set(LIBRESOLV "")
check_function_exists(res_init _have_res_init)
if(NOT _have_res_init)
  check_library_exists(resolv __res_init "" _have_res_init_in_resolv)
  if(_have_res_init_in_resolv)
    set(LIBRESOLV resolv)
  endif()
endif()

# ---- X11 extensions (configure.in.in) ------------------------------------
find_package(X11 REQUIRED)
if(X11_XTest_FOUND)
  set(HAVE_XTEST 1)
endif()
cmake_push_check_state(RESET)
set(CMAKE_REQUIRED_INCLUDES ${X11_INCLUDE_DIR})
set(CMAKE_REQUIRED_LIBRARIES ${X11_X11_LIB})
check_include_files("X11/Xlib.h;X11/XKBlib.h" _have_xkblib_h)
if(_have_xkblib_h)
  check_function_exists(XkbLockModifiers HAVE_XKB)
  check_function_exists(XkbSetPerClientControls HAVE_XKBSETPERCLIENTCONTROLS)
endif()
cmake_pop_check_state()
if(X11_Xcursor_FOUND)
  set(HAVE_XCURSOR 1)
endif()
if(X11_Xrender_FOUND)
  set(HAVE_XRENDER 1)
endif()

# ---- paths ---------------------------------------------------------------
# KDE_EXPAND_MAKEVAR(KDE_CONFDIR, kde_confdir) etc.
set(KDE_CONFDIR "${KDE3_FULL_CONFIG_INSTALL_DIR}")
set(KDE_DATADIR "${KDE3_FULL_DATA_INSTALL_DIR}")
set(KDE_BINDIR "${KDE3_FULL_BIN_INSTALL_DIR}")
