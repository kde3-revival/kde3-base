/*
 screendesktopstest: the state of "Desktops switch per monitor"
 (ScreenDesktops, ScreenHome) without X.  Prints FAIL lines, exits 1 on any.
*/

#include "../screendesktops.h"

#include <stdio.h>

using namespace KWinInternal;

static int failures = 0;

static void check( bool ok, const char* what, int line )
    {
    if( !ok )
        {
        printf( "FAIL line %d: %s\n", line, what );
        ++failures;
        }
    }
#define CHECK( x ) check( ( x ), #x, __LINE__ )

int main()
    {
    ScreenDesktops sd;
    sd.setDesktopCount( 4 );
    QStringList ids;
    ids << "edid:A" << "edid:B";
    QStringList lost, added;
    sd.setOutputs( ids, &lost, &added );
    CHECK( sd.outputCount() == 2 );
    CHECK( lost.isEmpty()); // (the placeholder before the first monitors isn't a monitor)
    sd.setCurrent( -1, 1 );
    // independent switching
    CHECK( sd.setCurrent( 1, 3 ));
    CHECK( sd.current( 0 ) == 1 && sd.current( 1 ) == 3 );
    CHECK( !sd.setCurrent( 1, 3 ));
    CHECK( !sd.setCurrent( 1, 5 )); // out of range
    CHECK( sd.current( 1 ) == 3 );
    sd.setActive( 1 );
    CHECK( sd.activeDesktop() == 3 );
    CHECK( sd.current( 7 ) == 3 ); // out of range: the active one's
    // the monitors renumbered (primary changed): desktops follow the identity
    QStringList swapped;
    swapped << "edid:B" << "edid:A";
    lost.clear(); added.clear();
    sd.setOutputs( swapped, &lost, &added );
    CHECK( lost.isEmpty() && added.isEmpty());
    CHECK( sd.current( 0 ) == 3 && sd.current( 1 ) == 1 );
    CHECK( sd.active() == 0 ); // still B
    // B goes away: A keeps its desktop, B's is remembered
    QStringList onlyA;
    onlyA << "edid:A";
    lost.clear(); added.clear();
    sd.setOutputs( onlyA, &lost, &added );
    CHECK( lost.count() == 1 && lost.first() == "edid:B" );
    CHECK( sd.outputCount() == 1 && sd.current( 0 ) == 1 );
    CHECK( sd.active() == 0 );
    sd.setCurrent( 0, 2 );
    // B comes back: shows its desktop again
    lost.clear(); added.clear();
    sd.setOutputs( ids, &lost, &added );
    CHECK( added.count() == 1 && added.first() == "edid:B" );
    CHECK( sd.current( sd.output( "edid:A" )) == 2 );
    CHECK( sd.current( sd.output( "edid:B" )) == 3 );
    // a new monitor shows the active desktop
    QStringList three = ids;
    three << "output:HDMI-1";
    sd.setActive( sd.output( "edid:A" ));
    sd.setOutputs( three, NULL, NULL );
    CHECK( sd.current( 2 ) == 2 );
    // fewer desktops clamp
    sd.setDesktopCount( 2 );
    CHECK( sd.current( sd.output( "edid:B" )) == 2 );
    sd.setDesktopCount( 4 );
    sd.setCurrent( sd.output( "edid:B" ), 4 );
    // session: save and restore
    QString saved = sd.save();
    ScreenDesktops sd2;
    sd2.setDesktopCount( 4 );
    sd2.setOutputs( three, NULL, NULL );
    sd2.setCurrent( -1, 1 );
    sd2.restore( saved );
    CHECK( sd2.current( sd2.output( "edid:B" )) == 4 );
    CHECK( sd2.current( sd2.output( "edid:A" )) == 2 );
    // classic mode: all together
    sd2.setActive( sd2.output( "edid:B" ));
    sd2.setIndependent( false );
    CHECK( sd2.current( 0 ) == 4 && sd2.current( 1 ) == 4 && sd2.current( 2 ) == 4 );
    CHECK( sd2.setCurrent( 1, 3 ));
    CHECK( sd2.current( 0 ) == 3 && sd2.current( 2 ) == 3 );
    // classic: a monitor back shows the common desktop
    QStringList two;
    two << "edid:A" << "output:HDMI-1";
    sd2.setOutputs( two, NULL, NULL );
    sd2.setCurrent( -1, 1 );
    sd2.setOutputs( three, NULL, NULL );
    CHECK( sd2.current( sd2.output( "edid:B" )) == 1 );
    // an identity made unambiguous keeps its desktop, present or away
    ScreenDesktops sd3;
    sd3.setDesktopCount( 4 );
    QStringList one;
    one << "edid:E" << "edid:F";
    sd3.setOutputs( one, NULL, NULL );
    sd3.setCurrent( 0, 3 );
    sd3.rename( "edid:E", "edid:E@DP-1" );
    CHECK( sd3.output( "edid:E@DP-1" ) == 0 && sd3.current( 0 ) == 3 && sd3.output( "edid:E" ) < 0 );
    QStringList onlyF;
    onlyF << "edid:F";
    sd3.setOutputs( onlyF, NULL, NULL );
    sd3.rename( "edid:E@DP-1", "edid:E@DP-2" );
    QStringList back;
    back << "edid:E@DP-2" << "edid:F";
    sd3.setOutputs( back, NULL, NULL );
    CHECK( sd3.current( 0 ) == 3 );
    // ScreenHome round trip, an identity with '|'
    ScreenHome h;
    h.id = "edid:00ff|x:SER 1";
    h.geometry = QRect( 10, 20, 300, 400 );
    h.restore = QRect( -5, 6, 70, 80 );
    h.maximize = 3;
    h.fullscreen = true;
    ScreenHome h2 = ScreenHome::fromString( h.toString());
    CHECK( h2.id == h.id && h2.geometry == h.geometry && h2.restore == h.restore );
    CHECK( h2.fs_restore.isNull() && h2.maximize == 3 && h2.fullscreen && !h2.user_moved );
    CHECK( ScreenHome::fromString( "garbage" ).isNull());
    CHECK( ScreenHome().toString().isNull());
    // malformed or stale homes are refused (no home at all)
    CHECK( ScreenHome::fromString( "x,0,10,10|0,0,0,0|0,0,0,0|0|0|0|id" ).isNull());
    CHECK( ScreenHome::fromString( "0,0,0,10|0,0,0,0|0,0,0,0|0|0|0|id" ).isNull()); // empty frame
    CHECK( ScreenHome::fromString( "0,0,10,10|0,0,0,0|0,0,0,0|7|0|0|id" ).isNull()); // maximize mode
    CHECK( ScreenHome::fromString( "0,0,10,10|0,0,0,0|0,0,0,0|0|2|0|id" ).isNull());
    CHECK( ScreenHome::fromString( "0,0,99999,10|0,0,0,0|0,0,0,0|0|0|0|id" ).isNull());
    CHECK( ScreenHome::fromString( "0,0,10,10|1,2|0,0,0,0|0|0|0|id" ).isNull());
    CHECK( !ScreenHome::fromString( "5,6,10,10|0,0,0,0|0,0,0,0|1|0|1|id" ).isNull());
    printf( "%s (%d failures)\n", failures == 0 ? "PASS" : "FAIL", failures );
    return failures == 0 ? 0 : 1;
    }
