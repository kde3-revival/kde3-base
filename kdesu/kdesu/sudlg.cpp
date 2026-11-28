/* vi: ts=8 sts=4 sw=4
 *
 * This file is part of the KDE project, module kdesu.
 * Copyright (C) 2000 Geert Jansen <jansen@kde.org>
 */

#include <config.h>
#include <qstring.h>
#include <kconfig.h>
#include <kdebug.h>
#include <klocale.h>
#include <kmessagebox.h>

#include <kdesu/su.h>
#include "sudlg.h"

KDEsuDialog::KDEsuDialog(QCString user, QCString auth_user, bool enableKeep,const QString& icon, bool withIgnoreButton)
     : KPasswordDialog(Password, enableKeep, (withIgnoreButton ? User1:NoDefault), icon)
{
    // su (root's password) or sudo (the user's own, for the administrators)
    const QString superUserCommand = SuProcess::superUserCommandName();

    m_User = auth_user;
    setCaption(i18n("Run as %1").arg(user));

    QString prompt;
    if (superUserCommand == "sudo" && m_User == "root") {
	    prompt = i18n("Please enter your password." );
    } else {
        if (m_User == "root") {
	    prompt = i18n("The action you requested needs root privileges. "
	    "Please enter root's password below or click "
	    "Ignore to continue with your current privileges.");
        } else {
	    prompt = i18n("The action you requested needs additional privileges. "
		"Please enter the password for \"%1\" below or click "
		"Ignore to continue with your current privileges.").arg(m_User);
	}
    }
    setPrompt(prompt);

    if( withIgnoreButton )
	setButtonText(User1, i18n("&Ignore"));
}


KDEsuDialog::~KDEsuDialog()
{
}

bool KDEsuDialog::checkPassword(const char *password)
{
    // SuProcess runs sudo only to become root
    const QString command = m_User == "root" ? SuProcess::superUserCommandName()
                                             : QString::fromLatin1("su");
    SuProcess proc;
    proc.setUser(m_User);
    int status = proc.checkInstall(password);
    switch (status)
    {
    case -1:
	KMessageBox::sorry(this, i18n("Conversation with %1 failed.")
		.arg(command));
	done(Rejected);
	return false;

    case 0:
	return true;

    case SuProcess::SuNotFound:
        KMessageBox::sorry(this,
		i18n("The program '%1' is not found;\n"
		     "make sure your PATH is set correctly.")
		.arg(command));
	done(Rejected);
	return false;

    case SuProcess::SuNotAllowed:
        if (command == "sudo")
            KMessageBox::sorry(this,
		i18n("You are not allowed to use 'sudo';\n"
		     "the sudoers file does not let you run programs "
		     "as root."));
        else
            KMessageBox::sorry(this,
		i18n("You are not allowed to use 'su';\n"
		     "on some systems, you need to be in a special "
		     "group (often: wheel) to use this program."));
	done(Rejected);
	return false;

    case SuProcess::SuIncorrectPassword:
        KMessageBox::sorry(this, i18n("Incorrect password; please try again."));
	return false;

    default:
        KMessageBox::error(this, i18n("Internal error: illegal return from "
		"SuProcess::checkInstall()"));
	done(Rejected);
	return false;
    }
}

void KDEsuDialog::slotUser1()
{
    done(AsUser);
}

#include "sudlg.moc"
