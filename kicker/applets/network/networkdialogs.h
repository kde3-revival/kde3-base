/*
    Copyright (C) 2026 Serghei Amelian

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
    02110-1301, USA.
*/

#ifndef NETWORKDIALOGS_H
#define NETWORKDIALOGS_H

#include <kdialogbase.h>

#include "networkbackend.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;

/**
 * Asks for the password of a Wi-Fi network, with a switch showing it in
 * clear and the reason for asking again (a password not accepted).  It
 * is not modal: the answer comes with okClicked() or cancelClicked().
 */
class PasswordDialog : public KDialogBase
{
    Q_OBJECT

public:
    PasswordDialog(const QString &ssid, const QString &error, bool sae,
                   QWidget *parent = 0, const char *name = 0);

    QString ssid() const { return m_ssid; }
    QString password() const;

    /** Whether @p password suits the key management (WPA-PSK: 8 to 63
        characters or 64 hexadecimal digits; SAE: any). */
    static bool isValid(const QString &password, bool sae);

private slots:
    void check();
    void showPassword(bool on);

private:
    QString m_ssid;
    bool m_sae;
    QLineEdit *m_password;
};

/** Asks for the name, the security and the password of a hidden network. */
class HiddenNetworkDialog : public KDialogBase
{
    Q_OBJECT

public:
    HiddenNetworkDialog(QWidget *parent = 0, const char *name = 0);

    QString ssid() const;
    NetworkBackend::Security security() const;
    QString password() const;

private slots:
    void check();
    void showPassword(bool on);

private:
    QLineEdit *m_ssid;
    QComboBox *m_security;
    QLabel *m_passwordLabel;
    QLineEdit *m_password;
    QCheckBox *m_show;
};

#endif
