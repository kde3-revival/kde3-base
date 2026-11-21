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

#ifndef NETWORKBUTTON_H
#define NETWORKBUTTON_H

#include <qimage.h>
#include <qpixmap.h>
#include <qwidget.h>

/**
 * The applet's face: the state icon, scaled to the panel and highlighted
 * under the mouse like other panel icons.  It only reports the mouse.
 */
class NetworkButton : public QWidget
{
    Q_OBJECT

public:
    NetworkButton(QWidget *parent = 0, const char *name = 0);

    /** Shows @p image, scaled to the size of the button when needed. */
    void setImage(const QImage &image);

signals:
    void leftPressed();
    void rightPressed(const QPoint &globalPos);

protected:
    virtual void paintEvent(QPaintEvent *e);
    virtual void resizeEvent(QResizeEvent *e);
    virtual void enterEvent(QEvent *e);
    virtual void leaveEvent(QEvent *e);
    virtual void mousePressEvent(QMouseEvent *e);

private slots:
    void slotIconChanged(int group);

private:
    void generateIcons();

    QImage m_image;
    QPixmap m_normalIcon;
    QPixmap m_activeIcon;
    bool m_highlight;
};

#endif
