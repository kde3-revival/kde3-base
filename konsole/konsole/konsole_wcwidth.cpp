/*
 * This is an implementation of wcwidth() and wcswidth() as defined in
 * "The Single UNIX Specification, Version 2, The Open Group, 1997"
 * <http://www.UNIX-systems.org/online.html>
 *
 * Markus Kuhn -- 2001-01-12 -- public domain
 *
 * The tables are generated from the Unicode Character Database by
 * gen_wcwidth.py, which also documents which characters get which width.
 */

#include "konsole_wcwidth.h"

struct interval {
  Q_UINT32 first;
  Q_UINT32 last;
};

#include "konsole_wcwidth_table.h"

/* auxiliary function for binary search in interval table */
static bool bisearch(Q_UINT32 ucs, const struct interval *table, int max) {
  int min = 0;
  int mid;

  if (ucs < table[0].first || ucs > table[max].last)
    return false;
  while (max >= min) {
    mid = (min + max) / 2;
    if (ucs > table[mid].last)
      min = mid + 1;
    else if (ucs < table[mid].first)
      max = mid - 1;
    else
      return true;
  }

  return false;
}

/* Column width of the ISO 10646 character ucs:
 *
 *    - The null character (U+0000) has a column width of 0.
 *
 *    - Other C0/C1 control characters and DEL will lead to a return
 *      value of -1.
 *
 *    - Everything else is looked up in the generated tables: 0 for
 *      combining and format characters, 2 for East Asian Wide/Fullwidth
 *      characters and emoji, 1 for the rest (see gen_wcwidth.py).
 */

int konsole_wcwidth(Q_UINT32 ucs)
{
  /* test for 8-bit control characters */
  if (ucs == 0)
    return 0;
  if (ucs < 32 || (ucs >= 0x7f && ucs < 0xa0))
    return -1;
  if (ucs < 0x300)
    return 1;

  if (bisearch(ucs, zero_width,
               sizeof(zero_width) / sizeof(struct interval) - 1))
    return 0;

  if (bisearch(ucs, double_width,
               sizeof(double_width) / sizeof(struct interval) - 1))
    return 2;

  return 1;
}

// Column width of a string; surrogate pairs count as one character.
int string_width( const QString &txt )
{
  int w = 0;
  for ( uint i = 0; i < txt.length(); ++i )
  {
    Q_UINT32 ucs = txt[ i ].unicode();
    if ( ucs >= 0xd800 && ucs < 0xdc00 && i + 1 < txt.length() )
    {
      Q_UINT32 low = txt[ i + 1 ].unicode();
      if ( low >= 0xdc00 && low < 0xe000 )
      {
        ucs = 0x10000 + ( ( ucs - 0xd800 ) << 10 ) + ( low - 0xdc00 );
        ++i;
      }
    }
    w += konsole_wcwidth( ucs );
  }
  return w;
}
