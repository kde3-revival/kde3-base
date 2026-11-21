/*
 * Shows MonitorCanvas with a few monitors, to drag them around by hand;
 * prints the layout after every move.
 *
 *   canvastest [n]      n monitors (default 3)
 */

#include "monitorcanvas.h"
#include "randrcore.h"

#include <qapplication.h>

#include <stdio.h>
#include <stdlib.h>

using namespace KRandR;

class Printer : public QObject
{
    Q_OBJECT
public:
    Printer( Layout *l ) : m_layout( l ) {}
public slots:
    void print() {
	for ( Layout::ConstIterator it = m_layout->begin(); it != m_layout->end(); ++it ) {
	    int w, h;
	    settingSize( *it, w, h );
	    printf( "%s %dx%d+%d+%d  ", (*it).name.latin1(), w, h, (*it).x, (*it).y );
	}
	printf( "%s\n", validateLayout( *m_layout ) ? "ok" : "INVALID" );
	fflush( stdout );
    }
private:
    Layout *m_layout;
};

int main( int argc, char **argv )
{
    QApplication app( argc, argv );
    int n = argc > 1 ? atoi( argv[1] ) : 3;
    Layout l;
    const char *names[] = { "eDP-1", "DP-5", "DP-4", "HDMI-1" };
    const int sizes[][2] = { { 1920, 1200 }, { 2560, 1440 }, { 2560, 1440 }, { 1280, 1024 } };
    int x = 0;
    for ( int i = 0; i < n && i < 4; ++i ) {
	OutputSetting s;
	s.key = s.name = names[i];
	s.model = i ? "LS27B61x" : "";
	s.enabled = true;
	s.width = sizes[i][0];
	s.height = sizes[i][1];
	s.x = x;
	x += s.width;
	s.primary = ( i == 1 );
	l.append( s );
    }
    MonitorCanvas c;
    Printer p( &l );
    QObject::connect( &c, SIGNAL( moved() ), &p, SLOT( print() ) );
    c.setMonitors( &l );
    c.setCurrent( 1 );
    c.resize( 600, 300 );
    c.move( 0, 0 );
    app.setMainWidget( &c );
    c.show();
    p.print();
    return app.exec();
}

#include "canvastest.moc"
