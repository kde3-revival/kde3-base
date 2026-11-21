/*
 * Tests of the RandR configuration logic (randrcore) against a simulated
 * X server that checks what a real one does: a CRTC must fit into the
 * screen, the screen cannot shrink below an active CRTC, an output is
 * driven by one CRTC at a time, sizes within the limits.
 *
 *   randrtest            run the tests, exit status = failures
 *   randrtest -v         also print the plans
 */

#include "randrcore.h"
#include "randrx11.h"

#include <qfile.h>
#include <qrect.h>

#include <kinstance.h>
#include <ksimpleconfig.h>
#include <ktempfile.h>

#include <stdio.h>
#include <string.h>

using namespace KRandR;

static int failures = 0;
static bool verbose = false;

#define CHECK( cond ) do { if ( !( cond ) ) { \
    printf( "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond ); ++failures; } } while ( 0 )
#define CHECK_EQ( a, b ) do { if ( !( ( a ) == ( b ) ) ) { \
    printf( "FAIL %s:%d: %s == %s\n", __FILE__, __LINE__, #a, #b ); ++failures; } } while ( 0 )

// --- a simulated server ------------------------------------------------------

struct Fake
{
    ScreenState s;
    XID nextId;
    QStringList log;

    Fake() : nextId( 100 ) {
	s.minWidth = 320; s.minHeight = 200;
	s.maxWidth = 8192; s.maxHeight = 8192;
	s.mmWidth = 0; s.mmHeight = 0;
    }

    XID mode( int w, int h, double rate ) {
	for ( QMap<XID, Mode>::ConstIterator it = s.modes.begin(); it != s.modes.end(); ++it )
	    if ( (*it).width == w && (*it).height == h && (*it).rate == rate )
		return it.key();
	Mode m;
	m.id = nextId++;
	m.width = w; m.height = h; m.rate = rate;
	s.modes.insert( m.id, m );
	return m.id;
    }
    XID addCrtc( int rotations = Rotate0 | Rotate90 | Rotate180 | Rotate270 ) {
	Crtc c;
	c.id = nextId++;
	c.rotations = rotations;
	s.crtcs.append( c );
	return c.id;
    }
    // modes: "WxH@rate" list, the first one preferred
    XID addOutput( const QString &name, const QString &key, bool connected,
		   const QStringList &modes ) {
	Output o;
	o.id = nextId++;
	o.name = name;
	o.key = key.isEmpty() ? "connector-" + name : key;
	o.connected = connected;
	for ( QStringList::ConstIterator it = modes.begin(); it != modes.end(); ++it ) {
	    int x = (*it).find( 'x' ), at = (*it).find( '@' );
	    o.modes.append( mode( (*it).left( x ).toInt(), (*it).mid( x + 1, at - x - 1 ).toInt(),
				  (*it).mid( at + 1 ).toDouble() ) );
	}
	o.preferredCount = modes.isEmpty() ? 0 : 1;
	for ( QValueList<Crtc>::Iterator it = s.crtcs.begin(); it != s.crtcs.end(); ++it ) {
	    o.crtcs.append( (*it).id );
	    (*it).possible.append( o.id );
	}
	s.outputs.append( o );
	return o.id;
    }
    Output *out( XID id ) {
	for ( QValueList<Output>::Iterator it = s.outputs.begin(); it != s.outputs.end(); ++it )
	    if ( (*it).id == id ) return &*it;
	return 0;
    }
    Output *out( const QString &name ) {
	for ( QValueList<Output>::Iterator it = s.outputs.begin(); it != s.outputs.end(); ++it )
	    if ( (*it).name == name ) return &*it;
	return 0;
    }
    Crtc *crtc( XID id ) {
	for ( QValueList<Crtc>::Iterator it = s.crtcs.begin(); it != s.crtcs.end(); ++it )
	    if ( (*it).id == id ) return &*it;
	return 0;
    }

    // the server's checks
    bool setCrtc( const CrtcOp &op ) {
	Crtc *c = crtc( op.crtc );
	if ( !c ) { log.append( "BadCrtc" ); return false; }
	if ( !op.mode ) {
	    for ( QValueList<XID>::ConstIterator it = c->outputs.begin(); it != c->outputs.end(); ++it )
		out( *it )->crtc = 0;
	    c->mode = 0; c->outputs.clear(); c->width = c->height = 0;
	    log.append( QString( "off %1" ).arg( op.crtc ) );
	    return true;
	}
	const Mode *m = s.mode( op.mode );
	if ( !m || op.outputs.isEmpty() ) { log.append( "BadMatch mode" ); return false; }
	if ( !( c->rotations & op.rotation ) ) { log.append( "BadMatch rotation" ); return false; }
	bool rot = op.rotation & ( Rotate90 | Rotate270 );
	int w = rot ? m->height : m->width, h = rot ? m->width : m->height;
	if ( op.x < 0 || op.y < 0 || op.x + w > s.width || op.y + h > s.height ) {
	    log.append( QString( "BadValue crtc %1 %2x%3+%4+%5 outside %6x%7" ).arg( op.crtc )
			.arg( w ).arg( h ).arg( op.x ).arg( op.y ).arg( s.width ).arg( s.height ) );
	    return false;
	}
	for ( QValueList<XID>::ConstIterator it = op.outputs.begin(); it != op.outputs.end(); ++it ) {
	    Output *o = out( *it );
	    if ( !o || !o->crtcs.contains( op.crtc ) || !o->modes.contains( op.mode ) ) {
		log.append( "BadMatch output" ); return false;
	    }
	    if ( o->crtc && o->crtc != op.crtc ) {
		log.append( QString( "output %1 still on crtc %2" ).arg( o->name ).arg( o->crtc ) );
		return false;
	    }
	}
	for ( QValueList<XID>::ConstIterator it = c->outputs.begin(); it != c->outputs.end(); ++it )
	    out( *it )->crtc = 0;
	c->mode = op.mode; c->x = op.x; c->y = op.y; c->rotation = op.rotation;
	c->width = w; c->height = h; c->outputs = op.outputs;
	for ( QValueList<XID>::ConstIterator it = op.outputs.begin(); it != op.outputs.end(); ++it )
	    out( *it )->crtc = op.crtc;
	log.append( QString( "crtc %1 %2x%3+%4+%5" ).arg( op.crtc ).arg( w ).arg( h )
		    .arg( op.x ).arg( op.y ) );
	return true;
    }
    bool setScreenSize( int w, int h ) {
	if ( w < s.minWidth || h < s.minHeight || w > s.maxWidth || h > s.maxHeight ) {
	    log.append( "BadValue size" ); return false;
	}
	for ( QValueList<Crtc>::ConstIterator it = s.crtcs.begin(); it != s.crtcs.end(); ++it ) {
	    if ( (*it).mode && ( (*it).x + (*it).width > w || (*it).y + (*it).height > h ) ) {
		log.append( QString( "BadMatch size %1x%2: crtc %3 outside" ).arg( w ).arg( h )
			    .arg( (*it).id ) );
		return false;
	    }
	}
	s.width = w; s.height = h;
	log.append( QString( "screen %1x%2" ).arg( w ).arg( h ) );
	return true;
    }
    bool setPrimary( XID id ) {
	for ( QValueList<Output>::Iterator it = s.outputs.begin(); it != s.outputs.end(); ++it )
	    (*it).primary = ( (*it).id == id );
	log.append( "primary" );
	return true;
    }
    // directly, as the server's initial configuration
    void enable( const QString &name, int w, int h, int x, int y ) {
	Output *o = out( name );
	XID m = 0;
	for ( QValueList<XID>::ConstIterator it = o->modes.begin(); it != o->modes.end(); ++it )
	    if ( s.mode( *it )->width == w && s.mode( *it )->height == h && !m ) m = *it;
	for ( QValueList<Crtc>::Iterator it = s.crtcs.begin(); it != s.crtcs.end(); ++it ) {
	    if ( (*it).mode ) continue;
	    (*it).mode = m; (*it).x = x; (*it).y = y; (*it).width = w; (*it).height = h;
	    (*it).rotation = Rotate0; (*it).outputs.append( o->id );
	    o->crtc = (*it).id;
	    break;
	}
	s.width = QMAX( s.width, x + w );
	s.height = QMAX( s.height, y + h );
    }
};

class FakeExecutor : public PlanExecutor
{
public:
    FakeExecutor( Fake &f ) : m_f( f ) {}
    bool setCrtc( const CrtcOp &op ) { return m_f.setCrtc( op ); }
    bool setScreenSize( int w, int h, int, int ) { return m_f.setScreenSize( w, h ); }
    bool setPrimary( XID o ) { return m_f.setPrimary( o ); }
private:
    Fake &m_f;
};

// applies layout to the fake; checks that it went through and that the
// result is the layout
static bool apply( Fake &f, const Layout &layout, const char *what )
{
    Plan p = makePlan( f.s, layout );
    if ( verbose )
	printf( "  %s: %s\n", what, p.describe( f.s ).latin1() );
    FakeExecutor ex( f );
    f.log.clear();
    QString err;
    bool ok = executePlan( ex, p, &err );
    if ( !ok ) {
	printf( "FAIL %s: %s [%s]\n", what, err.latin1(), f.log.join( "; " ).latin1() );
	++failures;
	return false;
    }
    // the server now has what was asked for
    Layout now = currentLayout( f.s );
    for ( Layout::ConstIterator it = layout.begin(); it != layout.end(); ++it ) {
	bool found = false;
	for ( Layout::ConstIterator n = now.begin(); n != now.end(); ++n ) {
	    if ( (*n).key != (*it).key ) continue;
	    found = true;
	    bool same = (*n).enabled == (*it).enabled &&
		( !(*it).enabled || ( (*n).width == (*it).width && (*n).height == (*it).height &&
				      (*n).x == (*it).x && (*n).y == (*it).y &&
				      (*n).rotation == (*it).rotation &&
				      ( !(*it).primary || (*n).primary ) ) );
	    if ( !same ) {
		printf( "FAIL %s: %s is %s %dx%d+%d+%d rot %d\n", what, (*it).name.latin1(),
			(*n).enabled ? "on" : "off", (*n).width, (*n).height, (*n).x, (*n).y,
			(*n).rotation );
		++failures;
		return false;
	    }
	}
	if ( !found && (*it).enabled && f.s.outputByKey( (*it).key ) ) {
	    printf( "FAIL %s: %s missing\n", what, (*it).name.latin1() );
	    ++failures;
	    return false;
	}
    }
    // applying it again changes nothing
    Plan again = makePlan( f.s, layout );
    if ( !again.isEmpty() ) {
	printf( "FAIL %s: not idempotent: %s\n", what, again.describe( f.s ).latin1() );
	++failures;
	return false;
    }
    return true;
}

static OutputSetting setting( const Fake &f, const QString &name, int w, int h, int x, int y,
			      bool primary = false, int rotation = Rotate0 )
{
    OutputSetting s;
    const Output *o = f.s.outputByName( name );
    s.key = o->key;
    s.name = name;
    s.enabled = w > 0;
    s.width = w; s.height = h; s.x = x; s.y = y;
    s.primary = primary;
    s.rotation = rotation;
    return s;
}

static QStringList modes( const char *a, const char *b = 0, const char *c = 0 )
{
    QStringList l;
    l << a;
    if ( b ) l << b;
    if ( c ) l << c;
    return l;
}

// --- tests -------------------------------------------------------------------

static QByteArray hex( const char *s )
{
    QByteArray b( strlen( s ) / 2 );
    for ( unsigned int i = 0; i < b.size(); ++i ) {
	unsigned int v;
	sscanf( s + 2 * i, "%2x", &v );
	b[i] = (char) v;
    }
    return b;
}

// the first block of the EDID of two Samsung LS27B61x (same serial
// number field, different serial strings)
static const char *edidA =
    "00ffffffffffff004c2dea7150315a342e200104a53c22782abdb5a7534ba224"
    "0c5054a10800d1c0a9c081c0b3009500810081800101565e00a0a0a029503020"
    "350056502100001a000000fd00304b1e701f000a202020202020000000fc004c"
    "533237423631780a20202020000000ff00483454544230303034390a20200190";
static const char *edidB =
    "00ffffffffffff004c2dea7150315a342e200104a53c22782abdb5a7534ba224"
    "0c5054a10800d1c0a9c081c0b3009500810081800101565e00a0a0a029503020"
    "350056502100001a000000fd00304b1e701f000a202020202020000000fc004c"
    "533237423631780a20202020000000ff00483454544230303037300a20200196";

static void testEdid()
{
    QByteArray a = hex( edidA ), b = hex( edidB );
    EdidInfo ea = parseEdid( (const unsigned char *) a.data(), a.size() );
    EdidInfo eb = parseEdid( (const unsigned char *) b.data(), b.size() );
    CHECK( ea.valid && eb.valid );
    CHECK_EQ( ea.vendor, QString( "SAM" ) );
    CHECK_EQ( ea.product, 0x71eau );
    CHECK_EQ( ea.name, QString( "LS27B61x" ) );
    CHECK_EQ( ea.serial, QString( "H4TTB00049" ) );
    CHECK_EQ( eb.serial, QString( "H4TTB00070" ) );
    CHECK_EQ( edidKey( ea, "DP-5" ), QString( "SAM-71ea-H4TTB00049" ) );
    CHECK( edidKey( ea, "DP-5" ) != edidKey( eb, "DP-4" ) );

    // broken / missing EDID: the connector
    QByteArray bad = a.copy();
    bad[1] = 0;
    CHECK( !parseEdid( (const unsigned char *) bad.data(), bad.size() ).valid );
    CHECK( !parseEdid( 0, 0 ).valid );
    CHECK_EQ( edidKey( EdidInfo(), "HDMI-1" ), QString( "connector-HDMI-1" ) );

    // two monitors of the same model without serials: unique by connector
    QByteArray n = a.copy();
    for ( int i = 12; i < 16; ++i ) n[i] = 0;
    n[108 + 3] = 0x10;      // the serial descriptor becomes a dummy one
    EdidInfo en = parseEdid( (const unsigned char *) n.data(), n.size() );
    CHECK( en.serial.isEmpty() );
    CHECK_EQ( edidKey( en, "DP-1" ), QString( "SAM-71ea" ) );
    ScreenState st;
    Output o1, o2, o3;
    o1.id = 1; o1.name = "DP-1"; o1.connected = true;
    o2.id = 2; o2.name = "DP-2"; o2.connected = true;
    o3.id = 3; o3.name = "HDMI-1"; o3.connected = true;
    st.outputs << o1 << o2 << o3;
    QMap<XID, EdidInfo> e;
    e.insert( 1, en );
    e.insert( 2, en );
    assignKeys( st, e );
    CHECK_EQ( st.outputs[0].key, QString( "SAM-71ea@DP-1" ) );
    CHECK_EQ( st.outputs[1].key, QString( "SAM-71ea@DP-2" ) );
    CHECK_EQ( st.outputs[2].key, QString( "connector-HDMI-1" ) );
}

static void testConnectorOrder()
{
    CHECK( connectorLessThan( "DP-2", "DP-10" ) );
    CHECK( !connectorLessThan( "DP-10", "DP-2" ) );
    CHECK( connectorLessThan( "DP-1", "DP-1-1" ) );
    CHECK( connectorLessThan( "DP-4", "DP-5" ) );
    CHECK( connectorLessThan( "DP-5", "HDMI-1" ) );
    CHECK( !connectorLessThan( "DP-5", "DP-5" ) );
    CHECK( isInternalPanel( "eDP-1" ) && isInternalPanel( "LVDS1" ) && !isInternalPanel( "DP-1" ) );
}

// the user's desk: DP-5 left and primary, DP-4 right
static void setupDesk( Fake &f )
{
    f.addCrtc(); f.addCrtc(); f.addCrtc(); f.addCrtc();
    f.addOutput( "DP-4", "SAM-71ea-H4TTB00070", true,
		 modes( "2560x1440@59.95", "2560x1440@74.97", "1920x1080@60" ) );
    f.addOutput( "DP-5", "SAM-71ea-H4TTB00049", true,
		 modes( "2560x1440@59.95", "2560x1440@74.97", "1920x1080@60" ) );
    f.addOutput( "HDMI-4", "", false, QStringList() );
}

static void testDefaultLayout()
{
    // nothing on: by connector, the first one primary
    Fake f;
    setupDesk( f );
    f.s.width = 1024; f.s.height = 768;
    Layout l = defaultLayout( f.s );
    CHECK_EQ( l.count(), 2u );
    CHECK_EQ( l[0].name, QString( "DP-4" ) );
    CHECK( l[0].primary && !l[1].primary );
    CHECK_EQ( l[1].x, 2560 );
    CHECK_EQ( l[0].rate, 59.95 );
    apply( f, l, "default from nothing" );

    // DP-5 left and primary now: kept
    Fake g;
    setupDesk( g );
    g.enable( "DP-5", 2560, 1440, 0, 0 );
    g.enable( "DP-4", 2560, 1440, 2560, 0 );
    g.out( "DP-5" )->primary = true;
    Layout d = defaultLayout( g.s );
    CHECK_EQ( d[0].name, QString( "DP-5" ) );
    CHECK( d[0].primary );
    CHECK_EQ( d[1].name, QString( "DP-4" ) );
    CHECK_EQ( d[1].x, 2560 );
    Plan p = makePlan( g.s, d );
    CHECK( p.isEmpty() );   // already like that
}

static void testPlans()
{
    // laptop: eDP alone, then a monitor at the right (screen grows)
    Fake f;
    f.addCrtc(); f.addCrtc(); f.addCrtc();
    f.addOutput( "eDP-1", "AUO-1234", true, modes( "1920x1080@60.01", "1280x720@60" ) );
    f.addOutput( "HDMI-1", "DEL-a0b1-XYZ", true, modes( "2560x1440@59.95", "1920x1080@60" ) );
    f.enable( "eDP-1", 1920, 1080, 0, 0 );
    f.out( "eDP-1" )->primary = true;
    Layout l;
    l << setting( f, "eDP-1", 1920, 1080, 0, 0, true )
      << setting( f, "HDMI-1", 2560, 1440, 1920, 0 );
    Plan p = makePlan( f.s, l );
    CHECK( p.disable.isEmpty() );
    CHECK( p.resize && p.width == 4480 && p.height == 1440 );
    CHECK_EQ( p.enable.count(), 1u );
    apply( f, l, "grow" );

    // monitor at the left instead: both move, eDP's CRTC is reconfigured
    // after the screen grew... and the screen keeps its size
    Layout left;
    left << setting( f, "HDMI-1", 2560, 1440, 0, 0 )
	 << setting( f, "eDP-1", 1920, 1080, 2560, 0, true );
    apply( f, left, "swap sides" );

    // the external monitor at a smaller mode: both CRTCs would be
    // outside the shrunk screen (3840x1080), so they are switched off
    // before it shrinks
    Layout small;
    small << setting( f, "HDMI-1", 1920, 1080, 0, 0 )
	  << setting( f, "eDP-1", 1920, 1080, 1920, 0, true );
    p = makePlan( f.s, small );
    CHECK_EQ( p.disable.count(), 2u );
    CHECK( p.resize && p.width == 3840 && p.height == 1080 );

    // a smaller mode for the eDP: only its CRTC is switched off, the
    // HDMI one stays as it is
    Layout narrow;
    narrow << setting( f, "HDMI-1", 1920, 1080, 0, 0 )
	   << setting( f, "eDP-1", 1280, 720, 1920, 0, true );
    apply( f, small, "shrink" );
    p = makePlan( f.s, narrow );
    CHECK( p.disable.count() == 1 && p.enable.count() == 1 && p.resize && p.width == 3200 );
    apply( f, narrow, "narrow" );

    // unplugged: the HDMI output disappears
    f.out( "HDMI-1" )->connected = false;
    Layout only;
    only << setting( f, "eDP-1", 1920, 1080, 0, 0, true );
    p = makePlan( f.s, only );
    CHECK( p.resize && p.width == 1920 && p.height == 1080 );
    apply( f, only, "unplugged" );

    // portrait monitor
    f.out( "HDMI-1" )->connected = true;
    Layout portrait;
    portrait << setting( f, "eDP-1", 1920, 1080, 0, 0, true )
	     << setting( f, "HDMI-1", 2560, 1440, 1920, 0, false, Rotate90 );
    p = makePlan( f.s, portrait );
    CHECK( p.height == 2560 && p.width == 1920 + 1440 );
    apply( f, portrait, "portrait" );

    // laptop lid closed: only the external monitor
    Layout lid;
    lid << setting( f, "eDP-1", 0, 0, 0, 0 )
	<< setting( f, "HDMI-1", 2560, 1440, 0, 0, true );
    apply( f, lid, "lid closed" );
    CHECK( f.out( "HDMI-1" )->primary );

    // too big
    Layout huge;
    huge << setting( f, "eDP-1", 1920, 1080, 0, 0, true )
	 << setting( f, "HDMI-1", 2560, 1440, 7000, 0 );
    p = makePlan( f.s, huge );
    CHECK( !p.error.isEmpty() );

    // nothing enabled
    Layout none;
    none << setting( f, "eDP-1", 0, 0, 0, 0 ) << setting( f, "HDMI-1", 0, 0, 0, 0 );
    CHECK( !makePlan( f.s, none ).error.isEmpty() );

    // negative positions are moved to 0,0
    Layout neg;
    neg << setting( f, "eDP-1", 1920, 1080, 0, 0, true )
	<< setting( f, "HDMI-1", 2560, 1440, -2560, -100 );
    p = makePlan( f.s, neg );
    CHECK( p.error.isEmpty() && p.width == 4480 && p.height == 1440 );
}

static void testCrtcMove()
{
    // the output's CRTC cannot rotate: it moves to another CRTC; the old
    // one must be off before the output is put on the new one
    Fake f;
    XID c1 = f.addCrtc( Rotate0 );
    f.addCrtc();
    f.addOutput( "DP-1", "A", true, modes( "1920x1080@60" ) );
    f.enable( "DP-1", 1920, 1080, 0, 0 );
    CHECK_EQ( f.out( "DP-1" )->crtc, c1 );
    Layout l;
    l << setting( f, "DP-1", 1920, 1080, 0, 0, true, Rotate270 );
    Plan p = makePlan( f.s, l );
    CHECK_EQ( p.disable.count(), 1u );
    apply( f, l, "crtc move" );
    CHECK( f.out( "DP-1" )->crtc != c1 );

    // more monitors than CRTCs
    Fake g;
    g.addCrtc();
    g.addOutput( "DP-1", "A", true, modes( "1920x1080@60" ) );
    g.addOutput( "DP-2", "B", true, modes( "1920x1080@60" ) );
    Layout two;
    two << setting( g, "DP-1", 1920, 1080, 0, 0, true ) << setting( g, "DP-2", 1920, 1080, 1920, 0 );
    CHECK( !makePlan( g.s, two ).error.isEmpty() );
}

static void testModes()
{
    Fake f;
    setupDesk( f );
    const Output *o = f.s.outputByName( "DP-4" );
    const Mode *m = findMode( f.s, *o, 2560, 1440, 75 );
    CHECK( m && m->rate == 74.97 );
    m = findMode( f.s, *o, 2560, 1440, 0 );
    CHECK( m && m->rate == 74.97 );     // highest
    m = findMode( f.s, *o, 2560, 1440, 60 );
    CHECK( m && m->rate == 59.95 );
    CHECK( !findMode( f.s, *o, 800, 600, 60 ) );
    m = findMode( f.s, *o, 0, 0, 0 );
    CHECK( m && m->width == 2560 && m->rate == 59.95 );     // preferred

    // a profile whose mode the monitor does not have any more: preferred
    Layout l;
    l << setting( f, "DP-4", 800, 600, 0, 0, true );
    Plan p = makePlan( f.s, l );
    CHECK( p.error.isEmpty() && p.width == 2560 );
}

static void testValidate()
{
    Fake f;
    setupDesk( f );
    Layout l;
    l << setting( f, "DP-4", 2560, 1440, 0, 0, true ) << setting( f, "DP-5", 2560, 1440, 2000, 0 );
    QString err;
    CHECK( !validateLayout( l, &err ) && !err.isEmpty() );
    l[1].x = 2560;
    CHECK( validateLayout( l ) );
    l[0].enabled = l[1].enabled = false;
    CHECK( !validateLayout( l ) );
}

static void testReviewCases()
{
    // reflection survives currentLayout -> plan, and a profile
    Fake f;
    f.addCrtc( Rotate0 | Rotate90 | Rotate180 | Rotate270 | ReflectX | ReflectY );
    f.addCrtc();
    f.addOutput( "DP-1", "A", true, modes( "1920x1080@60", "1280x1024@60" ) );
    f.addOutput( "DP-2", "B", true, modes( "1920x1080@60", "1280x1024@75", "1280x1024@60" ) );
    f.enable( "DP-1", 1920, 1080, 0, 0 );
    f.crtc( f.out( "DP-1" )->crtc )->rotation = Rotate0 | ReflectX;
    Layout cur = currentLayout( f.s );
    CHECK_EQ( cur[0].rotation, Rotate0 | ReflectX );
    CHECK( makePlan( f.s, cur ).isEmpty() );
    {
	KTempFile tmp;
	tmp.setAutoDelete( true );
	tmp.close();
	KSimpleConfig cfg( tmp.name() );
	ProfileStore store( &cfg );
	store.saveProfile( "A+B", cur );
	CHECK_EQ( store.profile( "A+B" )[0].rotation, Rotate0 | ReflectX );
    }

    // a scaling transform left by xrandr: the CRTC is set again even if
    // nothing else changes, with the transform reset
    f.crtc( f.out( "DP-1" )->crtc )->transformed = true;
    Plan p = makePlan( f.s, cur );
    CHECK( p.enable.count() == 1 && p.enable.first().resetTransform );

    // no primary in the layout (an undo to a state without one): none
    f.out( "DP-1" )->primary = true;
    Layout noPrimary = cur;
    noPrimary[0].primary = false;
    p = makePlan( f.s, noPrimary );
    CHECK( p.setPrimary && p.primary == 0 );

    // the default keeps the mode and rotation of what is on
    f.crtc( f.out( "DP-1" )->crtc )->transformed = false;
    Layout custom;
    custom << setting( f, "DP-1", 1920, 1080, 0, 0, true, Rotate90 );
    apply( f, custom, "portrait" );
    Layout d = defaultLayout( f.s );
    CHECK( d[0].name == "DP-1" && d[0].rotation == Rotate90 && d[0].width == 1920 );
    CHECK( d[1].name == "DP-2" && d[1].x == 1080 && d[1].enabled );
    apply( f, d, "default keeps portrait" );

    // more monitors than CRTCs: the default leaves the last ones off
    Fake g;
    g.addCrtc();
    g.addOutput( "DP-1", "A", true, modes( "1920x1080@60" ) );
    g.addOutput( "DP-2", "B", true, modes( "1920x1080@60" ) );
    Layout two = defaultLayout( g.s );
    CHECK( two.count() == 2 && two[0].enabled && !two[1].enabled && two[0].primary );
    apply( g, two, "too few CRTCs" );
}

static Layout row( int n, int w, int h )
{
    Layout l;
    for ( int i = 0; i < n; ++i ) {
	OutputSetting s;
	s.key = s.name = QString( "M%1" ).arg( i );
	s.enabled = true;
	s.width = w; s.height = h;
	s.x = i * w;
	s.primary = ( i == 0 );
	l.append( s );
    }
    return l;
}

static void testEditing()
{
    // dropped somewhere above the right half of the left monitor: on top
    // of it, aligned to its right edge (within the threshold)
    Layout l = row( 2, 1920, 1080 );
    l[1].x = 10; l[1].y = -900;
    snapMonitor( l, 1, 50 );
    CHECK( l[1].x == 0 && l[1].y == -1080 );
    normalizeLayout( l );
    CHECK( l[0].x == 0 && l[0].y == 1080 && l[1].y == 0 );
    CHECK( validateLayout( l ) );

    // dropped onto the other one: moved to the nearest free side (below)
    l = row( 2, 1920, 1080 );
    l[1].x = 300; l[1].y = 20;
    snapMonitor( l, 1, 50 );
    CHECK( validateLayout( l ) );
    CHECK( l[1].x == 300 && l[1].y == 1080 );
    // more to the right: at the right side, top aligned
    l[1].x = 1500; l[1].y = 20;
    snapMonitor( l, 1, 50 );
    CHECK( l[1].x == 1920 && l[1].y == 0 );

    // dropped left of it, a bit lower: stays lower (no alignment)
    l = row( 2, 1920, 1080 );
    l[1].x = -2500; l[1].y = 400;
    snapMonitor( l, 1, 50 );
    CHECK( l[1].x == -1920 && l[1].y == 400 );

    // under the second one, nearly at the left edge of the first one:
    // aligned with that edge
    l = row( 3, 2560, 1440 );
    l[0].width = 1920; l[0].height = 1200;
    l[1].x = 1920; l[2].x = 1920 + 2560;
    l[2].x = -136; l[2].y = 1500;
    snapMonitor( l, 2, 300 );
    CHECK( l[2].x == 0 && l[2].y == 1440 );
    CHECK( validateLayout( l ) );

    // far away: still edge to edge
    l = row( 3, 1000, 800 );
    l[2].x = 5000; l[2].y = 3000;
    snapMonitor( l, 2, 30 );
    CHECK( validateLayout( l ) );
    bool touches = false;
    for ( int i = 0; i < 2; ++i ) {
	QRect a = monitorRect( l[i] ), b = monitorRect( l[2] );
	if ( a.x() + a.width() == b.x() || b.x() + b.width() == a.x() ||
	     a.y() + a.height() == b.y() || b.y() + b.height() == a.y() )
	    touches = true;
    }
    CHECK( touches );

    // the middle one gets bigger: the right one moves along
    l = row( 3, 1000, 800 );
    QRect before = monitorRect( l[1] );
    l[1].width = 1600; l[1].height = 900;
    adjustNeighbours( l, 1, before.x(), before.y(), before.width(), before.height() );
    CHECK( l[2].x == 2600 && l[0].x == 0 );
    CHECK( validateLayout( l ) );
    // rotated: narrower
    before = monitorRect( l[1] );
    l[1].rotation = Rotate90;
    adjustNeighbours( l, 1, before.x(), before.y(), before.width(), before.height() );
    CHECK( l[2].x == 1000 + 900 );
    // switched off: the gap closes
    before = monitorRect( l[1] );
    l[1].enabled = false;
    adjustNeighbours( l, 1, before.x(), before.y(), before.width(), before.height() );
    CHECK( l[2].x == 1000 );
    // on again: at the right
    l[1].enabled = true;
    placeAtRight( l, 1 );
    CHECK( l[1].x == 2000 && l[1].y == 0 );
    CHECK( validateLayout( l ) );

    // a stack: the lower one follows the upper one's bottom edge
    l = row( 2, 1000, 800 );
    l[1].x = 0; l[1].y = 800;
    before = monitorRect( l[0] );
    l[0].height = 1000;
    adjustNeighbours( l, 0, before.x(), before.y(), before.width(), before.height() );
    CHECK( l[1].y == 1000 );
}

static void testProfiles()
{
    KTempFile tmp;
    tmp.setAutoDelete( true );
    tmp.close();
    Fake f;
    setupDesk( f );
    QString set = f.s.connectedSetKey();
    CHECK_EQ( set, QString( "SAM-71ea-H4TTB00049+SAM-71ea-H4TTB00070" ) );

    Layout l;
    l << setting( f, "DP-5", 2560, 1440, 0, 0, true )
      << setting( f, "DP-4", 2560, 1440, 2560, 0 );
    l[0].rate = 74.97;
    l[0].model = "LS27B61x";
    {
	KSimpleConfig cfg( tmp.name() );
	ProfileStore store( &cfg );
	CHECK( !store.hasProfile( set ) );
	bool fromProfile = true;
	Layout d = layoutFor( f.s, store, true, &fromProfile );
	CHECK( !fromProfile && d.count() == 2 );
	CHECK( layoutFor( f.s, store, false ).isEmpty() );
	store.saveProfile( set, l );
    }
    {
	KSimpleConfig cfg( tmp.name() );
	ProfileStore store( &cfg );
	CHECK( store.hasProfile( set ) );
	CHECK_EQ( store.profiles().count(), 1u );
	Layout r = store.profile( set );
	CHECK( r == l );
	bool fromProfile = false;
	Layout u = layoutFor( f.s, store, true, &fromProfile );
	CHECK( fromProfile && u == l );
	apply( f, u, "profile" );
	CHECK_EQ( f.s.width, 5120 );
	CHECK( f.out( "DP-5" )->primary );
	// the connectors swapped (cables): the monitors are found by EDID
	f.out( "DP-4" )->key = "SAM-71ea-H4TTB00049";
	f.out( "DP-5" )->key = "SAM-71ea-H4TTB00070";
	CHECK_EQ( f.s.connectedSetKey(), set );
	Plan p = makePlan( f.s, u );
	CHECK( !p.isEmpty() );
	apply( f, u, "profile, cables swapped" );
	CHECK( f.out( "DP-4" )->primary );
	store.removeProfile( set );
	CHECK( !store.hasProfile( set ) );
    }
}

int main( int argc, char **argv )
{
    verbose = argc > 1 && !strcmp( argv[1], "-v" );
    KInstance instance( "randrtest" );

    testEdid();
    testConnectorOrder();
    testDefaultLayout();
    testPlans();
    testCrtcMove();
    testModes();
    testValidate();
    testEditing();
    testReviewCases();
    testProfiles();

    printf( "=== %d failure(s)\n", failures );
    return failures;
}
