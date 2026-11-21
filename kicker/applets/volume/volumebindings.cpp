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

// The volume applet's global shortcuts, KMix's names, included by the
// applet and by kcontrol/keys (with NOSLOTS defined), which lists them
// and writes them to the "Global Shortcuts" of kdeglobals.

#ifndef NOSLOTS
# define DEF( name, label, key, fnSlot ) \
   keys->insert( name, label, QString::null, key, key, this, SLOT(fnSlot) )
#else
# define DEF( name, label, key, fnSlot ) \
   keys->insert( name, label, QString::null, key, key )
#endif

	keys->insert( "Program:volume", i18n("Volume Control") );
	DEF( "Increase volume", i18n("Increase Volume"), Qt::Key_VolumeUp, increaseVolume() );
	DEF( "Decrease volume", i18n("Decrease Volume"), Qt::Key_VolumeDown, decreaseVolume() );
	DEF( "Toggle mute", i18n("Toggle Mute"), Qt::Key_VolumeMute, toggleMute() );

#undef DEF
