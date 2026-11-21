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

#ifndef VOLUMEPOPUP_H
#define VOLUMEPOPUP_H

#include <qdatetime.h>
#include <qframe.h>

class QCheckBox;
class QLabel;
class QPushButton;
class QSlider;

/**
 * The popup of the panel button: a slider for the volume of the default
 * sink, its mute switch and a button starting the mixer.  It closes when
 * clicked outside.
 */
class VolumePopup : public QFrame
{
    Q_OBJECT

public:
    VolumePopup(QWidget *parent = 0, const char *name = 0);

    /**
     * Shows the state: the volume in percent (-1 for no device), whether
     * it is muted, and whether there is a mixer to start.
     */
    void setState(int volume, bool muted, bool haveMixer);

    /**
     * Whether the popup closed a moment ago, so that the click outside
     * that closed it, on the panel button, does not open it again.
     */
    bool justHidden() const;

signals:
    void volumeChanged(int percent);
    void muteToggled(bool muted);
    void mixerRequested();

protected:
    virtual void showEvent(QShowEvent *e);
    virtual void hideEvent(QHideEvent *e);
    virtual void keyPressEvent(QKeyEvent *e);
    virtual void wheelEvent(QWheelEvent *e);

private slots:
    void sliderMoved(int value);
    void sliderPressed();
    void sliderReleased();
    void muteClicked(bool on);

private:
    void showVolume(int percent);

    QLabel *m_label;
    QSlider *m_slider;
    QCheckBox *m_mute;
    QPushButton *m_mixer;

    bool m_updating;
    bool m_dragging;
    int m_volume;
    QTime m_hideTime;
};

#endif
