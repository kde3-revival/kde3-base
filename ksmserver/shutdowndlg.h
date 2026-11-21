/*****************************************************************
ksmserver - the KDE session management server

Copyright (C) 2000 Matthias Ettrich <ettrich@kde.org>
******************************************************************/

#ifndef SHUTDOWNDLG_H
#define SHUTDOWNDLG_H

#include <qpixmap.h>
#include <qdialog.h>
#include <kpushbutton.h>
class QPushButton;
class QVButtonGroup;
class QPopupMenu;
class QTimer;

#include <kapplication.h>

// The (singleton) widget that makes the desktop gray.
class KSMShutdownFeedback : public QWidget
{
    Q_OBJECT

public:
    static void start() { s_pSelf = new KSMShutdownFeedback(); s_pSelf->show(); }
    static void stop() { delete s_pSelf; s_pSelf = 0L; }
    static KSMShutdownFeedback * self() { return s_pSelf; }

protected:
    ~KSMShutdownFeedback() {}

private slots:
    void slotPaintEffect();

private:
    static KSMShutdownFeedback * s_pSelf;
    KSMShutdownFeedback();
    int m_currentY;
    QPixmap m_root;
};


// The confirmation dialog
class KSMShutdownDlg : public QDialog
{
    Q_OBJECT

public:
    // Sleeping keeps the session, so it is not a KApplication::ShutdownType
    enum SleepType { NoSleep, SleepSuspend, SleepHibernate };

    // Returns true if the user confirmed; then either @p sleep says how
    // to put the computer to sleep, or (if it is NoSleep) @p sdtype and
    // @p bopt say how to end the session.  The boot options to restart
    // into are offered only if @p dmBootOptions (the DM shuts down).
    // On entry, @p sdtype or else @p sleep is the default (focused) action.
    static bool confirmShutdown( bool mayHalt, bool mayReboot, bool dmBootOptions,
                                 bool maySuspend, bool mayHibernate,
                                 KApplication::ShutdownType& sdtype, QString& bopt,
                                 SleepType& sleep );

public slots:
    void slotLogout();
    void slotHalt();
    void slotReboot();
    void slotReboot(int);
    void slotSuspend();
    void slotHibernate();

protected:
    ~KSMShutdownDlg() {};

private:
    KSMShutdownDlg( QWidget* parent, bool mayHalt, bool mayReboot, bool dmBootOptions,
                    bool maySuspend, bool mayHibernate,
                    KApplication::ShutdownType sdtype, SleepType defaultSleep );
    KApplication::ShutdownType m_shutdownType;
    SleepType m_sleep;
    QString m_bootOption;
    QPopupMenu *targets;
    QStringList rebootOptions;
};

class KSMDelayedPushButton : public KPushButton
{
  Q_OBJECT

public:

  KSMDelayedPushButton( const KGuiItem &item, QWidget *parent, const char *name = 0 );
  void setPopup( QPopupMenu *pop);

private slots:
  void slotTimeout();
  void slotPressed();
  void slotReleased();

private:
  QPopupMenu *pop;
  QTimer *popt;
};

#endif
