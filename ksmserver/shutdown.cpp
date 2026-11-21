/*****************************************************************
ksmserver - the KDE session management server

Copyright (C) 2000 Matthias Ettrich <ettrich@kde.org>
Copyright (C) 2005 Lubos Lunak <l.lunak@kde.org>

relatively small extensions by Oswald Buddenhagen <ob6@inf.tu-dresden.de>

some code taken from the dcopserver (part of the KDE libraries), which is
Copyright (c) 1999 Matthias Ettrich <ettrich@kde.org>
Copyright (c) 1999 Preston Brown <pbrown@kde.org>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

******************************************************************/


#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <pwd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/stat.h>
#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif
#include <sys/socket.h>
#include <sys/un.h>

#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
#include <string.h>
#include <assert.h>

#ifdef HAVE_LIMITS_H
#include <limits.h>
#endif

#include <qfile.h>
#include <qtextstream.h>
#include <qdatastream.h>
#include <qptrstack.h>
#include <qpushbutton.h>
#include <qmessagebox.h>
#include <qguardedptr.h>
#include <qtimer.h>
#include <qregexp.h>

#include <klocale.h>
#include <kglobal.h>
#include <kconfig.h>
#include <kstandarddirs.h>
#include <unistd.h>
#include <kapplication.h>
#include <kstaticdeleter.h>
#include <ktempfile.h>
#include <kprocess.h>
#include <dcopclient.h>
#include <dcopref.h>
#include <dmctl.h>
#include <kdebug.h>
#include <knotifyclient.h>
#include <kmessagebox.h>

#include "server.h"
#include "global.h"
#include "shutdowndlg.h"
#include "client.h"
#include "logind.h"

// Whether the session runs under KDM, which then decides on shutting
// the computer down itself (the same test as kdmlib's DM class)
static bool underKDM()
{
    const char* ctl = getenv( "XDM_MANAGED" );
    return getenv( "DM_CONTROL" ) || ( ctl && ctl[0] == '/' );
}

void KSMServer::logout( int confirm, int sdtype, int sdmode )
{
    shutdown( (KApplication::ShutdownConfirm)confirm,
              (KApplication::ShutdownType)sdtype,
              (KApplication::ShutdownMode)sdmode );
}

void KSMServer::shutdown( KApplication::ShutdownConfirm confirm,
    KApplication::ShutdownType sdtype, KApplication::ShutdownMode sdmode )
{
    pendingShutdown.stop();
    if( dialogActive )
        return;
    if( state >= Shutdown ) // already performing shutdown
        return;
    if( state != Idle ) // performing startup
    {
    // perform shutdown as soon as startup is finished, in order to avoid saving partial session
        if( !pendingShutdown.isActive())
        {
            pendingShutdown.start( 1000 );
            pendingShutdown_confirm = confirm;
            pendingShutdown_sdtype = sdtype;
            pendingShutdown_sdmode = sdmode;
        }
        return;
    }

    KConfig *config = KGlobal::config();
    config->reparseConfiguration(); // config may have changed in the KControl module

    config->setGroup("General" );
    bool logoutConfirmed =
        (confirm == KApplication::ShutdownConfirmYes) ? false :
       (confirm == KApplication::ShutdownConfirmNo) ? true :
                  !config->readBoolEntry( "confirmLogout", true );
    bool offerShutdown = config->readBoolEntry( "offerShutdown", true );
    // KDM turns the computer off itself once the session has ended (and
    // knows the boot loader's options).  Under other display managers,
    // which KDE cannot talk to, systemd-logind can do it, but only for
    // the user at the computer: a remote session must not turn it off.
    bool dmShutdown = offerShutdown && DM().canShutdown();
    bool mayHalt = dmShutdown;
    bool mayReboot = dmShutdown;
    if ( offerShutdown && !dmShutdown && !underKDM() && Logind::sessionIsLocal() ) {
        mayHalt = Logind::canPowerOff();
        mayReboot = Logind::canReboot();
    }
    bool maysd = mayHalt || mayReboot;
    bool explicitType = sdtype != KApplication::ShutdownTypeNone &&
                        sdtype != KApplication::ShutdownTypeDefault;
    if (sdtype == KApplication::ShutdownTypeDefault)
        sdtype = maysd ? (KApplication::ShutdownType)
                         config->readNumEntry( "shutdownType", (int)KApplication::ShutdownTypeNone )
                       : KApplication::ShutdownTypeNone;
    if ( ( sdtype == KApplication::ShutdownTypeHalt && !mayHalt ) ||
         ( sdtype == KApplication::ShutdownTypeReboot && !mayReboot ) ) {
        if ( explicitType && logoutConfirmed )
            return; /* unsupported fast shutdown */
        sdtype = KApplication::ShutdownTypeNone;
    }
    if (sdmode == KApplication::ShutdownModeDefault)
        sdmode = KApplication::ShutdownModeInteractive;

    dialogActive = true;
    QString bopt;
    KSMShutdownDlg::SleepType sleep = KSMShutdownDlg::NoSleep;
    if ( !logoutConfirmed ) {
        // Sleeping does not end the session, so it does not depend on
        // offerShutdown or on what the display manager allows
        bool maySuspend = false;
        bool mayHibernate = false;
        if ( config->readBoolEntry( "offerSleep", true ) ) {
            maySuspend = Logind::canSuspend();
            mayHibernate = Logind::canHibernate();
        }
        KSMShutdownFeedback::start(); // make the screen gray
        logoutConfirmed =
            KSMShutdownDlg::confirmShutdown( mayHalt, mayReboot, dmShutdown,
                                             maySuspend, mayHibernate,
                                             sdtype, bopt, sleep );
        // ###### We can't make the screen remain gray while talking to the apps,
        // because this prevents interaction ("do you want to save", etc.)
        // TODO: turn the feedback widget into a list of apps to be closed,
        // with an indicator of the current status for each.
        KSMShutdownFeedback::stop(); // make the screen become normal again
    }

    if ( logoutConfirmed && sleep != KSMShutdownDlg::NoSleep ) {
        // dialogActive stays set, so no logout starts meanwhile
        goToSleep( sleep == KSMShutdownDlg::SleepHibernate,
                   config->readBoolEntry( "lockBeforeSleep", true ) );
        dialogActive = false;
        return;
    }

    logindShutdown = false;
    if ( logoutConfirmed && !dmShutdown &&
         ( sdtype == KApplication::ShutdownTypeHalt ||
           sdtype == KApplication::ShutdownTypeReboot ) ) {
        // Ask again (the dialog may have been open for a while): once the
        // session has ended, nothing could be done about a refusal
        if ( !logindMayShutdown( sdtype ) ) {
            kapp->enableStyles(); // for the message box
            KMessageBox::sorry( 0, sdtype == KApplication::ShutdownTypeReboot
                ? i18n( "The computer cannot be restarted now. The session has not been ended." )
                : i18n( "The computer cannot be turned off now. The session has not been ended." ) );
            kapp->disableStyles();
            dialogActive = false;
            return;
        }
        logindShutdown = true;
    }

    if ( logoutConfirmed ) {

	shutdownType = sdtype;
	shutdownMode = sdmode;
	bootOption = bopt;

        // shall we save the session on logout?
        saveSession = ( config->readEntry( "loginMode", "restorePreviousLogout" ) == "restorePreviousLogout" );

        if ( saveSession )
            sessionGroup = QString("Session: ") + SESSION_PREVIOUS_LOGOUT;

        // Set the real desktop background to black so that exit looks
        // clean regardless of what was on "our" desktop.
        kapp->desktop()->setBackgroundColor( Qt::black );
        state = Shutdown;
        wmPhase1WaitingCount = 0;
        saveType = saveSession?SmSaveBoth:SmSaveGlobal;
	performLegacySessionSave();
        startProtection();
        for ( KSMClient* c = clients.first(); c; c = clients.next() ) {
            c->resetState();
            // Whoever came with the idea of phase 2 got it backwards
            // unfortunately. Window manager should be the very first
            // one saving session data, not the last one, as possible
            // user interaction during session save may alter
            // window positions etc.
            // Moreover, KWin's focus stealing prevention would lead
            // to undesired effects while session saving (dialogs
            // wouldn't be activated), so it needs be assured that
            // KWin will turn it off temporarily before any other
            // user interaction takes place.
            // Therefore, make sure the WM finishes its phase 1
            // before others a chance to change anything.
            // KWin will check if the session manager is ksmserver,
            // and if yes it will save in phase 1 instead of phase 2.
            if( isWM( c )) {
                ++wmPhase1WaitingCount;
                SmsSaveYourself( c->connection(), saveType,
                             true, SmInteractStyleAny, false );
            }

        }
        if( wmPhase1WaitingCount == 0 ) { // no WM, simply start them all
            for ( KSMClient* c = clients.first(); c; c = clients.next() )
                SmsSaveYourself( c->connection(), saveType,
                             true, SmInteractStyleAny, false );
        }
        if ( clients.isEmpty() )
            completeShutdownOrCheckpoint();
    }
    dialogActive = false;
}

// Locks the screen through kdesktop; returns once it is locked.
bool KSMServer::lockScreen()
{
    QByteArray data, replyData;
    QCString replyType;
    // kdesktop answers when kdesktop_lock reports the screen as locked
    if ( !kapp->dcopClient()->call( "kdesktop", "KScreensaverIface", "lock()",
                                    data, replyType, replyData, true, 10000 ) ) {
        kdWarning( 1218 ) << "Could not lock the screen through kdesktop" << endl;
        return false;
    }
    return true;
}

// Suspends or hibernates the computer through systemd-logind, keeping
// the session.  Returns before the computer actually sleeps.
void KSMServer::goToSleep( bool hibernate, bool lock )
{
    kapp->enableStyles(); // for the message boxes
    if ( lock && !lockScreen() ) {
        QString msg = hibernate
            ? i18n( "The screen could not be locked. Hibernate the computer anyway?" )
            : i18n( "The screen could not be locked. Put the computer to sleep anyway?" );
        KGuiItem cont = hibernate ? KGuiItem( i18n( "&Hibernate" ), "hdd_unmount" )
                                  : KGuiItem( i18n( "&Sleep" ), "player_pause" );
        // Dangerous: Cancel is the default, sleeping unlocked is the risk
        if ( KMessageBox::warningContinueCancel( 0, msg, QString::null, cont, QString::null,
                                                 KMessageBox::Notify | KMessageBox::Dangerous )
             != KMessageBox::Continue ) {
            kapp->disableStyles();
            return;
        }
    }

    QString error;
    bool ok = hibernate ? Logind::hibernate( &error ) : Logind::suspend( &error );
    if ( !ok ) {
        kdWarning( 1218 ) << ( hibernate ? "Hibernate" : "Suspend" ) << " failed: "
                          << error << endl;
        KMessageBox::error( 0, hibernate
            ? i18n( "The computer could not be hibernated:\n%1" ).arg( error )
            : i18n( "The computer could not be put to sleep:\n%1" ).arg( error ) );
    }
    kapp->disableStyles();
}

// Whether systemd-logind will turn off or restart the computer for the
// user at it, without asking for a password
bool KSMServer::logindMayShutdown( KApplication::ShutdownType sdtype )
{
    if ( !Logind::sessionIsLocal() )
        return false;
    return sdtype == KApplication::ShutdownTypeReboot ? Logind::canReboot()
                                                      : Logind::canPowerOff();
}

// The session has ended: have systemd-logind turn off or restart the
// computer, where KDM would do it once ksmserver has exited
void KSMServer::logindShutdownNow()
{
    bool reboot = shutdownType == KApplication::ShutdownTypeReboot;
    kdDebug( 1218 ) << "Asking logind to " << ( reboot ? "reboot" : "power off" ) << endl;
    QString error;
    bool ok = reboot ? Logind::reboot( &error ) : Logind::powerOff( &error );
    // Nothing more can be done here: all the applications are gone, the
    // display manager takes over as after a plain logout
    if ( !ok )
        kdWarning( 1218 ) << ( reboot ? "Reboot" : "PowerOff" ) << " failed: "
                          << error << endl;
    // Only once, and nothing left for the DM in cleanUp()
    logindShutdown = false;
    shutdownType = KApplication::ShutdownTypeNone;
}

// Whether suspend() is offered: the user's choice and logind's answer
bool KSMServer::canSuspend()
{
    KConfig* config = KGlobal::config();
    config->reparseConfiguration(); // config may have changed in the KControl module
    config->setGroup( "General" );
    return config->readBoolEntry( "offerSleep", true ) && Logind::canSuspend();
}

void KSMServer::suspend()
{
    // Not from within the DCOP call: locking the screen waits for kdesktop
    QTimer::singleShot( 0, this, SLOT( suspendInternal() ) );
}

void KSMServer::suspendInternal()
{
    sleepInternal( false );
}

bool KSMServer::canHibernate()
{
    KConfig* config = KGlobal::config();
    config->reparseConfiguration();
    config->setGroup( "General" );
    return config->readBoolEntry( "offerSleep", true ) && Logind::canHibernate();
}

void KSMServer::hibernate()
{
    QTimer::singleShot( 0, this, SLOT( hibernateInternal() ) );
}

void KSMServer::hibernateInternal()
{
    sleepInternal( true );
}

void KSMServer::sleepInternal( bool hibernate )
{
    if ( state != Idle || dialogActive )
        return;
    KConfig* config = KGlobal::config();
    config->reparseConfiguration();
    config->setGroup( "General" );
    if ( !config->readBoolEntry( "offerSleep", true ) )
        return;
    dialogActive = true; // no logout meanwhile
    goToSleep( hibernate, config->readBoolEntry( "lockBeforeSleep", true ) );
    dialogActive = false;
}

/*
 A key bound in KDE is also seen by systemd-logind, which reads the
 keyboards itself: with its default HandleSuspendKey=suspend the sleep
 key would suspend twice (and without locking the screen first).  Like
 GNOME, KDE takes logind's "block" inhibitor for its suspend key
 (KEY_SLEEP, XF86Sleep) and/or its hibernate key (KEY_SUSPEND,
 XF86Suspend) while kdesktop has them bound, and gives them back to
 logind when they are not bound any more, or kdesktop is gone.  The lock
 goes away with ksmserver.

 While the screen saver or the locker runs, it has the keyboard grabbed
 and no global shortcut works: logind gets the keys back meanwhile (the
 screen is locked already, or the saver would have ended on that key
 press anyway), else the sleep key would do nothing then.
*/
void KSMServer::setSleepKeysHandled( bool suspendKey, bool hibernateKey )
{
    static bool connected = false;
    if ( !connected ) {
        connected = true;
        connectDCOPSignal( "kdesktop", "KScreensaverIface", "KDE_start_screensaver()",
                           "screensaverStarted()", false );
        connectDCOPSignal( "kdesktop", "KScreensaverIface", "KDE_stop_screensaver()",
                           "screensaverStopped()", false );
        // kdesktop gone (crashed, quit): nobody handles the keys any more
        kapp->dcopClient()->setNotifications( true );
        connect( kapp->dcopClient(), SIGNAL( applicationRemoved( const QCString& ) ),
                 SLOT( dcopApplicationRemoved( const QCString& ) ) );
    }
    boundSuspendKey = suspendKey;
    boundHibernateKey = hibernateKey;
    if ( !screensaverRunning )
        inhibitSleepKeys( suspendKey, hibernateKey );
}

// a restarted kdesktop tells its keys again
void KSMServer::dcopApplicationRemoved( const QCString& appId )
{
    if ( appId != "kdesktop" )
        return;
    boundSuspendKey = boundHibernateKey = false;
    screensaverRunning = false;
    inhibitSleepKeys( false, false );
}

void KSMServer::screensaverStarted()
{
    screensaverRunning = true;
    inhibitSleepKeys( false, false );
}

void KSMServer::screensaverStopped()
{
    screensaverRunning = false;
    inhibitSleepKeys( boundSuspendKey, boundHibernateKey );
}

void KSMServer::inhibitSleepKeys( bool suspendKey, bool hibernateKey )
{
    if ( suspendKey == inhibitedSuspendKey && hibernateKey == inhibitedHibernateKey )
        return;
    int fd = -1;
    if ( suspendKey || hibernateKey ) {
        QCString what;
        if ( suspendKey )
            what = "handle-suspend-key";
        if ( hibernateKey )
            what += what.isEmpty() ? "handle-hibernate-key" : ":handle-hibernate-key";
        QString error;
        // (the new lock first: no moment without one)
        fd = Logind::inhibit( what, QString::fromLatin1( "KDE handles the sleep keys" ), &error );
        if ( fd < 0 ) {
            kdWarning( 1218 ) << "logind Inhibit(" << what << "): " << error << endl;
            return; // the old lock stays, it is asked again next time
        }
    }
    if ( sleepKeysInhibitor >= 0 )
        ::close( sleepKeysInhibitor );
    sleepKeysInhibitor = fd;
    inhibitedSuspendKey = suspendKey;
    inhibitedHibernateKey = hibernateKey;
}

void KSMServer::pendingShutdownTimeout()
{
    shutdown( pendingShutdown_confirm, pendingShutdown_sdtype, pendingShutdown_sdmode );
}

void KSMServer::saveCurrentSession()
{
    if ( state != Idle || dialogActive )
        return;

    if ( currentSession().isEmpty() || currentSession() == SESSION_PREVIOUS_LOGOUT )
        sessionGroup = QString("Session: ") + SESSION_BY_USER;

    state = Checkpoint;
    wmPhase1WaitingCount = 0;
    saveType = SmSaveLocal;
    saveSession = true;
    performLegacySessionSave();
    for ( KSMClient* c = clients.first(); c; c = clients.next() ) {
        c->resetState();
        if( isWM( c )) {
            ++wmPhase1WaitingCount;
            SmsSaveYourself( c->connection(), saveType, false, SmInteractStyleNone, false );
        }
    }
    if( wmPhase1WaitingCount == 0 ) {
        for ( KSMClient* c = clients.first(); c; c = clients.next() )
            SmsSaveYourself( c->connection(), saveType, false, SmInteractStyleNone, false );
    }
    if ( clients.isEmpty() )
        completeShutdownOrCheckpoint();
}

void KSMServer::saveCurrentSessionAs( QString session )
{
    if ( state != Idle || dialogActive )
        return;
    sessionGroup = "Session: " + session;
    saveCurrentSession();
}

// callbacks
void KSMServer::saveYourselfDone( KSMClient* client, bool success )
{
    if ( state == Idle ) {
        // State saving when it's not shutdown or checkpoint. Probably
        // a shutdown was cancelled and the client is finished saving
        // only now. Discard the saved state in order to avoid
        // the saved data building up.
        QStringList discard = client->discardCommand();
        if( !discard.isEmpty())
            executeCommand( discard );
        return;
    }
    if ( success ) {
        client->saveYourselfDone = true;
        completeShutdownOrCheckpoint();
    } else {
        // fake success to make KDE's logout not block with broken
        // apps. A perfect ksmserver would display a warning box at
        // the very end.
        client->saveYourselfDone = true;
        completeShutdownOrCheckpoint();
    }
    startProtection();
    if( isWM( client ) && !client->wasPhase2 && wmPhase1WaitingCount > 0 ) {
        --wmPhase1WaitingCount;
        // WM finished its phase1, save the rest
        if( wmPhase1WaitingCount == 0 ) {
            for ( KSMClient* c = clients.first(); c; c = clients.next() )
                if( !isWM( c ))
                    SmsSaveYourself( c->connection(), saveType, saveType != SmSaveLocal,
                        saveType != SmSaveLocal ? SmInteractStyleAny : SmInteractStyleNone,
                        false );
        }
    }
}

void KSMServer::interactRequest( KSMClient* client, int /*dialogType*/ )
{
    if ( state == Shutdown )
        client->pendingInteraction = true;
    else
        SmsInteract( client->connection() );

    handlePendingInteractions();
}

void KSMServer::interactDone( KSMClient* client, bool cancelShutdown_ )
{
    if ( client != clientInteracting )
        return; // should not happen
    clientInteracting = 0;
    if ( cancelShutdown_ )
        cancelShutdown( client );
    else
        handlePendingInteractions();
}


void KSMServer::phase2Request( KSMClient* client )
{
    client->waitForPhase2 = true;
    client->wasPhase2 = true;
    completeShutdownOrCheckpoint();
    if( isWM( client ) && wmPhase1WaitingCount > 0 ) {
        --wmPhase1WaitingCount;
        // WM finished its phase1 and requests phase2, save the rest
        if( wmPhase1WaitingCount == 0 ) {
            for ( KSMClient* c = clients.first(); c; c = clients.next() )
                if( !isWM( c ))
                    SmsSaveYourself( c->connection(), saveType, saveType != SmSaveLocal,
                        saveType != SmSaveLocal ? SmInteractStyleAny : SmInteractStyleNone,
                        false );
        }
    }
}

void KSMServer::handlePendingInteractions()
{
    if ( clientInteracting )
        return;

    for ( KSMClient* c = clients.first(); c; c = clients.next() ) {
        if ( c->pendingInteraction ) {
            clientInteracting = c;
            c->pendingInteraction = false;
            break;
        }
    }
    if ( clientInteracting ) {
        endProtection();
        SmsInteract( clientInteracting->connection() );
    } else {
        startProtection();
    }
}


void KSMServer::cancelShutdown( KSMClient* c )
{
    kdDebug( 1218 ) << "Client " << c->program() << " (" << c->clientId() << ") canceled shutdown." << endl;
    KNotifyClient::event( 0, "cancellogout", i18n( "Logout canceled by '%1'" ).arg( c->program()));
    clientInteracting = 0;
    logindShutdown = false;
    for ( KSMClient* c = clients.first(); c; c = clients.next() ) {
        SmsShutdownCancelled( c->connection() );
        if( c->saveYourselfDone ) {
            // Discard also saved state.
            QStringList discard = c->discardCommand();
            if( !discard.isEmpty())
                executeCommand( discard );
        }
    }
    state = Idle;
}

void KSMServer::startProtection()
{
    protectionTimer.start( 10000, true );
}

void KSMServer::endProtection()
{
    protectionTimer.stop();
}

/*
   Internal protection slot, invoked when clients do not react during
  shutdown.
 */
void KSMServer::protectionTimeout()
{
    if ( ( state != Shutdown && state != Checkpoint ) || clientInteracting )
        return;

    for ( KSMClient* c = clients.first(); c; c = clients.next() ) {
        if ( !c->saveYourselfDone && !c->waitForPhase2 ) {
            kdDebug( 1218 ) << "protectionTimeout: client " << c->program() << "(" << c->clientId() << ")" << endl;
            c->saveYourselfDone = true;
        }
    }
    completeShutdownOrCheckpoint();
    startProtection();
}

void KSMServer::completeShutdownOrCheckpoint()
{
    if ( state != Shutdown && state != Checkpoint )
        return;

    for ( KSMClient* c = clients.first(); c; c = clients.next() ) {
        if ( !c->saveYourselfDone && !c->waitForPhase2 )
            return; // not done yet
    }

    // do phase 2
    bool waitForPhase2 = false;
    for ( KSMClient* c = clients.first(); c; c = clients.next() ) {
        if ( !c->saveYourselfDone && c->waitForPhase2 ) {
            c->waitForPhase2 = false;
            SmsSaveYourselfPhase2( c->connection() );
            waitForPhase2 = true;
        }
    }
    if ( waitForPhase2 )
        return;

    if ( saveSession )
        storeSession();
    else
        discardSession();

    if ( state == Shutdown ) {
        bool waitForKNotify = true;
        if( !kapp->dcopClient()->connectDCOPSignal( "knotify", "",
            "notifySignal(QString,QString,QString,QString,QString,int,int,int,int)",
            "ksmserver", "notifySlot(QString,QString,QString,QString,QString,int,int,int,int)", false )) {
            waitForKNotify = false;
        }
        if( !kapp->dcopClient()->connectDCOPSignal( "knotify", "",
            "playingFinished(int,int)",
            "ksmserver", "logoutSoundFinished(int,int)", false )) {
            waitForKNotify = false;
        }
        // event() can return -1 if KNotifyClient short-circuits and avoids KNotify
        logoutSoundEvent = KNotifyClient::event( 0, "exitkde" ); // KDE says good bye
        if( logoutSoundEvent <= 0 )
            waitForKNotify = false;
        if( waitForKNotify ) {
            state = WaitingForKNotify;
            knotifyTimeoutTimer.start( 20000, true );
            return;
        }
        startKilling();
    } else if ( state == Checkpoint ) {
        for ( KSMClient* c = clients.first(); c; c = clients.next() ) {
            SmsSaveComplete( c->connection());
        }
        state = Idle;
    }
}

void KSMServer::startKilling()
{
    knotifyTimeoutTimer.stop();
    // kill all clients
    state = Killing;
    for ( KSMClient* c = clients.first(); c; c = clients.next() ) {
        if( isWM( c )) // kill the WM as the last one in order to reduce flicker
            continue;
        kdDebug( 1218 ) << "completeShutdown: client " << c->program() << "(" << c->clientId() << ")" << endl;
        SmsDie( c->connection() );
    }

    kdDebug( 1218 ) << " We killed all clients. We have now clients.count()=" <<
       clients.count() << endl;
    completeKilling();
    QTimer::singleShot( 10000, this, SLOT( timeoutQuit() ) );
}

void KSMServer::completeKilling()
{
    kdDebug( 1218 ) << "KSMServer::completeKilling clients.count()=" <<
        clients.count() << endl;
    if( state == Killing ) {
        bool wait = false;
        for( KSMClient* c = clients.first(); c; c = clients.next()) {
            if( isWM( c ))
                continue;
            wait = true; // still waiting for clients to go away
        }
        if( wait )
            return;
        killWM();
    }
}

void KSMServer::killWM()
{
    state = KillingWM;
    bool iswm = false;
    for ( KSMClient* c = clients.first(); c; c = clients.next() ) {
        if( isWM( c )) {
            iswm = true;
            kdDebug( 1218 ) << "killWM: client " << c->program() << "(" << c->clientId() << ")" << endl;
            SmsDie( c->connection() );
        }
    }
    if( iswm ) {
        completeKillingWM();
        QTimer::singleShot( 5000, this, SLOT( timeoutWMQuit() ) );
    }
    else
        killingCompleted();
}

void KSMServer::completeKillingWM()
{
    kdDebug( 1218 ) << "KSMServer::completeKillingWM clients.count()=" <<
        clients.count() << endl;
    if( state == KillingWM ) {
        if( clients.isEmpty())
            killingCompleted();
    }
}

// shutdown is fully complete
void KSMServer::killingCompleted()
{
    if ( logindShutdown )
        logindShutdownNow();
    kapp->quit();
}

// called when KNotify performs notification for logout (not when sound is finished though)
void KSMServer::notifySlot(QString event ,QString app,QString,QString,QString,int present,int,int,int)
{
    if( state != WaitingForKNotify )
        return;
    if( event != "exitkde" || app != "ksmserver" )
        return;
    if( present & KNotifyClient::Sound ) // logoutSoundFinished() will be called
        return;
    startKilling();
}

// This is stupid. The normal DCOP signal connected to notifySlot() above should be simply
// emitted in KNotify only after the sound is finished playing.
void KSMServer::logoutSoundFinished( int event, int )
{
    if( state != WaitingForKNotify )
        return;
    if( event != logoutSoundEvent )
        return;
    startKilling();
}

void KSMServer::knotifyTimeout()
{
    if( state != WaitingForKNotify )
        return;
    startKilling();
}

void KSMServer::timeoutQuit()
{
    for (KSMClient *c = clients.first(); c; c = clients.next()) {
        kdWarning( 1218 ) << "SmsDie timeout, client " << c->program() << "(" << c->clientId() << ")" << endl;
    }
    killWM();
}

void KSMServer::timeoutWMQuit()
{
    if( state == KillingWM ) {
        kdWarning( 1218 ) << "SmsDie WM timeout" << endl;
    }
    killingCompleted();
}
