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
#include <qcursor.h>
#include <qdesktopwidget.h>
#include <qdrawutil.h>
#include <qimage.h>
#include <qpainter.h>
#include <qstyle.h>
#include <qtimer.h>

#include <kglobal.h>
#include <kiconeffect.h>
#include <kiconloader.h>
#include <klocale.h>

#include "volumeosd.h"

// How long the display stays after the last change, in milliseconds.
static const int HideDelay = 1500;

// The size of the speaker, the space around and between the parts, and
// the length and thickness of the level bar, in pixels.
static const int SpeakerSize = 48;
static const int Margin = 14;
static const int Spacing = 14;
static const int BarLength = 200;
static const int BarThickness = 14;

VolumeOSD::VolumeOSD(const char *name)
    : QWidget(0, name,
              WStyle_Customize | WStyle_NoBorder | WStyle_StaysOnTop |
              WX11BypassWM),
      m_volume(0),
      m_muted(false),
      m_textWidth(0)
{
    setFocusPolicy(NoFocus);
    // Painted in full from a pixmap: no flicker of the background when
    // the display changes in place.
    setBackgroundMode(NoBackground);

    QFont f = font();
    f.setBold(true);
    if (f.pointSize() > 0)
    {
        f.setPointSize(f.pointSize() * 3 / 2);
    }
    else
    {
        f.setPixelSize(f.pixelSize() * 3 / 2);
    }
    setFont(f);

    m_hideTimer = new QTimer(this);
    connect(m_hideTimer, SIGNAL(timeout()), SLOT(hide()));
}

void VolumeOSD::loadIcons()
{
    // KMix's speaker; its icon at panel size scaled up when it is missing.
    QPixmap pix = KGlobal::iconLoader()->loadIcon("kmix", KIcon::NoGroup,
                                                  SpeakerSize, KIcon::DefaultState,
                                                  0, true);
    QImage image;
    if (pix.isNull())
    {
        KIconLoader loader("volumeapplet");
        image = loader.loadIcon("volume", KIcon::User).convertToImage();
    }
    else
    {
        image = pix.convertToImage();
    }

    if (image.isNull())
    {
        m_icon = QPixmap();
        m_mutedIcon = QPixmap();
        return;
    }

    if (image.width() != SpeakerSize || image.height() != SpeakerSize)
    {
        image = image.smoothScale(SpeakerSize, SpeakerSize, QImage::ScaleMin);
    }
    m_icon.convertFromImage(image);

    // Muted: grey and faint, like a disabled icon.
    KIconEffect::toGray(image, 1.0);
    KIconEffect::semiTransparent(image);
    m_mutedIcon.convertFromImage(image);
}

void VolumeOSD::updateSize()
{
    QFontMetrics fm(font());
    m_textWidth = QMAX(fm.width(i18n("Muted")),
                       fm.width(i18n("volume in percent", "%1%").arg(888)));

    int height = Margin + QMAX(SpeakerSize, fm.height()) + Margin;
    int width = Margin + SpeakerSize + Spacing + BarLength + Spacing +
                m_textWidth + Margin;
    resize(width, height);
}

void VolumeOSD::showVolume(int percent, bool muted)
{
    m_volume = percent;
    m_muted = muted;

    if (!isVisible())
    {
        // The icon theme or the font may have changed since last time.
        loadIcons();
        updateSize();
    }

    // In the lower third of the screen with the mouse, centred.
    QRect screen = QApplication::desktop()->screenGeometry(QCursor::pos());
    QPoint pos(screen.x() + (screen.width() - width()) / 2,
               screen.y() + screen.height() * 5 / 6 - height() / 2);
    if (pos != this->pos())
    {
        move(pos);
    }

    if (isVisible())
    {
        repaint(false);
        raise();
    }
    else
    {
        show();
        raise();
    }

    m_hideTimer->start(HideDelay, true);
}

void VolumeOSD::paintEvent(QPaintEvent *)
{
    const QColorGroup &cg = colorGroup();

    QPixmap buffer(size());
    QPainter p(&buffer, this);

    p.fillRect(rect(), cg.brush(QColorGroup::Background));
    style().drawPrimitive(QStyle::PE_PanelPopup, &p, rect(), cg,
                          QStyle::Style_Default,
                          QStyleOption(style().pixelMetric(
                              QStyle::PM_DefaultFrameWidth, this), 0));

    int x = Margin;
    const QPixmap &icon = m_muted ? m_mutedIcon : m_icon;
    if (!icon.isNull())
    {
        p.drawPixmap(x + (SpeakerSize - icon.width()) / 2,
                     (height() - icon.height()) / 2, icon);
    }
    x += SpeakerSize + Spacing;

    // The level: full above 100%, in the colours of a disabled widget
    // when muted.
    const QColorGroup &barGroup = m_muted ? palette().disabled() : cg;
    QRect bar(x, (height() - BarThickness) / 2, BarLength, BarThickness);
    qDrawShadePanel(&p, bar, barGroup, true, 1,
                    &barGroup.brush(QColorGroup::Base));
    QRect inner = bar;
    inner.addCoords(2, 2, -2, -2);
    int filled = inner.width() * QMIN(QMAX(m_volume, 0), 100) / 100;
    if (filled > 0)
    {
        p.fillRect(inner.x(), inner.y(), filled, inner.height(),
                   m_muted ? barGroup.brush(QColorGroup::Mid)
                           : barGroup.brush(QColorGroup::Highlight));
    }
    x += BarLength + Spacing;

    QString text = m_muted ? i18n("Muted")
                           : i18n("volume in percent", "%1%").arg(m_volume);
    p.setPen(cg.foreground());
    p.drawText(x, 0, m_textWidth, height(), AlignLeft | AlignVCenter, text);

    p.end();
    bitBlt(this, 0, 0, &buffer);
}

#include "volumeosd.moc"
