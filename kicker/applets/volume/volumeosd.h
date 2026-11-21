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

#ifndef VOLUMEOSD_H
#define VOLUMEOSD_H

#include <qpixmap.h>
#include <qwidget.h>

class QTimer;

/**
 * The on-screen display of a volume change: the speaker, a level bar
 * and the volume in percent (or "Muted"), in the lower third of the
 * screen with the mouse pointer.  It is not managed by the window
 * manager and never takes the focus, and hides itself a moment after
 * the last change it was shown for.
 */
class VolumeOSD : public QWidget
{
    Q_OBJECT

public:
    VolumeOSD(const char *name = 0);

    /**
     * Shows @p percent (above 100 when amplified) and whether it is
     * @p muted, updating the window in place when it is already shown,
     * and hides it again a moment later.
     */
    void showVolume(int percent, bool muted);

protected:
    virtual void paintEvent(QPaintEvent *e);

private:
    void loadIcons();
    void updateSize();

    QTimer *m_hideTimer;
    QPixmap m_icon;
    QPixmap m_mutedIcon;
    int m_volume;
    bool m_muted;
    int m_textWidth;
};

#endif
