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

# LIB_* of the optional X libraries: the imported target or nothing
foreach(_x XTEST:XTest:Xtst XCURSOR:Xcursor:Xcursor XRENDER:Xrender:Xrender
           XFIXES:Xfixes:Xfixes XRANDR:Xrandr:Xrandr XSS:Xss:Xss)
  string(REPLACE ":" ";" _x "${_x}")
  list(GET _x 0 _var)
  list(GET _x 1 _found)
  list(GET _x 2 _tgt)
  if(X11_${_found}_FOUND)
    set(LIB_${_var} X11::${_tgt})
  else()
    set(LIB_${_var} "")
  endif()
endforeach()

# ---- paths ---------------------------------------------------------------
# kdm/configure.in.in asked imake (xmkmf) for BINDIR and LIBDIR of X; imake
# is gone from current X.Org, these are its values there.
set(XBINDIR "/usr/bin" CACHE PATH "X binaries directory (imake BINDIR)")
set(XLIBDIR "/usr/lib/X11" CACHE PATH "X libraries directory (imake LIBDIR)")
# KDE_EXPAND_MAKEVAR(KDE_CONFDIR, kde_confdir) etc.
set(KDE_CONFDIR "${KDE3_FULL_CONFIG_INSTALL_DIR}")
set(KDE_DATADIR "${KDE3_FULL_DATA_INSTALL_DIR}")
set(KDE_BINDIR "${KDE3_FULL_BIN_INSTALL_DIR}")

# ---- passwords (KDE_PASSWDLIBS, KDE_PAM, KDE_SHADOWPASSWD) ---------------
# libcrypt (KDE_MISC_TESTS)
set(LIBCRYPT "")
check_library_exists(crypt crypt "" _have_crypt_in_libcrypt)
if(_have_crypt_in_libcrypt)
  set(LIBCRYPT crypt)
  set(HAVE_CRYPT 1)
else()
  check_function_exists(crypt HAVE_CRYPT)
endif()

# --with-pam[=service]: default is PAM if found, service "kde"
set(KDE3_PAM "" CACHE STRING
  "PAM: empty = use it if found, OFF = do not use it, ON or a service name = require it")
set(KDE3_PAM_SERVICE kde)
if(KDE3_PAM AND NOT KDE3_PAM MATCHES "^(ON|YES|TRUE|1|on|yes|true)$")
  set(KDE3_PAM_SERVICE "${KDE3_PAM}")
endif()
set(PAMLIBS "")
set(HAVE_PAM "")
if(NOT KDE3_PAM STREQUAL "" AND NOT KDE3_PAM)
  # --without-pam
else()
  check_library_exists(pam pam_start "" _have_libpam)
  if(_have_libpam)
    check_include_file(security/pam_appl.h _have_security_pam_appl_h)
    if(_have_security_pam_appl_h)
      set(_pam_header security/pam_appl.h)
    else()
      check_include_file(pam/pam_appl.h HAVE_PAM_PAM_APPL_H)
      if(HAVE_PAM_PAM_APPL_H)
        set(_pam_header pam/pam_appl.h)
      endif()
    endif()
  endif()
  if(_pam_header)
    set(HAVE_PAM 1)
    set(PAMLIBS pam ${CMAKE_DL_LIBS})
    # struct pam_message is const in the conversation function on Linux
    check_c_source_compiles("#include <${_pam_header}>
static int conv(int n, const struct pam_message **m, struct pam_response **r, void *d)
{ (void)n; (void)m; (void)r; (void)d; return 0; }
int main(void) { struct pam_conv c = { conv, 0 }; return c.conv == 0; }"
      _pam_message_const)
    if(NOT _pam_message_const)
      set(PAM_MESSAGE_NONCONST 1)
    endif()
  elseif(KDE3_PAM)
    message(FATAL_ERROR "KDE3_PAM was given, but cannot compile with PAM")
  endif()
endif()

# DEF_PAM_SERVICE(kdm, kdm, KDM) etc.: --with-<x>-pam overrides
foreach(_svc KDM KCHECKPASS KSCREENSAVER)
  set(KDE3_${_svc}_PAM_SERVICE "" CACHE STRING
    "PAM service of ${_svc} (default: the one of KDE3_PAM)")
  if(HAVE_PAM)
    if(KDE3_${_svc}_PAM_SERVICE)
      set(${_svc}_PAM_SERVICE "${KDE3_${_svc}_PAM_SERVICE}")
    else()
      set(${_svc}_PAM_SERVICE "${KDE3_PAM_SERVICE}")
    endif()
    message(STATUS "The PAM service used by ${_svc} will be ${${_svc}_PAM_SERVICE}")
  elseif(KDE3_${_svc}_PAM_SERVICE)
    message(FATAL_ERROR "Cannot use KDE3_${_svc}_PAM_SERVICE, as no PAM was detected")
  else()
    set(${_svc}_PAM_SERVICE "")
  endif()
endforeach()

# shadow passwords
set(LIBSHADOW "")
check_library_exists(shadow getspent "" _have_libshadow)
if(_have_libshadow)
  set(LIBSHADOW shadow)
  set(_have_shadow TRUE)
else()
  check_function_exists(getspent _have_shadow)
endif()
option(KDE3_WITH_SHADOW "Shadow password support" ${_have_shadow})
if(KDE3_WITH_SHADOW)
  set(HAVE_SHADOW 1)
else()
  set(LIBSHADOW "")
endif()

if(HAVE_PAM)
  set(PASSWDLIBS ${PAMLIBS})
else()
  set(PASSWDLIBS ${LIBCRYPT} ${LIBSHADOW})
endif()

# ---- kdesu (kdesu/configure.in.in) ---------------------------------------
# the group kdesud is installed setgid to: "nogroup", "nobody" or 65534
set(KDESUD_NOGROUP 65534)
foreach(_grp nogroup nobody)
  execute_process(COMMAND getent group ${_grp} RESULT_VARIABLE _r
    OUTPUT_QUIET ERROR_QUIET)
  if(_r EQUAL 0)
    set(KDESUD_NOGROUP ${_grp})
    break()
  endif()
endforeach()
option(KDE3_KDESU_SUDO_BACKEND "Use sudo instead of su as kdesu backend" OFF)
if(KDE3_KDESU_SUDO_BACKEND)
  set(DEFAULT_SUPER_USER_COMMAND sudo)
else()
  set(DEFAULT_SUPER_USER_COMMAND su)
endif()

# ---- kxkb (kxkb/configure.in.in) -----------------------------------------
# kxkb is not built without XKBstr.h and XKBrules.h (DO_NOT_COMPILE)
cmake_push_check_state(RESET)
set(CMAKE_REQUIRED_INCLUDES ${X11_INCLUDE_DIR})
check_include_files("X11/Xlib.h;X11/extensions/XKBstr.h" _have_xkbstr_h)
check_include_files("stdio.h;X11/Xlib.h;X11/XKBlib.h;X11/extensions/XKBrules.h" _have_xkbrules_h)
cmake_pop_check_state()
if(_have_xkbstr_h AND _have_xkbrules_h AND X11_xkbfile_FOUND)
  set(KDE3_BUILD_KXKB TRUE)
else()
  set(KDE3_BUILD_KXKB FALSE)
  message(STATUS "kxkb is not built: XKB headers or libxkbfile missing")
endif()

# ---- klipper (klipper/configure.in.in) -----------------------------------
if(X11_Xfixes_FOUND)
  set(HAVE_XFIXES 1)
endif()

# ---- ksmserver (ksmserver/configure.in.in) -------------------------------
cmake_push_check_state(RESET)
set(CMAKE_REQUIRED_LIBRARIES ${X11_ICE_LIB})
check_function_exists(_IceTransNoListen HAVE__ICETRANSNOLISTEN)
cmake_pop_check_state()

# ---- khotkeys (khotkeys/configure.in.in) ---------------------------------
include(CheckCXXSourceRuns)
check_cxx_source_runs("
class A { public: virtual A* me(); };
class X { public: int x; virtual void ff() {} };
class B : public X, public A { public: virtual B* me(); };
int foo(A* a) { A* aa = a->me(); return a == aa; }
int main() { B* b = new B; return foo(b) == 0; }
A* A::me() { return this; }
B* B::me() { return this; }" _covariant_return_works)
if(NOT _covariant_return_works)
  set(COVARIANT_RETURN_BROKEN 1)
endif()

# ---- konsole (konsole/configure.in.in) -----------------------------------
# HAVE_OPENPTY is checked above; how to get the cwd of another process
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(HAVE_PROC_CWD 1)
endif()

# ---- kdeprint (kdeprint/kprinter/configure.in.in) ------------------------
check_function_exists(sigset HAVE_SIGSET)
check_function_exists(sigaction HAVE_SIGACTION)

# ---- ksplashml (ksplashml/themeengine/simple/configure.in.in) ------------
option(KDE3_WITH_XINERAMA "Xinerama support in ksplashsimple" ON)
set(LIB_XINERAMA "")
if(KDE3_WITH_XINERAMA AND X11_Xinerama_FOUND)
  set(HAVE_XINERAMA 1)
  set(LIB_XINERAMA X11::Xinerama X11::Xext)
endif()

# ---- kwin (kwin/kompmgr/configure.in.in) ---------------------------------
# --without-composite (configure.in.in) also disables kompmgr
option(KDE3_WITH_COMPOSITE "Xcomposite support (kompmgr, taskmanager)" ON)
set(KDE3_BUILD_KOMPMGR FALSE)
if(KDE3_WITH_COMPOSITE AND X11_Xdamage_FOUND AND X11_Xcomposite_FOUND
   AND X11_Xrender_FOUND AND X11_Xfixes_FOUND)
  set(KDE3_BUILD_KOMPMGR TRUE)
endif()

# ---- ksysguard (ksysguard/configure.in.in, ksysguardd/configure.in.in) ---
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(KSYSGUARDD_UNAME Linux)
elseif(CMAKE_SYSTEM_NAME MATCHES "^(FreeBSD|DragonFly)$")
  set(KSYSGUARDD_UNAME FreeBSD)
elseif(CMAKE_SYSTEM_NAME MATCHES "^(NetBSD|OpenBSD)$")
  set(KSYSGUARDD_UNAME ${CMAKE_SYSTEM_NAME})
else()
  set(KSYSGUARDD_UNAME "")
  message(STATUS "ksysguardd is not built on ${CMAKE_SYSTEM_NAME}")
endif()

# lm_sensors (--with-sensors=check)
set(KDE3_WITH_SENSORS "check" CACHE STRING "lm_sensors support in ksysguardd: check, ON or OFF")
set(LIBSENSORS "")
if(KDE3_WITH_SENSORS)
  check_include_file(sensors/sensors.h HAVE_SENSORS_SENSORS_H)
  check_library_exists(sensors sensors_init "" _have_libsensors)
  if(_have_libsensors)
    set(LIBSENSORS sensors)
  elseif(NOT KDE3_WITH_SENSORS STREQUAL "check")
    message(FATAL_ERROR "KDE3_WITH_SENSORS was given, but test for lm_sensors failed")
  endif()
endif()
if(KSYSGUARDD_UNAME STREQUAL "Linux")
  set(KSYSGUARDD_LIBHOSTS ${LIBSENSORS})
elseif(KSYSGUARDD_UNAME STREQUAL "FreeBSD")
  set(KSYSGUARDD_LIBHOSTS ${LIBSENSORS} kvm)
  if(CMAKE_SYSTEM_NAME STREQUAL "DragonFly")
    list(APPEND KSYSGUARDD_LIBHOSTS kinfo)
  endif()
endif()

# DNS-SD (--disable-dnssd)
option(KDE3_WITH_DNSSD "DNS-SD support in ksysguardd (libdns_sd)" ON)
if(KDE3_WITH_DNSSD)
  find_path(DNSSD_INCLUDE_DIR dns_sd.h)
  find_library(DNSSD_LIBRARY dns_sd)
  mark_as_advanced(DNSSD_INCLUDE_DIR DNSSD_LIBRARY)
  if(DNSSD_INCLUDE_DIR AND DNSSD_LIBRARY)
    cmake_push_check_state(RESET)
    set(CMAKE_REQUIRED_INCLUDES ${DNSSD_INCLUDE_DIR})
    set(CMAKE_REQUIRED_LIBRARIES ${DNSSD_LIBRARY})
    check_c_source_compiles("#include <dns_sd.h>
int main(void) { DNSServiceRefDeallocate((DNSServiceRef) 0);
  TXTRecordDeallocate((TXTRecordRef*) 0); return 0; }" HAVE_DNSSD)
    cmake_pop_check_state()
  endif()
endif()

# ---- kicker (kicker/taskmanager, applets/naughty, extensions/kasbar) -----
set(LIB_XCOMPOSITE "")
if(KDE3_WITH_COMPOSITE AND X11_Xcomposite_FOUND)
  set(HAVE_XCOMPOSITE 1)
  set(LIB_XCOMPOSITE X11::Xcomposite)
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "FreeBSD")
  set(LIB_KVM kvm)
else()
  set(LIB_KVM "")
endif()
check_include_file(sys/loadavg.h HAVE_SYS_LOADAVG_H)
