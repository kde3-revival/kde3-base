/*
 * RandR 1.2+ display configuration: model, EDID, profiles, layouts, plan.
 *
 * Copyright (C) 2026 Serghei Amelian
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#include "randrcore.h"

#include <qrect.h>

#include <kconfig.h>
#include <klocale.h>

#include <math.h>
#include <stdlib.h>

namespace KRandR {

Output::Output()
    : id( 0 ), connected( false ), nonDesktop( false ), mmWidth( 0 ),
      mmHeight( 0 ), preferredCount( 0 ), crtc( 0 ), primary( false )
{
}

bool OutputSetting::operator==( const OutputSetting &o ) const
{
    return key == o.key && name == o.name && enabled == o.enabled &&
	width == o.width && height == o.height &&
	fabs( rate - o.rate ) < 0.005 && x == o.x && y == o.y &&
	rotation == o.rotation && primary == o.primary;
}

// --- ScreenState -------------------------------------------------------------

const Output *ScreenState::output( XID id ) const
{
    for ( QValueList<Output>::ConstIterator it = outputs.begin(); it != outputs.end(); ++it )
	if ( (*it).id == id )
	    return &*it;
    return 0;
}

const Output *ScreenState::outputByName( const QString &name ) const
{
    for ( QValueList<Output>::ConstIterator it = outputs.begin(); it != outputs.end(); ++it )
	if ( (*it).name == name )
	    return &*it;
    return 0;
}

const Output *ScreenState::outputByKey( const QString &key ) const
{
    if ( key.isEmpty() )
	return 0;
    for ( QValueList<Output>::ConstIterator it = outputs.begin(); it != outputs.end(); ++it )
	if ( (*it).connected && (*it).key == key )
	    return &*it;
    return 0;
}

const Crtc *ScreenState::crtc( XID id ) const
{
    for ( QValueList<Crtc>::ConstIterator it = crtcs.begin(); it != crtcs.end(); ++it )
	if ( (*it).id == id )
	    return &*it;
    return 0;
}

const Mode *ScreenState::mode( XID id ) const
{
    QMap<XID, Mode>::ConstIterator it = modes.find( id );
    return it == modes.end() ? 0 : &*it;
}

QValueList<Output> ScreenState::connectedOutputs() const
{
    QValueList<Output> l;
    for ( QValueList<Output>::ConstIterator it = outputs.begin(); it != outputs.end(); ++it )
	if ( (*it).connected && !(*it).nonDesktop && !(*it).modes.isEmpty() &&
	     !(*it).crtcs.isEmpty() )
	    l.append( *it );
    return l;
}

QString ScreenState::connectedSetKey() const
{
    QStringList keys;
    QValueList<Output> l = connectedOutputs();
    for ( QValueList<Output>::ConstIterator it = l.begin(); it != l.end(); ++it )
	keys.append( (*it).key );
    keys.sort();
    return keys.join( "+" );
}

// --- EDID --------------------------------------------------------------------

static QString edidText( const unsigned char *d )
{
    // 13 characters, ended by a line feed, padded with spaces
    QCString s;
    for ( int i = 0; i < 13 && d[i] != 0x0a && d[i] != 0; ++i )
	s += ( d[i] >= 0x20 && d[i] < 0x7f ) ? (char) d[i] : '_';
    return QString::fromLatin1( s ).stripWhiteSpace();
}

EdidInfo parseEdid( const unsigned char *d, int len )
{
    static const unsigned char header[8] = { 0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0 };
    EdidInfo e;
    if ( !d || len < 128 )
	return e;
    for ( int i = 0; i < 8; ++i )
	if ( d[i] != header[i] )
	    return e;

    unsigned int id = ( d[8] << 8 ) | d[9];
    char v[4];
    v[0] = '@' + ( ( id >> 10 ) & 0x1f );
    v[1] = '@' + ( ( id >> 5 ) & 0x1f );
    v[2] = '@' + ( id & 0x1f );
    v[3] = 0;
    for ( int i = 0; i < 3; ++i )
	if ( v[i] < 'A' || v[i] > 'Z' )
	    v[i] = '_';
    e.vendor = QString::fromLatin1( v );
    e.product = d[10] | ( d[11] << 8 );
    e.serialNumber = d[12] | ( d[13] << 8 ) | ( d[14] << 16 ) | ( (unsigned int) d[15] << 24 );

    for ( int off = 54; off <= 108; off += 18 ) {
	const unsigned char *b = d + off;
	if ( b[0] != 0 || b[1] != 0 || b[2] != 0 )
	    continue;   // detailed timing
	if ( b[3] == 0xfc )
	    e.name = edidText( b + 5 );
	else if ( b[3] == 0xff )
	    e.serial = edidText( b + 5 );
    }
    e.valid = true;
    return e;
}

static QString sanitize( const QString &s )
{
    QString r;
    for ( unsigned int i = 0; i < s.length(); ++i ) {
	QChar c = s[i];
	if ( ( c >= 'A' && c <= 'Z' ) || ( c >= 'a' && c <= 'z' ) ||
	     ( c >= '0' && c <= '9' ) || c == '.' || c == '-' || c == '_' )
	    r += c;
	else
	    r += '_';
    }
    return r;
}

QString edidKey( const EdidInfo &e, const QString &connector )
{
    if ( !e.valid )
	return QString::fromLatin1( "connector-" ) + sanitize( connector );
    QString key = sanitize( e.vendor ) + QString().sprintf( "-%04x", e.product );
    if ( !e.serial.isEmpty() )
	key += "-" + sanitize( e.serial );
    else if ( e.serialNumber != 0 && e.serialNumber != 0x01010101 )
	key += QString().sprintf( "-%08x", e.serialNumber );
    return key;
}

void assignKeys( ScreenState &state, const QMap<XID, EdidInfo> &edids )
{
    QMap<QString, int> count;
    for ( QValueList<Output>::Iterator it = state.outputs.begin(); it != state.outputs.end(); ++it ) {
	QMap<XID, EdidInfo>::ConstIterator e = edids.find( (*it).id );
	EdidInfo info = e == edids.end() ? EdidInfo() : *e;
	(*it).key = edidKey( info, (*it).name );
	(*it).vendor = info.vendor;
	(*it).model = info.name;
	(*it).serial = info.serial;
	if ( (*it).connected )
	    count[ (*it).key ]++;
    }
    // the same monitor model without a serial twice: tell them apart
    // by their connector
    for ( QValueList<Output>::Iterator it = state.outputs.begin(); it != state.outputs.end(); ++it )
	if ( (*it).connected && count[ (*it).key ] > 1 )
	    (*it).key += "@" + sanitize( (*it).name );
}

// --- modes and layouts -------------------------------------------------------

bool isInternalPanel( const QString &c )
{
    QString n = c.lower();
    return n.startsWith( "edp" ) || n.startsWith( "lvds" ) || n.startsWith( "dsi" ) ||
	n.startsWith( "panel" );
}

bool connectorLessThan( const QString &a, const QString &b )
{
    unsigned int i = 0, j = 0;
    while ( i < a.length() && j < b.length() ) {
	if ( a[i].isDigit() && b[j].isDigit() ) {
	    unsigned int i2 = i, j2 = j;
	    while ( i2 < a.length() && a[i2].isDigit() ) ++i2;
	    while ( j2 < b.length() && b[j2].isDigit() ) ++j2;
	    long na = a.mid( i, i2 - i ).toLong();
	    long nb = b.mid( j, j2 - j ).toLong();
	    if ( na != nb )
		return na < nb;
	    i = i2;
	    j = j2;
	} else {
	    if ( a[i] != b[j] )
		return a[i] < b[j];
	    ++i;
	    ++j;
	}
    }
    return a.length() - i < b.length() - j;
}

const Mode *preferredMode( const ScreenState &state, const Output &o )
{
    if ( o.preferredCount > 0 && !o.modes.isEmpty() ) {
	const Mode *m = state.mode( o.modes.first() );
	if ( m )
	    return m;
    }
    const Mode *best = 0;
    for ( QValueList<XID>::ConstIterator it = o.modes.begin(); it != o.modes.end(); ++it ) {
	const Mode *m = state.mode( *it );
	if ( !m )
	    continue;
	if ( !best || m->width * m->height > best->width * best->height ||
	     ( m->width * m->height == best->width * best->height && m->rate > best->rate ) )
	    best = m;
    }
    return best;
}

const Mode *findMode( const ScreenState &state, const Output &o,
		      int width, int height, double rate )
{
    if ( width <= 0 || height <= 0 )
	return preferredMode( state, o );
    const Mode *best = 0;
    for ( QValueList<XID>::ConstIterator it = o.modes.begin(); it != o.modes.end(); ++it ) {
	const Mode *m = state.mode( *it );
	if ( !m || m->width != width || m->height != height )
	    continue;
	if ( !best ) {
	    best = m;
	} else if ( rate <= 0 ) {
	    if ( m->rate > best->rate + 0.005 )
		best = m;
	} else if ( fabs( m->rate - rate ) < fabs( best->rate - rate ) - 0.005 ) {
	    best = m;
	}
    }
    return best;
}

static bool rotated( int rotation )
{
    return rotation & ( Rotate90 | Rotate270 );
}

void settingSize( const OutputSetting &s, int &w, int &h )
{
    w = rotated( s.rotation ) ? s.height : s.width;
    h = rotated( s.rotation ) ? s.width : s.height;
}

static XID currentPrimary( const ScreenState &state )
{
    for ( QValueList<Output>::ConstIterator it = state.outputs.begin(); it != state.outputs.end(); ++it )
	if ( (*it).primary )
	    return (*it).id;
    return 0;
}

Layout currentLayout( const ScreenState &state )
{
    Layout l;
    QValueList<Output> outs = state.connectedOutputs();
    for ( QValueList<Output>::ConstIterator it = outs.begin(); it != outs.end(); ++it ) {
	const Output &o = *it;
	OutputSetting s;
	s.key = o.key;
	s.name = o.name;
	s.model = o.model;
	s.primary = o.primary;
	const Crtc *c = o.crtc ? state.crtc( o.crtc ) : 0;
	const Mode *m = c && c->mode ? state.mode( c->mode ) : 0;
	if ( m ) {
	    s.enabled = true;
	    s.width = m->width;
	    s.height = m->height;
	    s.rate = m->rate;
	    s.x = c->x;
	    s.y = c->y;
	    s.rotation = c->rotation & ( RotationMask | ReflectMask );
	} else {
	    const Mode *p = preferredMode( state, o );
	    if ( p ) {
		s.width = p->width;
		s.height = p->height;
		s.rate = p->rate;
	    }
	}
	l.append( s );
    }
    return l;
}

Layout defaultLayout( const ScreenState &state )
{
    QValueList<Output> outs = state.connectedOutputs();

    // enabled ones by their current position, then the others
    QValueList<Output> on, off;
    for ( QValueList<Output>::ConstIterator it = outs.begin(); it != outs.end(); ++it ) {
	const Crtc *c = (*it).crtc ? state.crtc( (*it).crtc ) : 0;
	if ( c && c->mode )
	    on.append( *it );
	else
	    off.append( *it );
    }
    // insertion sorts: a handful of outputs
    QValueList<Output> order;
    for ( QValueList<Output>::ConstIterator it = on.begin(); it != on.end(); ++it ) {
	const Crtc *c = state.crtc( (*it).crtc );
	QValueList<Output>::Iterator pos = order.begin();
	for ( ; pos != order.end(); ++pos ) {
	    const Crtc *pc = state.crtc( (*pos).crtc );
	    if ( c->x < pc->x || ( c->x == pc->x && c->y < pc->y ) ||
		 ( c->x == pc->x && c->y == pc->y && connectorLessThan( (*it).name, (*pos).name ) ) )
		break;
	}
	order.insert( pos, *it );
    }
    QValueList<Output> rest;
    for ( QValueList<Output>::ConstIterator it = off.begin(); it != off.end(); ++it ) {
	QValueList<Output>::Iterator pos = rest.begin();
	for ( ; pos != rest.end(); ++pos ) {
	    bool ai = isInternalPanel( (*it).name ), bi = isInternalPanel( (*pos).name );
	    if ( ( ai && !bi ) || ( ai == bi && connectorLessThan( (*it).name, (*pos).name ) ) )
		break;
	}
	rest.insert( pos, *it );
    }
    order += rest;

    XID primary = currentPrimary( state );
    bool havePrimary = false;
    for ( QValueList<Output>::ConstIterator it = order.begin(); it != order.end(); ++it )
	if ( (*it).id == primary )
	    havePrimary = true;

    Layout l;
    int x = 0;
    int crtcs = state.crtcs.count();
    for ( QValueList<Output>::ConstIterator it = order.begin(); it != order.end(); ++it ) {
	OutputSetting s;
	s.key = (*it).key;
	s.name = (*it).name;
	s.model = (*it).model;
	// what is on keeps its mode and rotation (a refresh rate or
	// portrait orientation chosen by hand)
	const Crtc *c = (*it).crtc ? state.crtc( (*it).crtc ) : 0;
	const Mode *m = c && c->mode ? state.mode( c->mode ) : 0;
	if ( m ) {
	    s.rotation = c->rotation & ( RotationMask | ReflectMask );
	} else {
	    m = preferredMode( state, *it );
	    s.rotation = Rotate0;
	}
	if ( !m )
	    continue;
	s.width = m->width;
	s.height = m->height;
	s.rate = m->rate;
	// more monitors than CRTCs: the last ones stay off
	s.enabled = ( crtcs-- > 0 );
	if ( s.enabled ) {
	    int w, h;
	    settingSize( s, w, h );
	    s.x = x;
	    s.y = 0;
	    x += w;
	}
	s.primary = false;
	l.append( s );
    }
    // the primary output if it stays on, else the first one
    bool primarySet = false;
    if ( havePrimary )
	for ( Layout::Iterator it = l.begin(); it != l.end(); ++it )
	    if ( (*it).enabled && state.outputByKey( (*it).key ) &&
		 state.outputByKey( (*it).key )->id == primary )
		primarySet = (*it).primary = true;
    if ( !primarySet )
	for ( Layout::Iterator it = l.begin(); it != l.end(); ++it )
	    if ( (*it).enabled ) {
		(*it).primary = true;
		break;
	    }
    return l;
}

void normalizeLayout( Layout &layout )
{
    bool first = true;
    int minX = 0, minY = 0;
    for ( Layout::ConstIterator it = layout.begin(); it != layout.end(); ++it ) {
	if ( !(*it).enabled )
	    continue;
	if ( first || (*it).x < minX ) minX = (*it).x;
	if ( first || (*it).y < minY ) minY = (*it).y;
	first = false;
    }
    for ( Layout::Iterator it = layout.begin(); it != layout.end(); ++it ) {
	if ( !(*it).enabled )
	    continue;
	(*it).x -= minX;
	(*it).y -= minY;
    }
}

void adjustNeighbours( Layout &layout, int index, int bx, int by, int bw, int bh )
{
    if ( index < 0 || index >= (int) layout.count() )
	return;
    int w = 0, h = 0;
    const OutputSetting &s = layout[index];
    if ( s.enabled )
	settingSize( s, w, h );
    // without a previous rectangle the monitor was off: nothing moves
    if ( bw <= 0 || bh <= 0 )
	return;
    int dx = s.enabled ? ( s.x + w ) - ( bx + bw ) : -bw;
    int dy = s.enabled ? ( s.y + h ) - ( by + bh ) : -bh;
    for ( int i = 0; i < (int) layout.count(); ++i ) {
	OutputSetting &o = layout[i];
	if ( i == index || !o.enabled )
	    continue;
	int ow, oh;
	settingSize( o, ow, oh );
	// right of it, sharing some rows
	if ( dx && o.x >= bx + bw && o.y < by + bh && o.y + oh > by )
	    o.x += dx;
	// below it, sharing some columns
	else if ( dy && o.y >= by + bh && o.x < bx + bw && o.x + ow > bx )
	    o.y += dy;
    }
}

void placeAtRight( Layout &layout, int index )
{
    if ( index < 0 || index >= (int) layout.count() )
	return;
    bool any = false;
    int right = 0, top = 0;
    for ( int i = 0; i < (int) layout.count(); ++i ) {
	const OutputSetting &o = layout[i];
	if ( i == index || !o.enabled )
	    continue;
	int w, h;
	settingSize( o, w, h );
	if ( !any || o.x + w > right )
	    right = o.x + w;
	if ( !any || o.y < top )
	    top = o.y;
	any = true;
    }
    layout[index].x = any ? right : 0;
    layout[index].y = any ? top : 0;
}

QRect monitorRect( const OutputSetting &s )
{
    int w, h;
    settingSize( s, w, h );
    return QRect( s.x, s.y, QMAX( w, 1 ), QMAX( h, 1 ) );
}

// --- snapping ----------------------------------------------------------------

static bool overlapsOthers( const Layout &layout, int index, const QRect &r )
{
    for ( int j = 0; j < (int) layout.count(); ++j ) {
	if ( j == index || !layout[j].enabled )
	    continue;
	if ( monitorRect( layout[j] ).intersects( r ) )
	    return true;
    }
    return false;
}

static int clampInt( int v, int lo, int hi )
{
    return v < lo ? lo : v > hi ? hi : v;
}

// along an edge: the position closest to v that keeps some common edge
// with the other monitor (oStart, oSize), aligned with the start or end
// of any monitor (edges: their starts and ends on this axis) when one
// is closer than threshold
static int alongEdge( int v, int size, int oStart, int oSize,
		      const QValueList<int> &edges, int threshold )
{
    int lo = oStart - size + 1, hi = oStart + oSize - 1;
    int p = clampInt( v, lo, hi );
    int best = p, bestDist = threshold + 1;
    for ( QValueList<int>::ConstIterator it = edges.begin(); it != edges.end(); ++it ) {
	// start at the edge, or end at it
	int c[2] = { *it, *it - size };
	for ( int k = 0; k < 2; ++k ) {
	    if ( c[k] < lo || c[k] > hi )
		continue;
	    int d = abs( c[k] - p );
	    if ( d < bestDist ) {
		best = c[k];
		bestDist = d;
	    }
	}
    }
    return best;
}

void snapMonitor( Layout &layout, int index, int threshold )
{
    if ( index < 0 || index >= (int) layout.count() || !layout[index].enabled )
	return;
    OutputSetting &s = layout[index];
    QRect r = monitorRect( s );
    int w = r.width(), h = r.height();

    QValueList<int> xEdges, yEdges;
    for ( int j = 0; j < (int) layout.count(); ++j ) {
	if ( j == index || !layout[j].enabled )
	    continue;
	QRect o = monitorRect( layout[j] );
	xEdges << o.x() << o.x() + o.width();
	yEdges << o.y() << o.y() + o.height();
    }

    bool found = false;
    QPoint best;
    long bestDist = 0;
    bool others = false;
    for ( int j = 0; j < (int) layout.count(); ++j ) {
	if ( j == index || !layout[j].enabled )
	    continue;
	others = true;
	QRect o = monitorRect( layout[j] );
	QPoint cand[4];
	int ay = alongEdge( r.y(), h, o.y(), o.height(), yEdges, threshold );
	int ax = alongEdge( r.x(), w, o.x(), o.width(), xEdges, threshold );
	cand[0] = QPoint( o.x() + o.width(), ay );
	cand[1] = QPoint( o.x() - w, ay );
	cand[2] = QPoint( ax, o.y() + o.height() );
	cand[3] = QPoint( ax, o.y() - h );
	for ( int c = 0; c < 4; ++c ) {
	    QRect nr( cand[c], QSize( w, h ) );
	    if ( overlapsOthers( layout, index, nr ) )
		continue;
	    long dist = labs( (long) cand[c].x() - r.x() ) + labs( (long) cand[c].y() - r.y() );
	    if ( !found || dist < bestDist ) {
		found = true;
		best = cand[c];
		bestDist = dist;
	    }
	}
    }
    if ( !others ) {
	s.x = 0;
	s.y = 0;
	return;
    }
    if ( !found ) {
	// all sides taken: to the right of everything
	QRect bounds;
	for ( int j = 0; j < (int) layout.count(); ++j )
	    if ( j != index && layout[j].enabled )
		bounds |= monitorRect( layout[j] );
	best = QPoint( bounds.x() + bounds.width(), bounds.y() );
    }
    s.x = best.x();
    s.y = best.y();
}

bool validateLayout( const Layout &layout, QString *error )
{
    QValueList<QRect> rects;
    QStringList names;
    for ( Layout::ConstIterator it = layout.begin(); it != layout.end(); ++it ) {
	if ( !(*it).enabled )
	    continue;
	int w, h;
	settingSize( *it, w, h );
	QRect r( (*it).x, (*it).y, w, h );
	for ( unsigned int i = 0; i < rects.count(); ++i ) {
	    if ( rects[i].intersects( r ) ) {
		if ( error )
		    *error = i18n( "The monitors %1 and %2 overlap." )
			.arg( names[i] ).arg( (*it).name );
		return false;
	    }
	}
	rects.append( r );
	names.append( (*it).name );
    }
    if ( rects.isEmpty() ) {
	if ( error )
	    *error = i18n( "At least one monitor must be enabled." );
	return false;
    }
    return true;
}

// --- plan --------------------------------------------------------------------

namespace {
struct Want {
    Want() : output( 0 ), mode( 0 ), x( 0 ), y( 0 ), w( 0 ), h( 0 ),
	     rotation( Rotate0 ), crtc( 0 ) {}
    const Output *output;
    const Mode *mode;
    int x, y, w, h;
    int rotation;
    XID crtc;
};
}

static bool crtcTaken( const QValueList<Want> &wants, XID crtc )
{
    for ( QValueList<Want>::ConstIterator it = wants.begin(); it != wants.end(); ++it )
	if ( (*it).crtc == crtc )
	    return true;
    return false;
}

Plan makePlan( const ScreenState &state, const Layout &layout )
{
    Plan p;
    QValueList<Want> wants;
    XID primary = 0;

    for ( Layout::ConstIterator it = layout.begin(); it != layout.end(); ++it ) {
	const OutputSetting &s = *it;
	const Output *o = state.outputByKey( s.key );
	if ( !o && s.key.isEmpty() )
	    o = state.outputByName( s.name );
	if ( !o || !o->connected || o->nonDesktop )
	    continue;
	if ( !s.enabled )
	    continue;
	for ( QValueList<Want>::ConstIterator w = wants.begin(); w != wants.end(); ++w ) {
	    if ( (*w).output == o ) {
		p.error = i18n( "The output %1 is configured twice." ).arg( o->name );
		return p;
	    }
	}
	Want w;
	w.output = o;
	w.mode = findMode( state, *o, s.width, s.height, s.rate );
	if ( !w.mode )
	    w.mode = preferredMode( state, *o );
	if ( !w.mode ) {
	    p.error = i18n( "The output %1 has no usable mode." ).arg( o->name );
	    return p;
	}
	w.rotation = s.rotation & RotationMask;
	if ( w.rotation != Rotate0 && w.rotation != Rotate90 &&
	     w.rotation != Rotate180 && w.rotation != Rotate270 )
	    w.rotation = Rotate0;
	w.rotation |= s.rotation & ReflectMask;
	w.w = rotated( w.rotation ) ? w.mode->height : w.mode->width;
	w.h = rotated( w.rotation ) ? w.mode->width : w.mode->height;
	w.x = s.x;
	w.y = s.y;
	wants.append( w );
	if ( s.primary && !primary )
	    primary = o->id;
    }
    if ( wants.isEmpty() ) {
	p.error = i18n( "No connected monitor would be enabled." );
	return p;
    }

    // top left at 0,0; screen = bounding box
    int minX = wants.first().x, minY = wants.first().y;
    for ( QValueList<Want>::ConstIterator it = wants.begin(); it != wants.end(); ++it ) {
	minX = QMIN( minX, (*it).x );
	minY = QMIN( minY, (*it).y );
    }
    int width = 0, height = 0;
    for ( QValueList<Want>::Iterator it = wants.begin(); it != wants.end(); ++it ) {
	(*it).x -= minX;
	(*it).y -= minY;
	width = QMAX( width, (*it).x + (*it).w );
	height = QMAX( height, (*it).y + (*it).h );
    }
    width = QMAX( width, state.minWidth );
    height = QMAX( height, state.minHeight );
    if ( ( state.maxWidth > 0 && width > state.maxWidth ) ||
	 ( state.maxHeight > 0 && height > state.maxHeight ) ) {
	p.error = i18n( "The screen would be %1x%2 pixels; at most %3x%4 are possible." )
	    .arg( width ).arg( height ).arg( state.maxWidth ).arg( state.maxHeight );
	return p;
    }

    // CRTCs: an output keeps its CRTC when it can, the others take a
    // free one (one that is off now first) that supports the rotation
    for ( QValueList<Want>::Iterator it = wants.begin(); it != wants.end(); ++it ) {
	const Output *o = (*it).output;
	const Crtc *c = o->crtc ? state.crtc( o->crtc ) : 0;
	if ( c && o->crtcs.contains( c->id ) &&
	     ( c->rotations & (*it).rotation ) == (*it).rotation &&
	     !crtcTaken( wants, c->id ) )
	    (*it).crtc = c->id;
    }
    for ( int pass = 0; pass < 2; ++pass ) {
	for ( QValueList<Want>::Iterator it = wants.begin(); it != wants.end(); ++it ) {
	    if ( (*it).crtc )
		continue;
	    const Output *o = (*it).output;
	    for ( QValueList<XID>::ConstIterator ci = o->crtcs.begin(); ci != o->crtcs.end(); ++ci ) {
		const Crtc *c = state.crtc( *ci );
		if ( !c || crtcTaken( wants, c->id ) ||
		     ( c->rotations & (*it).rotation ) != (*it).rotation )
		    continue;
		// pass 0: only CRTCs that are off and not wanted by an
		// output that has it now
		if ( pass == 0 && c->mode )
		    continue;
		(*it).crtc = c->id;
		break;
	    }
	}
    }
    for ( QValueList<Want>::ConstIterator it = wants.begin(); it != wants.end(); ++it ) {
	if ( !(*it).crtc ) {
	    p.error = i18n( "There is no free display controller (CRTC) for %1 "
			    "with this rotation." ).arg( (*it).output->name );
	    return p;
	}
    }

    // CRTCs to switch off first
    QValueList<XID> disabled;
    for ( QValueList<Crtc>::ConstIterator ci = state.crtcs.begin(); ci != state.crtcs.end(); ++ci ) {
	const Crtc &c = *ci;
	if ( !c.mode )
	    continue;
	const Want *w = 0;
	for ( QValueList<Want>::ConstIterator it = wants.begin(); it != wants.end(); ++it )
	    if ( (*it).crtc == c.id )
		w = &*it;
	bool off = !w;
	if ( w ) {
	    QValueList<XID> outs;
	    outs.append( w->output->id );
	    if ( c.outputs != outs )
		off = true;         // the output moves to/from this CRTC
	    if ( c.x + c.width > width || c.y + c.height > height )
		off = true;         // would not fit into the new screen
	}
	if ( off ) {
	    CrtcOp op;
	    op.crtc = c.id;
	    p.disable.append( op );
	    disabled.append( c.id );
	}
    }

    if ( width != state.width || height != state.height ) {
	p.resize = true;
	p.width = width;
	p.height = height;
	// keep the DPI of the screen
	if ( state.width > 0 && state.height > 0 && state.mmWidth > 0 && state.mmHeight > 0 ) {
	    p.mmWidth = (int) ( (double) width * state.mmWidth / state.width + 0.5 );
	    p.mmHeight = (int) ( (double) height * state.mmHeight / state.height + 0.5 );
	} else {
	    p.mmWidth = (int) ( width * 25.4 / 96 + 0.5 );
	    p.mmHeight = (int) ( height * 25.4 / 96 + 0.5 );
	}
    }

    for ( QValueList<Want>::ConstIterator it = wants.begin(); it != wants.end(); ++it ) {
	const Want &w = *it;
	const Crtc *c = state.crtc( w.crtc );
	QValueList<XID> outs;
	outs.append( w.output->id );
	if ( c && c->mode == w.mode->id && c->x == w.x && c->y == w.y &&
	     ( c->rotation & ( RotationMask | ReflectMask ) ) == w.rotation &&
	     c->outputs == outs && !c->transformed && !disabled.contains( c->id ) )
	    continue;       // unchanged
	CrtcOp op;
	op.crtc = w.crtc;
	op.x = w.x;
	op.y = w.y;
	op.mode = w.mode->id;
	op.rotation = w.rotation;
	op.resetTransform = c && c->transformed;
	op.outputs = outs;
	p.enable.append( op );
    }

    // a layout without a primary monitor (the state before an undo)
    // has none
    if ( primary != currentPrimary( state ) ) {
	p.primary = primary;
	p.setPrimary = true;
    }
    return p;
}

QString Plan::describe( const ScreenState &state ) const
{
    QStringList l;
    if ( !error.isEmpty() )
	return "error: " + error;
    for ( QValueList<CrtcOp>::ConstIterator it = disable.begin(); it != disable.end(); ++it )
	l.append( QString( "off crtc %1" ).arg( (*it).crtc ) );
    if ( resize )
	l.append( QString( "screen %1x%2 (%3x%4 mm)" ).arg( width ).arg( height )
		  .arg( mmWidth ).arg( mmHeight ) );
    for ( QValueList<CrtcOp>::ConstIterator it = enable.begin(); it != enable.end(); ++it ) {
	const Mode *m = state.mode( (*it).mode );
	QStringList outs;
	for ( QValueList<XID>::ConstIterator o = (*it).outputs.begin(); o != (*it).outputs.end(); ++o ) {
	    const Output *out = state.output( *o );
	    outs.append( out ? out->name : QString::number( *o ) );
	}
	l.append( QString( "crtc %1 %2 %3x%4@%5 +%6+%7 rot %8" )
		  .arg( (*it).crtc ).arg( outs.join( "," ) )
		  .arg( m ? m->width : 0 ).arg( m ? m->height : 0 )
		  .arg( m ? m->rate : 0.0, 0, 'f', 2 ).arg( (*it).x ).arg( (*it).y )
		  .arg( (*it).rotation ) );
    }
    if ( setPrimary ) {
	const Output *o = state.output( primary );
	l.append( QString( "primary %1" ).arg( o ? o->name : QString::number( primary ) ) );
    }
    return l.join( "; " );
}

// --- profiles ----------------------------------------------------------------

ProfileStore::ProfileStore( KConfig *config )
    : m_config( config )
{
}

QString ProfileStore::groupName( const QString &setKey )
{
    return QString::fromLatin1( "Profile " ) + setKey;
}

bool ProfileStore::hasProfile( const QString &setKey ) const
{
    return !setKey.isEmpty() && m_config->hasGroup( groupName( setKey ) );
}

QStringList ProfileStore::profiles() const
{
    QStringList l;
    QStringList groups = m_config->groupList();
    for ( QStringList::ConstIterator it = groups.begin(); it != groups.end(); ++it )
	if ( (*it).startsWith( "Profile " ) )
	    l.append( (*it).mid( 8 ) );
    return l;
}

static QString rotationString( int r )
{
    switch ( r ) {
    case Rotate90: return "90";
    case Rotate180: return "180";
    case Rotate270: return "270";
    default: return "0";
    }
}

static int rotationFromString( const QString &s )
{
    if ( s == "90" ) return Rotate90;
    if ( s == "180" ) return Rotate180;
    if ( s == "270" ) return Rotate270;
    return Rotate0;
}

Layout ProfileStore::profile( const QString &setKey ) const
{
    Layout l;
    if ( !hasProfile( setKey ) )
	return l;
    KConfigGroupSaver saver( m_config, groupName( setKey ) );
    QStringList keys = m_config->readListEntry( "Outputs" );
    for ( QStringList::ConstIterator it = keys.begin(); it != keys.end(); ++it ) {
	QString k = *it;
	OutputSetting s;
	s.key = k;
	s.name = m_config->readEntry( k + ":Connector" );
	s.model = m_config->readEntry( k + ":Model" );
	s.enabled = m_config->readBoolEntry( k + ":Enabled", true );
	QString mode = m_config->readEntry( k + ":Mode" );
	int x = mode.find( 'x' );
	if ( x > 0 ) {
	    s.width = mode.left( x ).toInt();
	    s.height = mode.mid( x + 1 ).toInt();
	}
	s.rate = m_config->readEntry( k + ":Rate" ).toDouble();
	QStringList pos = QStringList::split( ',', m_config->readEntry( k + ":Position", "0,0" ) );
	if ( pos.count() == 2 ) {
	    s.x = pos[0].toInt();
	    s.y = pos[1].toInt();
	}
	s.rotation = rotationFromString( m_config->readEntry( k + ":Rotation", "0" ) );
	QString reflect = m_config->readEntry( k + ":Reflect" );
	if ( reflect.contains( 'x' ) )
	    s.rotation |= ReflectX;
	if ( reflect.contains( 'y' ) )
	    s.rotation |= ReflectY;
	s.primary = m_config->readBoolEntry( k + ":Primary", false );
	l.append( s );
    }
    return l;
}

void ProfileStore::saveProfile( const QString &setKey, const Layout &layout )
{
    if ( setKey.isEmpty() )
	return;
    m_config->deleteGroup( groupName( setKey ), true );
    KConfigGroupSaver saver( m_config, groupName( setKey ) );
    QStringList keys;
    for ( Layout::ConstIterator it = layout.begin(); it != layout.end(); ++it ) {
	const OutputSetting &s = *it;
	QString k = s.key;
	keys.append( k );
	m_config->writeEntry( k + ":Connector", s.name );
	m_config->writeEntry( k + ":Model", s.model );
	m_config->writeEntry( k + ":Enabled", s.enabled );
	m_config->writeEntry( k + ":Mode", QString( "%1x%2" ).arg( s.width ).arg( s.height ) );
	m_config->writeEntry( k + ":Rate", QString::number( s.rate, 'f', 2 ) );
	m_config->writeEntry( k + ":Position", QString( "%1,%2" ).arg( s.x ).arg( s.y ) );
	m_config->writeEntry( k + ":Rotation", rotationString( s.rotation & RotationMask ) );
	QString reflect;
	if ( s.rotation & ReflectX )
	    reflect += 'x';
	if ( s.rotation & ReflectY )
	    reflect += 'y';
	if ( !reflect.isEmpty() )
	    m_config->writeEntry( k + ":Reflect", reflect );
	m_config->writeEntry( k + ":Primary", s.primary );
    }
    m_config->writeEntry( "Outputs", keys );
    m_config->sync();
}

void ProfileStore::removeProfile( const QString &setKey )
{
    m_config->deleteGroup( groupName( setKey ), true );
    m_config->sync();
}

bool autoRestoreEnabled( KConfig *config )
{
    KConfigGroupSaver saver( config, "General" );
    return config->readBoolEntry( "AutoRestore",
				  config->readBoolEntry( "ApplyOnHotplug", true ) );
}

Layout layoutFor( const ScreenState &state, const ProfileStore &store,
		  bool useDefault, bool *fromProfile )
{
    QString setKey = state.connectedSetKey();
    if ( fromProfile )
	*fromProfile = false;
    if ( store.hasProfile( setKey ) ) {
	Layout l = store.profile( setKey );
	if ( !l.isEmpty() ) {
	    if ( fromProfile )
		*fromProfile = true;
	    return l;
	}
    }
    return useDefault ? defaultLayout( state ) : Layout();
}

// --- layouts for sets without a profile ---------------------------------------

static const OutputSetting *settingFor( const Layout &l, const QString &key )
{
    for ( Layout::ConstIterator it = l.begin(); it != l.end(); ++it )
	if ( (*it).key == key )
	    return &*it;
    return 0;
}

// the number of the connected monitors (keys) that l has on
static int overlap( const Layout &l, const QStringList &keys )
{
    int n = 0;
    for ( Layout::ConstIterator it = l.begin(); it != l.end(); ++it )
	if ( (*it).enabled && keys.contains( (*it).key ) )
	    ++n;
    return n;
}

// closes the empty stripes between the enabled monitors (and before the
// first one): what a monitor that is gone leaves in a row or a column
static void closeGaps( Layout &l, bool vertical )
{
    for ( unsigned int round = 0; round <= l.count(); ++round ) {
	// the intervals, sorted by their start
	QValueList<int> starts, ends;
	for ( Layout::ConstIterator it = l.begin(); it != l.end(); ++it ) {
	    if ( !(*it).enabled )
		continue;
	    int w, h;
	    settingSize( *it, w, h );
	    int a = vertical ? (*it).y : (*it).x, b = a + ( vertical ? h : w );
	    QValueList<int>::Iterator s = starts.begin(), e = ends.begin();
	    while ( s != starts.end() && *s <= a ) {
		++s;
		++e;
	    }
	    starts.insert( s, a );
	    ends.insert( e, b );
	}
	int covered = 0, gapAt = 0, gap = 0;
	QValueList<int>::ConstIterator e = ends.begin();
	for ( QValueList<int>::ConstIterator s = starts.begin(); s != starts.end(); ++s, ++e ) {
	    if ( *s > covered ) {
		gapAt = *s;
		gap = *s - covered;
		break;
	    }
	    covered = QMAX( covered, *e );
	}
	if ( !gap )
	    return;
	for ( Layout::Iterator it = l.begin(); it != l.end(); ++it ) {
	    if ( !(*it).enabled )
		continue;
	    int &v = vertical ? (*it).y : (*it).x;
	    if ( v >= gapAt )
		v -= gap;
	}
    }
}

static bool hasEnabled( const Layout &l, const QString &key )
{
    const OutputSetting *s = settingFor( l, key );
    return s && s->enabled;
}

Layout deriveLayout( const ScreenState &state, const ProfileStore &store,
		     const Layout &previous )
{
    QValueList<Output> outs = state.connectedOutputs();
    QStringList keys;
    for ( QValueList<Output>::ConstIterator it = outs.begin(); it != outs.end(); ++it )
	keys.append( (*it).key );

    QValueList<Layout> refs;
    refs.append( previous );
    QStringList names = store.profiles();
    names.sort();
    for ( QStringList::ConstIterator it = names.begin(); it != names.end(); ++it )
	refs.append( store.profile( *it ) );
    int best = -1, bestOverlap = 0;
    for ( unsigned int i = 0; i < refs.count(); ++i ) {
	int n = overlap( refs[i], keys );
	if ( n > bestOverlap ) {
	    best = i;
	    bestOverlap = n;
	}
    }
    if ( best < 0 )
	return defaultLayout( state );
    const Layout &ref = refs[best];

    // the monitors the reference has, where it has them
    Layout l;
    QValueList<QRect> resized;
    for ( Layout::ConstIterator it = ref.begin(); it != ref.end(); ++it ) {
	if ( !(*it).enabled )
	    continue;
	const Output *o = state.outputByKey( (*it).key );
	if ( !o || !keys.contains( o->key ) )
	    continue;
	OutputSetting s = *it;
	s.name = o->name;
	s.model = o->model;
	// a mode the monitor does not have (any more): its preferred one,
	// and the monitors right of it or below it move along
	QRect before = monitorRect( s );
	const Mode *m = findMode( state, *o, s.width, s.height, s.rate );
	if ( !m )
	    m = preferredMode( state, *o );
	if ( !m )
	    continue;
	s.width = m->width;
	s.height = m->height;
	s.rate = m->rate;
	l.append( s );
	if ( monitorRect( s ).size() != before.size() )
	    resized.append( before );
	else
	    resized.append( QRect() );
    }
    for ( unsigned int i = 0; i < l.count(); ++i )
	if ( resized[i].isValid() )
	    adjustNeighbours( l, i, resized[i].x(), resized[i].y(),
			      resized[i].width(), resized[i].height() );
    normalizeLayout( l );
    closeGaps( l, false );
    closeGaps( l, true );

    // the others, at the right
    QValueList<Output> on, off;
    for ( QValueList<Output>::ConstIterator it = outs.begin(); it != outs.end(); ++it ) {
	if ( settingFor( l, (*it).key ) )
	    continue;
	const Crtc *c = (*it).crtc ? state.crtc( (*it).crtc ) : 0;
	QValueList<Output> &list = c && c->mode ? on : off;
	QValueList<Output>::Iterator pos = list.begin();
	for ( ; pos != list.end(); ++pos ) {
	    if ( c && c->mode ) {
		const Crtc *pc = state.crtc( (*pos).crtc );
		if ( c->x < pc->x || ( c->x == pc->x && c->y < pc->y ) )
		    break;
	    } else {
		bool ai = isInternalPanel( (*it).name ), bi = isInternalPanel( (*pos).name );
		if ( ( ai && !bi ) || ( ai == bi && connectorLessThan( (*it).name, (*pos).name ) ) )
		    break;
	    }
	}
	list.insert( pos, *it );
    }
    on += off;
    int crtcs = (int) state.crtcs.count() - (int) l.count();
    for ( QValueList<Output>::ConstIterator it = on.begin(); it != on.end(); ++it ) {
	const Output &o = *it;
	OutputSetting s;
	s.key = o.key;
	s.name = o.name;
	s.model = o.model;
	const Mode *m = 0;
	// what a profile has for it
	for ( QValueList<Layout>::ConstIterator r = refs.begin(); r != refs.end() && !m; ++r ) {
	    const OutputSetting *p = settingFor( *r, o.key );
	    if ( p && p->enabled && ( m = findMode( state, o, p->width, p->height, p->rate ) ) )
		s.rotation = p->rotation;
	}
	// what it has now, else its preferred mode
	const Crtc *c = o.crtc ? state.crtc( o.crtc ) : 0;
	if ( !m && c && c->mode && ( m = state.mode( c->mode ) ) )
	    s.rotation = c->rotation & ( RotationMask | ReflectMask );
	if ( !m && ( m = preferredMode( state, o ) ) )
	    s.rotation = Rotate0;
	if ( !m )
	    continue;
	s.width = m->width;
	s.height = m->height;
	s.rate = m->rate;
	// switched off in the reference or before (a closed laptop): stays off
	const OutputSetting *r = settingFor( ref, o.key ), *p = settingFor( previous, o.key );
	bool keepOff = ( r && !r->enabled ) || ( p && !p->enabled );
	s.enabled = !keepOff && ( crtcs-- > 0 );
	l.append( s );
	if ( s.enabled )
	    placeAtRight( l, l.count() - 1 );
    }

    // the primary one
    QString primary;
    for ( Layout::ConstIterator it = ref.begin(); it != ref.end() && primary.isEmpty(); ++it )
	if ( (*it).primary && hasEnabled( l, (*it).key ) )
	    primary = (*it).key;
    for ( Layout::ConstIterator it = previous.begin(); it != previous.end() && primary.isEmpty(); ++it )
	if ( (*it).primary && hasEnabled( l, (*it).key ) )
	    primary = (*it).key;
    for ( QValueList<Output>::ConstIterator it = outs.begin(); it != outs.end() && primary.isEmpty(); ++it )
	if ( (*it).primary && hasEnabled( l, (*it).key ) )
	    primary = (*it).key;
    for ( Layout::ConstIterator it = l.begin(); it != l.end() && primary.isEmpty(); ++it )
	if ( (*it).enabled )
	    primary = (*it).key;
    for ( Layout::Iterator it = l.begin(); it != l.end(); ++it )
	(*it).primary = ( (*it).key == primary );

    // not to be made from the reference: nothing rather than another order
    if ( !validateLayout( l ) )
	return Layout();
    return l;
}

bool sameArrangement( const Layout &a, const Layout &b )
{
    for ( int pass = 0; pass < 2; ++pass ) {
	const Layout &x = pass ? b : a, &y = pass ? a : b;
	for ( Layout::ConstIterator it = x.begin(); it != x.end(); ++it ) {
	    const OutputSetting *o = settingFor( y, (*it).key );
	    if ( !o || o->enabled != (*it).enabled )
		return false;
	    if ( !(*it).enabled )
		continue;
	    if ( o->x != (*it).x || o->y != (*it).y || o->width != (*it).width ||
		 o->height != (*it).height || o->rotation != (*it).rotation ||
		 o->primary != (*it).primary || fabs( o->rate - (*it).rate ) > 0.05 )
		return false;
	}
    }
    return true;
}

} // namespace KRandR
