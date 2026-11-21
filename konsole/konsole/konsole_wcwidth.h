/* Markus Kuhn -- 2001-01-12 -- public domain */
/* Adaptions for KDE by Waldo Bastian <bastian@kde.org> */

#ifndef	_KONSOLE_WCWIDTH_H_
#define	_KONSOLE_WCWIDTH_H_

#include <qglobal.h>
#include <qstring.h>

// Column width of the character ucs (a full code point, not UTF-16 unit).
int konsole_wcwidth(Q_UINT32 ucs);

int string_width( const QString &txt );

#endif
