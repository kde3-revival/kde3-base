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

#include <qcheckbox.h>
#include <qcombobox.h>
#include <qlabel.h>
#include <qlayout.h>
#include <qlineedit.h>
#include <qstylesheet.h>

#include <kglobal.h>
#include <kiconloader.h>
#include <klocale.h>

#include "networkdialogs.h"

// The order of the security choices of a hidden network.
static const NetworkBackend::Security HiddenSecurity[] = {
    NetworkBackend::WPAPersonal,
    NetworkBackend::WPA3Personal,
    NetworkBackend::Open
};

static QCheckBox *showSwitch(QWidget *parent, QLineEdit *edit, QObject *receiver)
{
    QCheckBox *show = new QCheckBox(i18n("S&how password"), parent);
    QObject::connect(show, SIGNAL(toggled(bool)), receiver, SLOT(showPassword(bool)));
    // Shown in clear, the field stays for what it is.
    edit->setEchoMode(QLineEdit::Password);
    return show;
}

PasswordDialog::PasswordDialog(const QString &ssid, const QString &error, bool sae,
                               QWidget *parent, const char *name)
    : KDialogBase(parent, name, false, i18n("Wi-Fi Password"), Ok | Cancel, Ok),
      m_ssid(ssid),
      m_sae(sae)
{
    QWidget *page = new QWidget(this);
    setMainWidget(page);

    QGridLayout *grid = new QGridLayout(page, 4, 2, 0, spacingHint());

    QLabel *icon = new QLabel(page);
    icon->setPixmap(DesktopIcon("encrypted", 32));
    icon->setAlignment(AlignTop);
    grid->addMultiCellWidget(icon, 0, 3, 0, 0);

    QLabel *text = new QLabel(
        i18n("The Wi-Fi network <b>%1</b> needs a password.")
            .arg(QStyleSheet::escape(ssid)),
        page);
    grid->addWidget(text, 0, 1);

    int row = 1;
    if (!error.isEmpty())
    {
        QLabel *errorLabel = new QLabel(QString("<font color=\"red\">%1</font>")
                                            .arg(QStyleSheet::escape(error)),
                                        page);
        errorLabel->setTextFormat(RichText);
        grid->addWidget(errorLabel, row++, 1);
    }

    QHBoxLayout *line = new QHBoxLayout(spacingHint());
    QLabel *label = new QLabel(i18n("&Password:"), page);
    line->addWidget(label);
    m_password = new QLineEdit(page);
    m_password->setMinimumWidth(fontMetrics().width('x') * 30);
    label->setBuddy(m_password);
    line->addWidget(m_password);
    grid->addLayout(line, row++, 1);

    QCheckBox *show = showSwitch(page, m_password, this);
    grid->addWidget(show, row++, 1);

    connect(m_password, SIGNAL(textChanged(const QString &)), SLOT(check()));
    check();
    m_password->setFocus();
}

QString PasswordDialog::password() const
{
    return m_password->text();
}

bool PasswordDialog::isValid(const QString &password, bool sae)
{
    if (sae)
    {
        return !password.isEmpty();
    }

    if (password.length() == 64)
    {
        static const QString Hex = "0123456789abcdefABCDEF";
        for (uint i = 0; i < password.length(); ++i)
        {
            if (Hex.find(password[i]) < 0)
            {
                return false;
            }
        }
        return true;
    }
    return password.length() >= 8 && password.length() <= 63;
}

void PasswordDialog::check()
{
    enableButtonOK(isValid(m_password->text(), m_sae));
}

void PasswordDialog::showPassword(bool on)
{
    m_password->setEchoMode(on ? QLineEdit::Normal : QLineEdit::Password);
}

HiddenNetworkDialog::HiddenNetworkDialog(QWidget *parent, const char *name)
    : KDialogBase(parent, name, false, i18n("Connect to a Hidden Network"),
                  Ok | Cancel, Ok)
{
    setButtonOK(KGuiItem(i18n("C&onnect"), "connect_creating"));

    QWidget *page = new QWidget(this);
    setMainWidget(page);

    QGridLayout *grid = new QGridLayout(page, 5, 2, 0, spacingHint());

    QLabel *text = new QLabel(
        i18n("Enter the name and the security of the hidden Wi-Fi network."), page);
    grid->addMultiCellWidget(text, 0, 0, 0, 1);

    QLabel *label = new QLabel(i18n("Network &name:"), page);
    grid->addWidget(label, 1, 0);
    m_ssid = new QLineEdit(page);
    m_ssid->setMaxLength(32);
    m_ssid->setMinimumWidth(fontMetrics().width('x') * 30);
    label->setBuddy(m_ssid);
    grid->addWidget(m_ssid, 1, 1);

    label = new QLabel(i18n("&Security:"), page);
    grid->addWidget(label, 2, 0);
    m_security = new QComboBox(false, page);
    for (unsigned i = 0; i < sizeof(HiddenSecurity) / sizeof(HiddenSecurity[0]); ++i)
    {
        m_security->insertItem(NetworkBackend::securityName(HiddenSecurity[i]));
    }
    label->setBuddy(m_security);
    grid->addWidget(m_security, 2, 1);

    m_passwordLabel = new QLabel(i18n("&Password:"), page);
    grid->addWidget(m_passwordLabel, 3, 0);
    m_password = new QLineEdit(page);
    m_passwordLabel->setBuddy(m_password);
    grid->addWidget(m_password, 3, 1);

    m_show = showSwitch(page, m_password, this);
    grid->addWidget(m_show, 4, 1);

    connect(m_ssid, SIGNAL(textChanged(const QString &)), SLOT(check()));
    connect(m_password, SIGNAL(textChanged(const QString &)), SLOT(check()));
    connect(m_security, SIGNAL(activated(int)), SLOT(check()));
    check();
    m_ssid->setFocus();
}

QString HiddenNetworkDialog::ssid() const
{
    return m_ssid->text();
}

NetworkBackend::Security HiddenNetworkDialog::security() const
{
    return HiddenSecurity[m_security->currentItem()];
}

QString HiddenNetworkDialog::password() const
{
    return security() == NetworkBackend::Open ? QString::null : m_password->text();
}

void HiddenNetworkDialog::check()
{
    NetworkBackend::Security s = security();
    bool needPassword = s != NetworkBackend::Open;
    m_passwordLabel->setEnabled(needPassword);
    m_password->setEnabled(needPassword);
    m_show->setEnabled(needPassword);

    bool ok = !m_ssid->text().isEmpty() &&
              m_ssid->text().utf8().length() <= 32 &&
              (!needPassword ||
               PasswordDialog::isValid(m_password->text(),
                                       s == NetworkBackend::WPA3Personal));
    enableButtonOK(ok);
}

void HiddenNetworkDialog::showPassword(bool on)
{
    m_password->setEchoMode(on ? QLineEdit::Normal : QLineEdit::Password);
}

#include "networkdialogs.moc"
