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

#include <qpainter.h>

#include <kapplication.h>
#include <kcursor.h>
#include <kglobalsettings.h>
#include <kiconeffect.h>
#include <kicontheme.h>
#include <kipc.h>

#include "volumebutton.h"

// The state icons are drawn at 22x22.
static const int IconSize = 22;

VolumeButton::VolumeButton(QWidget *parent, const char *name)
    : QWidget(parent, name),
      m_highlight(false),
      m_middlePressed(false),
      m_wheelDelta(0)
{
    setBackgroundOrigin(AncestorOrigin);

    if (KGlobalSettings::changeCursorOverIcon())
    {
        setCursor(KCursor::handCursor());
    }

    connect(kapp, SIGNAL(iconChanged(int)), SLOT(slotIconChanged(int)));
    kapp->addKipcEventMask(KIPC::IconChanged);
}

void VolumeButton::setImage(const QImage &image)
{
    m_image = image;
    generateIcons();
}

void VolumeButton::generateIcons()
{
    if (m_image.isNull())
    {
        m_normalIcon = QPixmap();
        m_activeIcon = QPixmap();
        update();
        return;
    }

    // The icon belongs with the system tray icons, which stay 22 pixels
    // on any panel: draw it at that size, smaller only on panels too
    // small for it, leaving a pixel around it.
    int size = QMIN(QMIN(width(), height()) - 2, IconSize);
    size = QMAX(size, 8);

    QImage image = m_image;
    if (image.width() != size || image.height() != size)
    {
        image = image.smoothScale(size, size, QImage::ScaleMin);
    }

    // KIconEffect changes the pixels of the image it gets, and a QImage
    // copy shares them (explicit sharing): give each effect its own deep
    // copy, or the effects pile up on m_image with every update.
    KIconEffect effect;
    m_normalIcon = effect.apply(image.copy(), KIcon::Panel, KIcon::DefaultState);
    m_activeIcon = effect.apply(image.copy(), KIcon::Panel, KIcon::ActiveState);
    update();
}

void VolumeButton::slotIconChanged(int group)
{
    if (group == KIcon::Panel)
    {
        generateIcons();
    }
}

void VolumeButton::paintEvent(QPaintEvent *)
{
    const QPixmap &pix = m_highlight ? m_activeIcon : m_normalIcon;
    if (pix.isNull())
    {
        return;
    }

    QPainter p(this);
    p.drawPixmap((width() - pix.width()) / 2, (height() - pix.height()) / 2,
                 pix);
}

void VolumeButton::resizeEvent(QResizeEvent *)
{
    generateIcons();
}

void VolumeButton::enterEvent(QEvent *)
{
    m_highlight = true;
    update();
}

void VolumeButton::leaveEvent(QEvent *)
{
    m_highlight = false;
    update();
}

void VolumeButton::mousePressEvent(QMouseEvent *e)
{
    switch (e->button())
    {
        case LeftButton:
            emit leftPressed();
            break;
        case MidButton:
            m_middlePressed = true;
            break;
        case RightButton:
            emit rightPressed(e->globalPos());
            break;
        default:
            e->ignore();
            break;
    }
}

void VolumeButton::mouseReleaseEvent(QMouseEvent *e)
{
    if (e->button() == MidButton && m_middlePressed)
    {
        m_middlePressed = false;
        if (rect().contains(e->pos()))
        {
            emit middleClicked();
        }
    }
}

void VolumeButton::wheelEvent(QWheelEvent *e)
{
    // Wheels with finer steps than a notch add up to one.
    m_wheelDelta += e->delta();
    int steps = m_wheelDelta / 120;
    m_wheelDelta -= steps * 120;

    if (steps)
    {
        emit wheelSteps(steps);
    }
}

#include "volumebutton.moc"
