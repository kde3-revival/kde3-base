/*****************************************************************
ksmserver - the KDE session management server

Sleep, hibernate, power off and reboot through systemd-logind.

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
 * suspend(), hibernate(), powerOff() and reboot() only log what they
 * would do and return true, for testing the logout dialog without
 * putting the machine to sleep or turning it off.  (The bus can also be
 * redirected to a fake logind through GIO's DBUS_SYSTEM_BUS_ADDRESS.)
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

    /**
     * True if logind would power off / reboot the machine for us without
     * asking for a password (answers "yes").
     *
     * Unlike sleeping, "challenge" does not count: the power off is only
     * requested once the session has ended, when no polkit agent could
     * ask for the password any more (and KDE 3 has none anyway), so it
     * would fail with the session already gone.  logind typically says
     * "challenge" for sessions that are remote or not in the foreground,
     * or when other users' inhibitors block the shutdown.
     */
    bool canPowerOff();
    bool canReboot();

    /**
     * True if our own logind session (the one ksmserver runs in, or
     * else $XDG_SESSION_ID) is a local one on a seat and in the
     * foreground: not Remote, attached to a Seat, and Active.  Only then
     * does turning the computer off concern the person sitting at it.
     */
    bool sessionIsLocal();

    /**
     * Ask logind to power off or reboot the machine.  Returns once
     * logind has accepted (or refused) the request.  On failure,
     * @p error (if given) receives logind's or D-Bus's error message.
     */
    bool powerOff( QString* error = 0 );
    bool reboot( QString* error = 0 );
}

#endif
