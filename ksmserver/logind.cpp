/*****************************************************************
ksmserver - the KDE session management server

Sleep and hibernate through systemd-logind.

Licensed like the rest of ksmserver, see the file LICENSE.
******************************************************************/

#include <config.h>

// GLib's headers must come before any Qt header: Qt 3 defines "signals"
// as a macro, which breaks GLib's declarations that use the word.
#ifdef HAVE_LOGIND_SLEEP
#include <gio/gio.h>
#endif

#include <stdlib.h>
#include <string.h>

#include <qstring.h>
#include <kdebug.h>

#include "logind.h"

#ifdef HAVE_LOGIND_SLEEP

static const char logindService[] = "org.freedesktop.login1";
static const char logindPath[] = "/org/freedesktop/login1";
static const char logindManager[] = "org.freedesktop.login1.Manager";

// How long to wait for logind.  The Can* questions are asked each time
// the logout dialog opens; Suspend and Hibernate return once logind has
// queued the sleep, so neither should take long.
static const int canTimeout = 3000;
static const int sleepTimeout = 10000;

static QString errorString( GError* err )
{
    QString msg = QString::fromUtf8( err->message );
    g_error_free( err );
    return msg;
}

// Calls a method of logind's Manager and returns its reply, or 0 with
// @p error set.  Takes over a floating @p params.
static GVariant* callManager( const char* method, GVariant* params,
                              const GVariantType* replyType, int timeout,
                              QString* error )
{
    if ( params )
        g_variant_ref_sink( params );

    GError* err = 0;
    GDBusConnection* bus = g_bus_get_sync( G_BUS_TYPE_SYSTEM, 0, &err );
    GVariant* reply = 0;
    if ( bus ) {
        // The shared system bus connection _exit()s the process when the
        // bus goes away (e.g. the D-Bus daemon is restarted); that must
        // never take the whole session down with it.
        g_dbus_connection_set_exit_on_close( bus, FALSE );
        reply = g_dbus_connection_call_sync( bus, logindService, logindPath,
                                             logindManager, method, params,
                                             replyType, G_DBUS_CALL_FLAGS_NONE,
                                             timeout, 0, &err );
        g_object_unref( bus );
    }
    if ( params )
        g_variant_unref( params );

    if ( !reply && error )
        *error = errorString( err );
    else if ( !reply )
        g_error_free( err );
    return reply;
}

static bool can( const char* method )
{
    QString error;
    GVariant* reply = callManager( method, 0, G_VARIANT_TYPE( "(s)" ),
                                   canTimeout, &error );
    if ( !reply ) {
        kdWarning( 1218 ) << "logind " << method << ": " << error << endl;
        return false;
    }
    const gchar* answer = 0;
    g_variant_get( reply, "(&s)", &answer );
    kdDebug( 1218 ) << "logind " << method << ": " << answer << endl;
    // "challenge": allowed after authentication through polkit
    bool ok = strcmp( answer, "yes" ) == 0 || strcmp( answer, "challenge" ) == 0;
    g_variant_unref( reply );
    return ok;
}

static bool goToSleep( const char* method, QString* error )
{
    const char* dryRun = getenv( "KSMSERVER_LOGIND_DRYRUN" );
    if ( dryRun && *dryRun && strcmp( dryRun, "0" ) != 0 ) {
        kdWarning( 1218 ) << "KSMSERVER_LOGIND_DRYRUN is set, not calling logind "
                          << method << "(true)" << endl;
        return true;
    }

    // interactive = true: let polkit ask for a password if it must
    GVariant* reply = callManager( method, g_variant_new( "(b)", TRUE ), 0,
                                   sleepTimeout, error );
    if ( !reply )
        return false;
    g_variant_unref( reply );
    return true;
}

bool Logind::canSuspend()
{
    return can( "CanSuspend" );
}

bool Logind::canHibernate()
{
    return can( "CanHibernate" );
}

bool Logind::suspend( QString* error )
{
    return goToSleep( "Suspend", error );
}

bool Logind::hibernate( QString* error )
{
    return goToSleep( "Hibernate", error );
}

#else // HAVE_LOGIND_SLEEP

static bool unsupported( QString* error )
{
    if ( error )
        *error = QString::fromLatin1( "ksmserver was built without systemd-logind support" );
    return false;
}

bool Logind::canSuspend()
{
    return false;
}

bool Logind::canHibernate()
{
    return false;
}

bool Logind::suspend( QString* error )
{
    return unsupported( error );
}

bool Logind::hibernate( QString* error )
{
    return unsupported( error );
}

#endif // HAVE_LOGIND_SLEEP
