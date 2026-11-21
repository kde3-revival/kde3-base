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

#include <qapplication.h>
#include <qcheckbox.h>
#include <qlabel.h>
#include <qlayout.h>
#include <qpushbutton.h>
#include <qslider.h>
#include <qstyle.h>

#include <kdialog.h>
#include <klocale.h>

#include "volumepopup.h"

// A click outside the popup closes it; one on the panel button reaching
// the button within this many milliseconds does not open it again.
static const int ReopenDelay = 300;

VolumePopup::VolumePopup(QWidget *parent, const char *name)
    : QFrame(parent, name, WStyle_Customize | WType_Popup),
      m_updating(false),
      m_dragging(false),
      m_volume(-1)
{
    setFrameStyle(QFrame::PopupPanel | QFrame::Raised);
    setLineWidth(style().pixelMetric(QStyle::PM_DefaultFrameWidth, this));

    QVBoxLayout *layout = new QVBoxLayout(this,
                                          frameWidth() + KDialog::spacingHint(),
                                          KDialog::spacingHint());

    m_label = new QLabel(this);
    m_label->setAlignment(AlignCenter);
    layout->addWidget(m_label);

    // A vertical QSlider has its minimum at the top: the value is
    // 100 - percent.
    m_slider = new QSlider(0, 100, 5, 100, Vertical, this);
    m_slider->setLineStep(1);
    m_slider->setTickmarks(QSlider::Left);
    m_slider->setTickInterval(10);
    m_slider->setMinimumHeight(120);
    connect(m_slider, SIGNAL(valueChanged(int)), SLOT(sliderMoved(int)));
    connect(m_slider, SIGNAL(sliderPressed()), SLOT(sliderPressed()));
    connect(m_slider, SIGNAL(sliderReleased()), SLOT(sliderReleased()));

    QHBoxLayout *sliderLayout = new QHBoxLayout(layout);
    sliderLayout->addStretch();
    sliderLayout->addWidget(m_slider);
    sliderLayout->addStretch();

    m_mute = new QCheckBox(i18n("&Mute"), this);
    connect(m_mute, SIGNAL(toggled(bool)), SLOT(muteClicked(bool)));
    layout->addWidget(m_mute);

    m_mixer = new QPushButton(i18n("Mi&xer..."), this);
    connect(m_mixer, SIGNAL(clicked()), SIGNAL(mixerRequested()));
    layout->addWidget(m_mixer);

    setState(-1, false, false);
}

void VolumePopup::setState(int volume, bool muted, bool haveMixer)
{
    m_volume = volume;

    m_slider->setEnabled(volume >= 0);
    m_mute->setEnabled(volume >= 0);
    m_mixer->setEnabled(haveMixer);

    m_updating = true;
    m_mute->setChecked(muted);
    m_updating = false;

    // Not under the mouse: the slider would jump back to the volumes the
    // server reports for the earlier steps of the drag.
    if (!m_dragging)
    {
        showVolume(volume);
    }
}

void VolumePopup::showVolume(int percent)
{
    if (percent < 0)
    {
        m_label->setText(i18n("No device"));
        return;
    }

    m_updating = true;
    m_slider->setValue(100 - QMIN(percent, 100));
    m_updating = false;

    m_label->setText(i18n("volume in percent", "%1%").arg(percent));
}

void VolumePopup::sliderMoved(int value)
{
    if (m_updating)
    {
        return;
    }

    int percent = 100 - value;
    m_label->setText(i18n("volume in percent", "%1%").arg(percent));
    emit volumeChanged(percent);
}

void VolumePopup::sliderPressed()
{
    m_dragging = true;
}

void VolumePopup::sliderReleased()
{
    m_dragging = false;
    showVolume(m_volume);
}

void VolumePopup::muteClicked(bool on)
{
    if (!m_updating)
    {
        emit muteToggled(on);
    }
}

bool VolumePopup::justHidden() const
{
    return m_hideTime.isValid() && m_hideTime.elapsed() < ReopenDelay;
}

void VolumePopup::showEvent(QShowEvent *e)
{
    QFrame::showEvent(e);
    m_slider->setFocus();
}

void VolumePopup::hideEvent(QHideEvent *e)
{
    QFrame::hideEvent(e);
    m_dragging = false;
    m_hideTime.start();
}

void VolumePopup::keyPressEvent(QKeyEvent *e)
{
    if (e->key() == Key_Escape)
    {
        close();
        return;
    }

    QFrame::keyPressEvent(e);
}

void VolumePopup::wheelEvent(QWheelEvent *e)
{
    // The wheel anywhere in the popup moves the slider.  An event the
    // slider ignores comes back here: send it only once.
    static bool forwarding = false;
    if (m_slider->isEnabled() && !forwarding)
    {
        forwarding = true;
        QApplication::sendEvent(m_slider, e);
        forwarding = false;
    }
    e->accept();
}

#include "volumepopup.moc"
