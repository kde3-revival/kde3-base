/*****************************************************************
ksmserver - the KDE session management server

Sleep and hibernate through systemd-logind.

Licensed like the rest of ksmserver, see the file LICENSE.
******************************************************************/

#ifndef KSMSERVER_LOGIND_H
#define KSMSERVER_LOGIND_H

class QString;

/*
 * A small synchronous wrapper around org.freedesktop.login1.Manager on
 * the system bus.  It deliberately has a plain interface: the GIO
 * implementation lives in logind.cpp, the only file that includes GLib
 * headers (Qt 3 defines "signals", which clashes with GLib, so they must
 * come before any Qt header).
 *
 * Every call blocks for at most a few seconds and needs no GLib main
 * loop.  Without GIO (KDEBASE_LOGIND_SLEEP=OFF) all of them return false.
 *
 * Setting KSMSERVER_LOGIND_DRYRUN=1 in ksmserver's environment makes
 * suspend() and hibernate() only log what they would do and return
 * true, for testing the logout dialog without putting the machine to
 * sleep.
 */
namespace Logind
{
    /** True if logind allows suspending (answers "yes" or "challenge"). */
    bool canSuspend();
    /** True if logind allows hibernating (answers "yes" or "challenge"). */
    bool canHibernate();

    /**
     * Ask logind to suspend or hibernate the machine.  Returns as soon as
     * logind has accepted (or refused) the request, before the machine
     * goes to sleep.  On failure, @p error (if given) receives logind's
     * or D-Bus's error message.
     */
    bool suspend( QString* error = 0 );
    bool hibernate( QString* error = 0 );
}

#endif
