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

#include "monitorcanvas.h"

#include <qpainter.h>
#include <qpixmap.h>

#include <klocale.h>

using namespace KRandR;

static const int margin = 16;

MonitorCanvas::MonitorCanvas( QWidget *parent, const char *name )
    : QWidget( parent, name, WNoAutoErase ), m_layout( 0 ), m_current( -1 ), m_drag( -1 ),
      m_dragged( false ), m_scale( 1 )
{
    setBackgroundMode( NoBackground );
    setFocusPolicy( NoFocus );
}

QSize MonitorCanvas::sizeHint() const
{
    return QSize( 480, 200 );
}

QSize MonitorCanvas::minimumSizeHint() const
{
    return QSize( 240, 100 );
}

void MonitorCanvas::setMonitors( Layout *layout )
{
    m_layout = layout;
    m_drag = -1;
    computeTransform();
    update();
}

void MonitorCanvas::setCurrent( int index )
{
    m_current = index;
    update();
}

QRect MonitorCanvas::monitorRect( const OutputSetting &s )
{
    return KRandR::monitorRect( s );
}

void MonitorCanvas::computeTransform()
{
    m_scale = 1;
    m_offset = QPoint( margin, margin );
    if ( !m_layout )
	return;
    QRect bounds;
    for ( Layout::ConstIterator it = m_layout->begin(); it != m_layout->end(); ++it )
	if ( (*it).enabled )
	    bounds |= monitorRect( *it );
    if ( bounds.isEmpty() )
	return;
    // room for dragging a monitor next to the others
    double sx = (double) ( width() - 2 * margin ) / bounds.width();
    double sy = (double) ( height() - 2 * margin ) / bounds.height();
    m_scale = QMIN( sx, sy ) * 0.8;
    if ( m_scale <= 0 )
	m_scale = 0.01;
    int w = (int) ( bounds.width() * m_scale ), h = (int) ( bounds.height() * m_scale );
    m_offset = QPoint( ( width() - w ) / 2 - (int) ( bounds.x() * m_scale ),
		       ( height() - h ) / 2 - (int) ( bounds.y() * m_scale ) );
}

QRect MonitorCanvas::toWidget( const QRect &r ) const
{
    int x1 = m_offset.x() + (int) ( r.x() * m_scale );
    int y1 = m_offset.y() + (int) ( r.y() * m_scale );
    int x2 = m_offset.x() + (int) ( ( r.x() + r.width() ) * m_scale );
    int y2 = m_offset.y() + (int) ( ( r.y() + r.height() ) * m_scale );
    return QRect( x1, y1, x2 - x1, y2 - y1 );
}

int MonitorCanvas::monitorAt( const QPoint &p ) const
{
    if ( !m_layout )
	return -1;
    // the current one is drawn last, on top
    if ( m_current >= 0 && m_current < (int) m_layout->count() &&
	 (*m_layout)[m_current].enabled &&
	 toWidget( monitorRect( (*m_layout)[m_current] ) ).contains( p ) )
	return m_current;
    for ( int i = (int) m_layout->count() - 1; i >= 0; --i )
	if ( (*m_layout)[i].enabled && toWidget( monitorRect( (*m_layout)[i] ) ).contains( p ) )
	    return i;
    return -1;
}

void MonitorCanvas::resizeEvent( QResizeEvent * )
{
    if ( m_drag < 0 )
	computeTransform();
}

void MonitorCanvas::paintEvent( QPaintEvent * )
{
    QPixmap buffer( size() );
    QPainter p( &buffer );
    const QColorGroup &cg = colorGroup();
    p.fillRect( rect(), cg.mid() );

    if ( m_layout ) {
	QValueList<int> order;
	for ( int i = 0; i < (int) m_layout->count(); ++i )
	    if ( i != m_current )
		order.append( i );
	if ( m_current >= 0 && m_current < (int) m_layout->count() )
	    order.append( m_current );

	QFont bold = font();
	bold.setBold( true );
	for ( QValueList<int>::ConstIterator it = order.begin(); it != order.end(); ++it ) {
	    const OutputSetting &s = (*m_layout)[*it];
	    if ( !s.enabled )
		continue;
	    QRect r = toWidget( monitorRect( s ) );
	    bool cur = ( *it == m_current );
	    p.setPen( QPen( cur ? cg.highlight().dark( 130 ) : cg.dark(), 2 ) );
	    p.setBrush( cur ? cg.highlight() : cg.button() );
	    p.drawRect( r );
	    p.setPen( cur ? cg.highlightedText() : cg.buttonText() );

	    QString size = QString( "%1x%2" ).arg( s.width ).arg( s.height );
	    QString text = s.model.isEmpty() ? size : s.model + "\n" + size;
	    if ( s.primary )
		text += "\n" + i18n( "Primary" );
	    QRect inner = r;
	    inner.addCoords( 4, 4, -4, -4 );
	    QFontMetrics bfm( bold );
	    p.setFont( bold );
	    p.drawText( inner, AlignHCenter | AlignTop, s.name );
	    inner.setTop( inner.top() + bfm.height() );
	    p.setFont( font() );
	    p.drawText( inner, AlignHCenter | AlignVCenter | WordBreak, text );
	}
    }
    p.end();
    bitBlt( this, 0, 0, &buffer );
}

void MonitorCanvas::mousePressEvent( QMouseEvent *e )
{
    if ( e->button() != LeftButton )
	return;
    int i = monitorAt( e->pos() );
    if ( i < 0 )
	return;
    if ( i != m_current ) {
	m_current = i;
	emit selected( i );
	update();
    }
    m_drag = i;
    m_dragged = false;
    m_dragStart = e->pos();
    m_dragOrigin = QPoint( (*m_layout)[i].x, (*m_layout)[i].y );
}

void MonitorCanvas::mouseMoveEvent( QMouseEvent *e )
{
    if ( m_drag < 0 || !m_layout )
	return;
    QPoint d = e->pos() - m_dragStart;
    if ( !m_dragged && d.manhattanLength() < 3 )
	return;
    m_dragged = true;
    OutputSetting &s = (*m_layout)[m_drag];
    s.x = m_dragOrigin.x() + (int) ( d.x() / m_scale );
    s.y = m_dragOrigin.y() + (int) ( d.y() / m_scale );
    update();
}

void MonitorCanvas::mouseReleaseEvent( QMouseEvent *e )
{
    if ( e->button() != LeftButton || m_drag < 0 || !m_layout )
	return;
    int i = m_drag;
    m_drag = -1;
    if ( !m_dragged )
	return;
    snapMonitor( *m_layout, i, (int) ( 24 / m_scale ) );
    normalizeLayout( *m_layout );
    computeTransform();
    update();
    // also when it snapped back: the others may have moved with the
    // normalization
    emit moved();
}

#include "monitorcanvas.moc"
