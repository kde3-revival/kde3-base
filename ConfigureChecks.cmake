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

# ---- kdesktop (kdesktop/configure.in.in, kdesktop/lock/configure.in.in) --
# XScreenSaver (--without-xscreensaver)
option(KDE3_WITH_XSCREENSAVER "XScreenSaver extension support" ON)
set(LIB_XSS "")
if(KDE3_WITH_XSCREENSAVER AND X11_Xss_FOUND)
  set(HAVE_XSCREENSAVER 1)
  set(LIB_XSS X11::Xss)
endif()

# XF86Misc (gone from current X.Org)
set(LIB_XF86MISC "")
if(X11_Xxf86misc_FOUND)
  set(HAVE_XF86MISC 1)
  set(LIB_XF86MISC ${X11_Xxf86misc_LIB})
  cmake_push_check_state(RESET)
  set(CMAKE_REQUIRED_LIBRARIES ${X11_Xxf86misc_LIB} ${X11_Xext_LIB} ${X11_X11_LIB})
  check_function_exists(XF86MiscSetGrabKeysState HAVE_XF86MISCSETGRABKEYSSTATE)
  cmake_pop_check_state()
endif()

# AC_HAVE_GL (--without-gl): GLLIB = -lGLU -lGL $(LIB_X11)
option(KDE3_WITH_GL "OpenGL support (screen savers, kdesktop_lock)" ON)
set(GLLIB "")
if(KDE3_WITH_GL)
  set(OpenGL_GL_PREFERENCE LEGACY)
  find_package(OpenGL COMPONENTS OpenGL GLX)
  if(OPENGL_FOUND AND OPENGL_GLU_FOUND)
    set(HAVE_GL 1)
    set(GLLIB OpenGL::GLU OpenGL::GL X11::X11)
    cmake_push_check_state(RESET)
    set(CMAKE_REQUIRED_LIBRARIES ${OPENGL_gl_LIBRARY} ${OPENGL_glx_LIBRARY})
    check_function_exists(glXChooseVisual HAVE_GLXCHOOSEVISUAL)
    cmake_pop_check_state()
  endif()
endif()

# ---- kcontrol (kcontrol/*/configure.in.in) -------------------------------
# access: XKBstr.h
set(KDE3_BUILD_KCONTROL_ACCESS ${_have_xkbstr_h})

# crypto: KDE_CHECK_SSL
option(KDE3_WITH_SSL "SSL support in the crypto module" ON)
set(LIBSSL "")
# The module uses kssl and KOpenSSLProxy, which only have SSL support when
# kdelibs was built with it (KSSL_HAVE_SSL in ksslconfig.h).
file(STRINGS "${KDE3_INCLUDE_DIR}/ksslconfig.h" _kssl_have_ssl
  REGEX "^#define[ \t]+KSSL_HAVE_SSL")
if(KDE3_WITH_SSL AND NOT _kssl_have_ssl)
  message(STATUS "kdelibs was built without SSL: the crypto module has no SSL support")
endif()
if(KDE3_WITH_SSL AND _kssl_have_ssl)
  find_package(OpenSSL)
  if(OPENSSL_FOUND)
    set(HAVE_SSL 1)
    set(LIBSSL OpenSSL::SSL OpenSSL::Crypto)
  endif()
endif()

# energy, screensaver: AC_HAVE_DPMS (--without-dpms), DPMSLIB = $(LIB_X11)
option(KDE3_WITH_DPMS "DPMS power saving" ON)
set(DPMSLIB "")
if(KDE3_WITH_DPMS AND X11_dpms_FOUND)
  set(HAVE_DPMS 1)
  set(DPMSLIB X11::X11)
  # AC_CHECK_DECL: is the prototype in <X11/extensions/dpms.h>?  Compile
  # only (a static library as try_compile target is not linked).
  cmake_push_check_state(RESET)
  set(CMAKE_REQUIRED_INCLUDES ${X11_INCLUDE_DIR})
  set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
  foreach(_decl DPMSCapable DPMSInfo)
    string(TOUPPER "HAVE_${_decl}_PROTO" _var)
    check_c_source_compiles("#include <X11/Xlib.h>
#include <X11/extensions/dpms.h>
int main(void) {
#ifndef ${_decl}
  (void) ${_decl};
#endif
  return 0; }" ${_var})
  endforeach()
  unset(CMAKE_TRY_COMPILE_TARGET_TYPE)
  cmake_pop_check_state()
endif()

# fonts, kfontinst: fontconfig, FreeType 2, Xft
pkg_check_modules(FONTCONFIG QUIET IMPORTED_TARGET fontconfig>=1.0)
pkg_check_modules(FREETYPE2 QUIET IMPORTED_TARGET freetype2)
set(LIBFONTCONFIG_LIBS "")
set(LIBFREETYPE_LIBS "")
if(FONTCONFIG_FOUND)
  set(HAVE_FONTCONFIG 1)
  set(LIBFONTCONFIG_LIBS PkgConfig::FONTCONFIG)
endif()
if(FREETYPE2_FOUND)
  set(HAVE_FREETYPE2 1)
  set(LIBFREETYPE_LIBS PkgConfig::FREETYPE2)
endif()
# kfontinst needs FreeType >= 2.1.3 (libtool version 9.2.3) and
# fontconfig >= 2.0; font previews need Xft
set(KDE3_BUILD_KFONTINST FALSE)
set(LIBXFT_LIB "")
if(FREETYPE2_FOUND AND FREETYPE2_VERSION VERSION_GREATER_EQUAL 9.2.3
   AND FONTCONFIG_FOUND AND FONTCONFIG_VERSION VERSION_GREATER_EQUAL 2.0)
  set(KDE3_BUILD_KFONTINST TRUE)
  if(X11_Xft_FOUND)
    set(HAVE_XFT 1)
    set(LIBXFT_LIB X11::Xft)
  endif()
else()
  message(STATUS "kfontinst is not built: FreeType 2 or fontconfig missing")
endif()

# info
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  check_include_file(linux/raw.h HAVE_LINUX_RAW_H)
  check_include_file(sys/raw.h HAVE_SYS_RAW_H)
endif()
set(LIBDEVINFO "")
if(CMAKE_SYSTEM_NAME STREQUAL "FreeBSD")
  check_include_file(devinfo.h HAVE_DEVINFO_H)
  check_library_exists(devinfo devinfo_foreach_rman "" _have_libdevinfo)
  if(_have_libdevinfo)
    set(LIBDEVINFO devinfo)
  endif()
endif()
# KDE_CHECK_LONG_LONG
set(HAVE_LONG_LONG 1)

# input: libusb 0.1 (libusb-config, --with-libusb=check)
set(KDE3_WITH_LIBUSB "check" CACHE STRING "libusb 0.1 support in the mouse module: check, ON or OFF")
set(USB_LIBS "")
if(KDE3_WITH_LIBUSB)
  pkg_check_modules(LIBUSB QUIET IMPORTED_TARGET libusb)
  if(LIBUSB_FOUND)
    set(HAVE_LIBUSB 1)
    set(USB_LIBS PkgConfig::LIBUSB)
  elseif(NOT KDE3_WITH_LIBUSB STREQUAL "check")
    message(FATAL_ERROR "KDE3_WITH_LIBUSB was given, but test for libusb failed")
  endif()
endif()

# nics
check_function_exists(getifaddrs HAVE_GETIFADDRS)
check_function_exists(getnameinfo HAVE_GETNAMEINFO)
check_struct_has_member("struct sockaddr" sa_len "sys/types.h;sys/socket.h"
  HAVE_STRUCT_SOCKADDR_SA_LEN)

# randr
if(X11_Xrandr_FOUND)
  cmake_push_check_state(RESET)
  set(CMAKE_REQUIRED_LIBRARIES ${X11_Xrandr_LIB} ${X11_Xrender_LIB} ${X11_Xext_LIB} ${X11_X11_LIB})
  check_function_exists(XRRSetScreenConfigAndRate XRANDR_SUPPORT)
  cmake_pop_check_state()
endif()
check_type_size(long SIZEOF_LONG)

# ---- konqueror (konqueror/preloader/configure.in.in) ---------------------
# mallinfo() for the memory usage estimate in konq_mainwindow.cc
set(_kde_mallinfo_type "")
foreach(_h stdlib malloc)
  check_cxx_source_compiles("#include <${_h}.h>
int main() { struct mallinfo m; int dummy; m = mallinfo(); dummy = m.hblkhd;
  dummy = m.uordblks; dummy = m.usmblks; return dummy; }" KDE_HAVE_MALLINFO_${_h})
  if(KDE_HAVE_MALLINFO_${_h})
    set(_kde_mallinfo_type ${_h})
    break()
  endif()
endforeach()
if(_kde_mallinfo_type)
  # which fields grow by the allocated size (run test of KDE_MALLINFO_CHECK_FIELD)
  foreach(_field hblkhd uordblks usmblks)
    string(CONFIGURE [==[
#include <@_kde_mallinfo_type@.h>
enum use_type { No, Yes, Never };
use_type use_field = No;
const int SIZE = 4 * 1024 * 1024;
const int SMALL_SIZE = 4 * 1024;

void dif( struct mallinfo& m1, struct mallinfo& m2, bool alloc )
    {
    int difval = m2.@_field@ - m1.@_field@;
    if( alloc )
    {
    if( difval >= SIZE && difval < 2 * SIZE && use_field != Never )
        use_field = Yes;
    }
    else // !alloc
    {
    difval = -difval;
    if( difval > SMALL_SIZE && ( difval < SIZE || difval >= 2 * SIZE ) && use_field == Yes )
        use_field = Never;
    }
    }

int main()
    {
    free( malloc( SIZE * 2 )); // avoid Doug Lea's malloc feature of having total_max in usmblks
    struct mallinfo m1 = mallinfo();
    void* mem1 = malloc( SIZE );
    struct mallinfo m2 = mallinfo();
    free( mem1 );
    struct mallinfo m3 = mallinfo();
    void* mem2[ SIZE / 32 ];
    for( int i = 0;
     i < SIZE / 32;
     ++i )
    mem2[ i ] = malloc( 32 );
    struct mallinfo m4 = mallinfo();
    for( int i = 0;
     i < SIZE / 32;
     ++i )
    free( mem2[ i ] );
    struct mallinfo m5 = mallinfo();
    void* mem3[ SIZE / 1024 ];
    for( int i = 0;
     i < SIZE / 1024;
     ++i )
    mem3[ i ] = malloc( 1024 );
    struct mallinfo m6 = mallinfo();
    for( int i = 0;
     i < SIZE / 1024;
     ++i )
    free( mem3[ i ] );
    struct mallinfo m7 = mallinfo();
    dif( m1, m2, true );
    dif( m2, m3, false );
    dif( m3, m4, true );
    dif( m4, m5, false );
    dif( m5, m6, true );
    dif( m6, m7, false );
    return use_field == Yes ? 0 : 1;
    }
]==] _src @ONLY)
    check_cxx_source_runs("${_src}" KDE_MALLINFO_FIELD_${_field})
  endforeach()
  string(TOUPPER "${_kde_mallinfo_type}" _t)
  set(KDE_MALLINFO_${_t} 1)
endif()

# ---- kioslave (kioslave/*/configure.in.in) -------------------------------
# pop3, smtp: cyrus-sasl 2
set(SASL2_LIBS "")
check_include_file(sasl.h HAVE_SASL_H)
check_include_file(sasl/sasl.h HAVE_SASL_SASL_H)
if(HAVE_SASL_SASL_H)
  check_library_exists(sasl2 sasl_client_init "" _have_libsasl2)
  if(_have_libsasl2)
    set(HAVE_LIBSASL2 1)
    set(SASL2_LIBS sasl2)
  endif()
endif()

# filter: bzip2 protocols when kdelibs has its bzip2 filter
find_package(BZip2)

# fish: kioslave/fish/configure.in.in added -D_GNU_SOURCE to CFLAGS and
# CXXFLAGS of the whole of kdebase
add_compile_definitions(_GNU_SOURCE)
check_function_exists(isastream HAVE_ISASTREAM)

# ldap (--with-ldap): OpenLDAP with LDAP_API_VERSION >= 2004
option(KDE3_WITH_LDAP "The LDAP ioslave" ON)
set(LDAP_LIBS "")
if(KDE3_WITH_LDAP)
  find_path(LDAP_INCLUDE_DIR ldap.h)
  find_library(LDAP_LIBRARY ldap)
  find_library(LBER_LIBRARY lber)
  mark_as_advanced(LDAP_INCLUDE_DIR LDAP_LIBRARY LBER_LIBRARY)
  if(LDAP_INCLUDE_DIR AND LDAP_LIBRARY)
    set(LDAP_LIBS ${LDAP_LIBRARY})
    if(LBER_LIBRARY)
      list(APPEND LDAP_LIBS ${LBER_LIBRARY})
    endif()
    list(APPEND LDAP_LIBS ${LIBRESOLV})
    cmake_push_check_state(RESET)
    set(CMAKE_REQUIRED_INCLUDES ${LDAP_INCLUDE_DIR})
    set(CMAKE_REQUIRED_LIBRARIES ${LDAP_LIBS})
    check_c_source_compiles("#include <ldap.h>
#if LDAP_API_VERSION < 2004
#error LDAP version too old
#endif
int main(void) { LDAP *ldap = 0; return ldap != 0; }" HAVE_LIBLDAP)
    cmake_pop_check_state()
  endif()
  if(NOT HAVE_LIBLDAP)
    set(LDAP_LIBS "")
  endif()
endif()

# media: linux/cdrom.h; HAL (COMPILE_HALBACKEND) is not supported any more
check_include_file(linux/cdrom.h COMPILE_LINUXCDPOLLING)
check_function_exists(statvfs HAVE_STATVFS)

# smb (--with-samba=check): libsmbclient
set(KDE3_WITH_SAMBA "check" CACHE STRING "The smb ioslave: check, ON or OFF")
set(KDE3_BUILD_KIO_SMB FALSE)
if(KDE3_WITH_SAMBA)
  pkg_check_modules(SMBCLIENT QUIET IMPORTED_TARGET smbclient)
  if(SMBCLIENT_FOUND)
    set(KDE3_BUILD_KIO_SMB TRUE)
  elseif(NOT KDE3_WITH_SAMBA STREQUAL "check")
    message(FATAL_ERROR "KDE3_WITH_SAMBA was given, but test for libsmbclient failed")
  endif()
endif()

# thumbnail (--with-openexr=check): OpenEXR
set(KDE3_WITH_OPENEXR "check" CACHE STRING "OpenEXR thumbnails: check, ON or OFF")
set(LIB_EXR "")
if(KDE3_WITH_OPENEXR)
  pkg_check_modules(OPENEXR QUIET IMPORTED_TARGET OpenEXR)
  if(OPENEXR_FOUND)
    set(LIB_EXR PkgConfig::OPENEXR)
  elseif(NOT KDE3_WITH_OPENEXR STREQUAL "check")
    message(FATAL_ERROR "KDE3_WITH_OPENEXR was given, but test for OpenEXR failed")
  endif()
endif()

# ---- kdm (kdm/configure.in.in) -------------------------------------------
if(EXISTS /etc/ttys)
  set(BSD_INIT 1)
endif()
check_function_exists(getttyent HAVE_GETTTYENT)
if(NOT CMAKE_SYSTEM_NAME MATCHES "^(Linux|Darwin|GNU/kFreeBSD)$")
  check_function_exists(getutxent HAVE_UTMPX)
endif()
if(NOT HAVE_UTMPX)
  check_function_exists(getutent _have_getutent)
  if(NOT _have_getutent)
    set(BSD_UTMP 1)
  endif()
endif()
check_struct_has_member("struct utmp" ut_user utmp.h HAVE_STRUCT_UTMP_UT_USER)
check_struct_has_member("struct passwd" pw_expire pwd.h HAVE_STRUCT_PASSWD_PW_EXPIRE)
check_struct_has_member("struct sockaddr_in" sin_len "sys/socket.h;netinet/in.h"
  HAVE_STRUCT_SOCKADDR_IN_SIN_LEN)

cmake_push_check_state(RESET)
set(CMAKE_REQUIRED_LIBRARIES ${KDE3_LIBUTIL})
foreach(_f setlogin setusercontext getusershell login_getclass auth_timeok)
  string(TOUPPER "HAVE_${_f}" _var)
  check_function_exists(${_f} ${_var})
endforeach()
cmake_pop_check_state()
foreach(_f setproctitle sysinfo strnlen arc4random)
  string(TOUPPER "HAVE_${_f}" _var)
  check_function_exists(${_f} ${_var})
endforeach()
if(NOT HAVE_ARC4RANDOM)
  # assume that /dev/random is non-blocking if /dev/urandom does not exist
  foreach(_dev urandom random)
    if(EXISTS /dev/${_dev})
      set(DEV_RANDOM "/dev/${_dev}")
      break()
    endif()
  endforeach()
endif()
check_function_exists(vsyslog USE_SYSLOG)

# libXau and (--without-xdmcp) libXdmcp: kdm is not built without them
set(KDE3_BUILD_KDM TRUE)
if(NOT X11_Xau_FOUND)
  message(STATUS "Cannot build KDM! Make sure that libXau is installed!")
  set(KDE3_BUILD_KDM FALSE)
endif()
option(KDE3_WITH_XDMCP "XDMCP support in kdm" ON)
set(LIBXDMCP "")
if(KDE3_WITH_XDMCP)
  if(X11_Xdmcp_FOUND)
    set(LIBXDMCP X11::Xdmcp)
    cmake_push_check_state(RESET)
    set(CMAKE_REQUIRED_LIBRARIES ${X11_Xdmcp_LIB})
    check_function_exists(XdmcpWrap HASXDMAUTH)
    cmake_pop_check_state()
  else()
    message(STATUS "Cannot build KDM! Make sure that libXdmcp and Xdmcp.h are installed or set KDE3_WITH_XDMCP=OFF")
    set(KDE3_BUILD_KDM FALSE)
  endif()
  set(XDMCP 1)
endif()

# Kerberos 5 Xauth cookies (--with-krb5auth), Sun secure RPC (--with-rpcauth);
# Kerberos IV and AFS (--with-krb4, --with-afs) are not supported any more
option(KDE3_KDM_KRB5AUTH "Use Kerberos 5 for Xauth cookies in kdm" OFF)
set(KRB5_LIBS "")
if(KDE3_KDM_KRB5AUTH)
  check_include_file(krb5/krb5.h _have_krb5_krb5_h)
  if(NOT _have_krb5_krb5_h)
    message(FATAL_ERROR "KDE3_KDM_KRB5AUTH requires Kerberos5 header files")
  endif()
  set(K5AUTH 1)
  set(KRB5_LIBS krb5)
endif()
option(KDE3_KDM_RPCAUTH "Use Sun's secure RPC for Xauth cookies in kdm" OFF)
if(KDE3_KDM_RPCAUTH)
  check_include_file(rpc/rpc.h _have_rpc_rpc_h)
  if(NOT _have_rpc_rpc_h)
    message(FATAL_ERROR "KDE3_KDM_RPCAUTH requires Sun RPC header files")
  endif()
  set(SECURE_RPC 1)
endif()

if(HAVE_PAM)
  set(USE_PAM 1)
elseif(HAVE_SHADOW)
  set(USESHADOW 1)
endif()

option(KDE3_KDM_XCONSOLE "Build kdm with built-in xconsole" OFF)
if(KDE3_KDM_XCONSOLE)
  set(WITH_KDM_XCONSOLE 1)
endif()

# _FILE_OFFSET_BITS (AC_SYS_LARGEFILE)
if(CMAKE_SIZEOF_VOID_P EQUAL 4)
  set(_FILE_OFFSET_BITS 64)
endif()

# configure.in.bot of kioslave/smb, kcontrol/* etc.: the summary of what
# is left out.  <result variable>:<option switching it off, or ->:<what>
foreach(_what
    "HAVE_PAM:KDE3_PAM:PAM"
    "HAVE_SSL:KDE3_WITH_SSL:SSL in the crypto module (OpenSSL, kdelibs with SSL)"
    "KDE3_BUILD_KOMPMGR:KDE3_WITH_COMPOSITE:Xcomposite, Xdamage, Xrender and Xfixes (kompmgr)"
    "HAVE_XCOMPOSITE:KDE3_WITH_COMPOSITE:Xcomposite (taskmanager)"
    "HAVE_XINERAMA:KDE3_WITH_XINERAMA:Xinerama (ksplashsimple)"
    "HAVE_DPMS:KDE3_WITH_DPMS:DPMS (power saving)"
    "KDE3_BUILD_KXKB:-:XKB headers and libxkbfile (kxkb)"
    "HAVE_LIBSASL2:-:cyrus-sasl 2 (the pop3 and smtp ioslaves lack a lot of authentication methods)"
    "HAVE_LIBLDAP:KDE3_WITH_LDAP:OpenLDAP (ldap ioslave)"
    "KDE3_BUILD_KIO_SMB:KDE3_WITH_SAMBA:libsmbclient (smb ioslave)"
    "LIB_EXR:KDE3_WITH_OPENEXR:OpenEXR (EXR thumbnails)"
    "HAVE_LIBUSB:KDE3_WITH_LIBUSB:libusb 0.1 (Logitech mouse support)"
    "HAVE_SENSORS_SENSORS_H:KDE3_WITH_SENSORS:lm_sensors (ksysguardd)"
    "HAVE_DNSSD:KDE3_WITH_DNSSD:libdns_sd (ksysguardd)"
    "HAVE_GL:KDE3_WITH_GL:OpenGL"
    "HAVE_XSCREENSAVER:KDE3_WITH_XSCREENSAVER:XScreenSaver extension"
    "KDE3_BUILD_KFONTINST:-:FreeType 2 and fontconfig (kfontinst)"
    "KDE3_BUILD_KDM:-:libXau and libXdmcp (kdm)")
  string(REPLACE ":" ";" _what "${_what}")
  list(GET _what 0 _var)
  list(GET _what 1 _opt)
  list(GET _what 2 _desc)
  if(NOT ${_var})
    if(NOT _opt STREQUAL "-" AND DEFINED ${_opt} AND NOT "${${_opt}}" STREQUAL "" AND NOT ${_opt})
      message(STATUS "Switched off (${_opt}): ${_desc}")
    else()
      message(STATUS "Not found, built without: ${_desc}")
    endif()
  endif()
endforeach()
