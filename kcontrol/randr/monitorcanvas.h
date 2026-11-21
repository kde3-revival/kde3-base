/*
 * The monitors of a layout as rectangles that can be dragged around.
 *
 * Copyright (C) 2026 Serghei Amelian
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#ifndef MONITORCANVAS_H
#define MONITORCANVAS_H

#include <qwidget.h>

#include "randrcore.h"

class MonitorCanvas : public QWidget
{
    Q_OBJECT

public:
    MonitorCanvas( QWidget *parent = 0, const char *name = 0 );

    // the layout is edited in place; not owned
    void setMonitors( KRandR::Layout *layout );
    void setCurrent( int index );
    int current() const { return m_current; }

    // the rectangle of an enabled monitor of the layout
    static QRect monitorRect( const KRandR::OutputSetting &s );

    virtual QSize sizeHint() const;
    virtual QSize minimumSizeHint() const;

signals:
    void selected( int index );
    void moved();

protected:
    virtual void paintEvent( QPaintEvent * );
    virtual void resizeEvent( QResizeEvent * );
    virtual void mousePressEvent( QMouseEvent * );
    virtual void mouseMoveEvent( QMouseEvent * );
    virtual void mouseReleaseEvent( QMouseEvent * );

private:
    void computeTransform();
    QRect toWidget( const QRect &r ) const;
    int monitorAt( const QPoint &p ) const;

    KRandR::Layout *m_layout;
    int m_current;
    int m_drag;             // index being dragged, -1
    QPoint m_dragStart;     // widget position of the press
    QPoint m_dragOrigin;    // layout position of the monitor at the press
    bool m_dragged;
    double m_scale;
    QPoint m_offset;        // widget position of layout 0,0
};

#endif
