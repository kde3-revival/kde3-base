/*****************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

You can Freely distribute this program under the GNU General Public
License. See the file "COPYING" for the exact licensing terms.
******************************************************************/

#include "screendesktops.h"

namespace KWinInternal
{

ScreenDesktops::ScreenDesktops()
    : independent_( true )
    , count_( 1 )
    , active_( 0 )
    {
    ids_.append( QString::null );
    current_.append( 1 );
    }

void ScreenDesktops::setIndependent( bool on )
    {
    independent_ = on;
    if( !on )
        setCurrent( active_, activeDesktop());
    }

void ScreenDesktops::setDesktopCount( int count )
    {
    count_ = count < 1 ? 1 : count;
    for( int i = 0;
         i < int( current_.count());
         ++i )
        if( current_[ i ] > count_ )
            current_[ i ] = count_;
    for( QMap< QString, int >::Iterator it = remembered_.begin();
         it != remembered_.end();
         ++it )
        if( *it > count_ )
            *it = count_;
    }

void ScreenDesktops::setOutputs( const QStringList& ids, QStringList* lost, QStringList* added )
    {
    QStringList new_ids = ids;
    if( new_ids.isEmpty())
        new_ids.append( QString::null );
    const int active_desktop = activeDesktop();
    const QString active_id = id( active_ );
    QValueVector< int > new_current( new_ids.count(), active_desktop );
    for( int i = 0;
         i < int( new_ids.count());
         ++i )
        {
        const int old = output( new_ids[ i ] );
        if( old >= 0 )
            new_current[ i ] = current_[ old ];
        else
            {
            if( added != NULL )
                added->append( new_ids[ i ] );
            QMap< QString, int >::Iterator it = remembered_.find( new_ids[ i ] );
            if( it != remembered_.end())
                {
                if( independent_ )
                    new_current[ i ] = *it;
                remembered_.remove( it );
                }
            }
        }
    for( int i = 0;
         i < int( ids_.count());
         ++i )
        if( !new_ids.contains( ids_[ i ] ) && !ids_[ i ].isEmpty())
            {
            if( lost != NULL )
                lost->append( ids_[ i ] );
            remembered_[ ids_[ i ] ] = current_[ i ];
            }
    ids_ = new_ids;
    current_ = new_current;
    active_ = output( active_id );
    if( active_ < 0 )
        active_ = 0;
    if( !independent_ ) // (a returning monitor shows the common desktop)
        setCurrent( active_, active_desktop );
    }

QString ScreenDesktops::id( int output ) const
    {
    if( output < 0 || output >= int( ids_.count()))
        return QString::null;
    return ids_[ output ];
    }

int ScreenDesktops::output( const QString& id ) const
    {
    return ids_.findIndex( id );
    }

int ScreenDesktops::current( int output ) const
    {
    if( output < 0 || output >= int( current_.count()))
        output = active_;
    return current_[ output ];
    }

bool ScreenDesktops::setCurrent( int output, int desktop )
    {
    if( desktop < 1 || desktop > count_ )
        return false;
    bool changed = false;
    for( int i = 0;
         i < int( current_.count());
         ++i )
        if( i == output || !independent_ || output < 0 || output >= int( current_.count()))
            {
            changed = changed || current_[ i ] != desktop;
            current_[ i ] = desktop;
            }
    return changed;
    }

void ScreenDesktops::setActive( int output )
    {
    if( output >= 0 && output < int( ids_.count()))
        active_ = output;
    }

QString ScreenDesktops::save() const
    {
    QStringList l;
    for( int i = 0;
         i < int( ids_.count());
         ++i )
        if( !ids_[ i ].isEmpty())
            l.append( ids_[ i ] + '=' + QString::number( current_[ i ] ));
    for( QMap< QString, int >::ConstIterator it = remembered_.begin();
         it != remembered_.end();
         ++it )
        if( !it.key().isEmpty())
            l.append( it.key() + '=' + QString::number( *it ));
    return l.join( "\n" );
    }

void ScreenDesktops::restore( const QString& state )
    {
    QStringList l = QStringList::split( '\n', state );
    for( QStringList::ConstIterator it = l.begin();
         it != l.end();
         ++it )
        {
        const int eq = ( *it ).findRev( '=' );
        if( eq <= 0 )
            continue;
        const QString key = ( *it ).left( eq );
        bool ok;
        const int desktop = ( *it ).mid( eq + 1 ).toInt( &ok );
        if( !ok || desktop < 1 || desktop > count_ )
            continue;
        const int o = output( key );
        if( o >= 0 )
            {
            if( independent_ )
                current_[ o ] = desktop;
            }
        else
            remembered_[ key ] = desktop;
        }
    }

static QString rectString( const QRect& r )
    {
    return QString( "%1,%2,%3,%4" ).arg( r.x()).arg( r.y()).arg( r.width()).arg( r.height());
    }

static QRect stringRect( const QString& s )
    {
    QStringList l = QStringList::split( ',', s, true );
    if( l.count() != 4 )
        return QRect();
    return QRect( l[ 0 ].toInt(), l[ 1 ].toInt(), l[ 2 ].toInt(), l[ 3 ].toInt());
    }

// one line: fields separated by '|', the identity last (it may hold anything but a newline)
QString ScreenHome::toString() const
    {
    if( isNull())
        return QString::null;
    return rectString( geometry ) + '|' + rectString( restore ) + '|' + rectString( fs_restore )
        + '|' + QString::number( maximize ) + '|' + QString::number( fullscreen ? 1 : 0 )
        + '|' + QString::number( user_moved ? 1 : 0 ) + '|' + id;
    }

ScreenHome ScreenHome::fromString( const QString& s )
    {
    ScreenHome h;
    QStringList l = QStringList::split( '|', s, true );
    if( l.count() < 7 )
        return h;
    h.geometry = stringRect( l[ 0 ] );
    h.restore = stringRect( l[ 1 ] );
    h.fs_restore = stringRect( l[ 2 ] );
    h.maximize = l[ 3 ].toInt() & 3;
    h.fullscreen = l[ 4 ].toInt() != 0;
    h.user_moved = l[ 5 ].toInt() != 0;
    QStringList rest = l;
    for( int i = 0;
         i < 6;
         ++i )
        rest.remove( rest.begin());
    h.id = rest.join( "|" );
    if( !h.geometry.isValid())
        h.id = QString::null;
    return h;
    }

} // namespace
