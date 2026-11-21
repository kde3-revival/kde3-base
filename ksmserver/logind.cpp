/*****************************************************************
ksmserver - the KDE session management server

Sleep, hibernate, power off and reboot through systemd-logind.

Licensed like the rest of ksmserver, see the file LICENSE.
******************************************************************/

#include <config.h>

// GLib's headers must come before any Qt header: Qt 3 defines "signals"
// as a macro, which breaks GLib's declarations that use the word.
#ifdef HAVE_LOGIND_SLEEP
#include <gio/gio.h>
#include <gio/gunixfdlist.h>
#endif

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <qstring.h>
#include <kdebug.h>

#include "logind.h"

#ifdef HAVE_LOGIND_SLEEP

static const char logindService[] = "org.freedesktop.login1";
static const char logindPath[] = "/org/freedesktop/login1";
static const char logindManager[] = "org.freedesktop.login1.Manager";
static const char logindSession[] = "org.freedesktop.login1.Session";
static const char propertiesInterface[] = "org.freedesktop.DBus.Properties";

// How long to wait for logind.  The Can* questions are asked each time
// the logout dialog opens; Suspend, Hibernate, PowerOff and Reboot
// return once logind has queued the job, so none should take long.
static const int canTimeout = 3000;
static const int actionTimeout = 10000;

static QString errorString( GError* err )
{
    QString msg = QString::fromUtf8( err->message );
    g_error_free( err );
    return msg;
}

// Calls a method of logind on @p path and returns its reply, or 0 with
// @p error set.  Takes over a floating @p params.
static GVariant* callLogind( const char* path, const char* interface,
                             const char* method, GVariant* params,
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
        reply = g_dbus_connection_call_sync( bus, logindService, path,
                                             interface, method, params,
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

static GVariant* callManager( const char* method, GVariant* params,
                              const GVariantType* replyType, int timeout,
                              QString* error )
{
    return callLogind( logindPath, logindManager, method, params,
                       replyType, timeout, error );
}

// Asks one of logind's Can* questions.  "challenge" counts as allowed
// only if @p challengeOk.
static bool can( const char* method, bool challengeOk )
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
    bool ok = strcmp( answer, "yes" ) == 0
              || ( challengeOk && strcmp( answer, "challenge" ) == 0 );
    g_variant_unref( reply );
    return ok;
}

// Object path of our logind session (to be freed), or 0
static gchar* ourSession()
{
    QString error;
    // The session ksmserver itself runs in
    GVariant* reply = callManager( "GetSessionByPID",
                                   g_variant_new( "(u)", (guint32)getpid() ),
                                   G_VARIANT_TYPE( "(o)" ), canTimeout, &error );
    if ( !reply ) {
        kdDebug( 1218 ) << "logind GetSessionByPID: " << error << endl;
        // e.g. started outside of the session's processes
        const char* id = getenv( "XDG_SESSION_ID" );
        if ( !id || !*id )
            return 0;
        reply = callManager( "GetSession", g_variant_new( "(s)", id ),
                             G_VARIANT_TYPE( "(o)" ), canTimeout, &error );
        if ( !reply ) {
            kdWarning( 1218 ) << "logind GetSession(" << id << "): " << error << endl;
            return 0;
        }
    }
    gchar* path = 0;
    g_variant_get( reply, "(o)", &path );
    g_variant_unref( reply );
    return path;
}

static bool action( const char* method, QString* error )
{
    const char* dryRun = getenv( "KSMSERVER_LOGIND_DRYRUN" );
    if ( dryRun && *dryRun && strcmp( dryRun, "0" ) != 0 ) {
        kdWarning( 1218 ) << "KSMSERVER_LOGIND_DRYRUN is set, not calling logind "
                          << method << "(true)" << endl;
        return true;
    }

    // interactive = true: let polkit ask for a password if it must
    GVariant* reply = callManager( method, g_variant_new( "(b)", TRUE ), 0,
                                   actionTimeout, error );
    if ( !reply )
        return false;
    g_variant_unref( reply );
    return true;
}

bool Logind::canSuspend()
{
    return can( "CanSuspend", true );
}

bool Logind::canHibernate()
{
    return can( "CanHibernate", true );
}

bool Logind::suspend( QString* error )
{
    return action( "Suspend", error );
}

bool Logind::hibernate( QString* error )
{
    return action( "Hibernate", error );
}

bool Logind::canPowerOff()
{
    return can( "CanPowerOff", false );
}

bool Logind::canReboot()
{
    return can( "CanReboot", false );
}

bool Logind::sessionIsLocal()
{
    gchar* path = ourSession();
    if ( !path )
        return false;

    QString error;
    GVariant* reply = callLogind( path, propertiesInterface, "GetAll",
                                  g_variant_new( "(s)", logindSession ),
                                  G_VARIANT_TYPE( "(a{sv})" ), canTimeout, &error );
    if ( !reply ) {
        kdWarning( 1218 ) << "logind session " << path << ": " << error << endl;
        g_free( path );
        return false;
    }
    GVariant* props = g_variant_get_child_value( reply, 0 );
    gboolean remote = TRUE, active = FALSE;
    const gchar* seat = 0;
    // Missing or mistyped properties leave the session non-local
    g_variant_lookup( props, "Remote", "b", &remote );
    g_variant_lookup( props, "Active", "b", &active );
    g_variant_lookup( props, "Seat", "(&s&o)", &seat, (const gchar**)0 );
    bool local = !remote && active && seat && *seat;
    kdDebug( 1218 ) << "logind session " << path << ": remote=" << bool( remote )
                    << " active=" << bool( active ) << " seat=" << ( seat ? seat : "" )
                    << endl;
    g_variant_unref( props );
    g_variant_unref( reply );
    g_free( path );
    return local;
}

bool Logind::powerOff( QString* error )
{
    return action( "PowerOff", error );
}

bool Logind::reboot( QString* error )
{
    return action( "Reboot", error );
}

int Logind::inhibit( const char* what, const QString& why, QString* error )
{
    GError* err = 0;
    GDBusConnection* bus = g_bus_get_sync( G_BUS_TYPE_SYSTEM, 0, &err );
    if ( !bus ) {
        if ( error )
            *error = errorString( err );
        else
            g_error_free( err );
        return -1;
    }
    g_dbus_connection_set_exit_on_close( bus, FALSE ); // see callLogind()
    GUnixFDList* fds = 0;
    GVariant* reply = g_dbus_connection_call_with_unix_fd_list_sync( bus,
        logindService, logindPath, logindManager, "Inhibit",
        g_variant_new( "(ssss)", what, "KDE", why.utf8().data(), "block" ),
        G_VARIANT_TYPE( "(h)" ), G_DBUS_CALL_FLAGS_NONE, canTimeout,
        0, &fds, 0, &err );
    g_object_unref( bus );
    int fd = -1;
    if ( reply ) {
        gint32 index = -1;
        g_variant_get( reply, "(h)", &index );
        g_variant_unref( reply );
        if ( fds )
            fd = g_unix_fd_list_get( fds, index, &err ); // (a dup of it)
        else
            err = g_error_new_literal( G_IO_ERROR, G_IO_ERROR_FAILED, "no file descriptor" );
    }
    if ( fds )
        g_object_unref( fds );
    if ( fd < 0 && err ) {
        if ( error )
            *error = errorString( err );
        else
            g_error_free( err );
    }
    return fd;
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

bool Logind::canPowerOff()
{
    return false;
}

bool Logind::canReboot()
{
    return false;
}

bool Logind::sessionIsLocal()
{
    return false;
}

bool Logind::powerOff( QString* error )
{
    return unsupported( error );
}

bool Logind::reboot( QString* error )
{
    return unsupported( error );
}

int Logind::inhibit( const char*, const QString&, QString* error )
{
    unsupported( error );
    return -1;
}

#endif // HAVE_LOGIND_SLEEP
