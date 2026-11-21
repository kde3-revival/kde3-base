/*
 * windows.h
 *
 * Copyright (c) 1997 Patrick Dowler dowler@morgul.fsh.uvic.ca
 * Copyright (c) 2001 Waldo Bastian bastian@kde.org
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#ifndef __KWINDOWCONFIG_H__
#define __KWINDOWCONFIG_H__

#include <qwidget.h>
#include <kcmodule.h>
#include <config.h>

class QRadioButton;
class QCheckBox;
class QPushButton;
class QComboBox;
class QGroupBox;
class QLabel;
class QSlider;
class QButtonGroup;
class QSpinBox;
class QVButtonGroup;
class QTabWidget;

class KColorButton;
class KIntNumInput;

#define TRANSPARENT 0
#define OPAQUE      1

#define CLICK_TO_FOCUS     0
#define FOCUS_FOLLOW_MOUSE 1

#define TITLEBAR_PLAIN  0
#define TITLEBAR_SHADED 1

#define RESIZE_TRANSPARENT  0
#define RESIZE_OPAQUE       1

#define SMART_PLACEMENT        0
#define MAXIMIZING_PLACEMENT   1
#define CASCADE_PLACEMENT      2
#define RANDOM_PLACEMENT       3
#define CENTERED_PLACEMENT     4
#define ZEROCORNERED_PLACEMENT 5
#define INTERACTIVE_PLACEMENT  6
#define MANUAL_PLACEMENT       7

#define  CLICK_TO_FOCUS               0
#define  FOCUS_FOLLOWS_MOUSE          1
#define  FOCUS_UNDER_MOUSE            2
#define  FOCUS_STRICTLY_UNDER_MOUSE   3

class QSpinBox;

class KFocusConfig : public KCModule
{
  Q_OBJECT
public:
  KFocusConfig( bool _standAlone, KConfig *_config, QWidget *parent=0, const char* name=0 );
  ~KFocusConfig();

  void load();
  void save();
  void defaults();

private slots:
  void setDelayFocusEnabled();
  void setAutoRaiseEnabled();
  void autoRaiseOnTog(bool);//CT 23Oct1998
  void delayFocusOnTog(bool);
  void clickRaiseOnTog(bool);
  void updateAltTabMode();
	void changed() { emit KCModule::changed(true); }


private:

  int getFocus( void );
  int getAutoRaiseInterval( void );
  int getDelayFocusInterval( void );

  void setFocus(int);
  void setAutoRaiseInterval(int);
  void setAutoRaise(bool);
  void setDelayFocusInterval(int);
  void setDelayFocus(bool);
  void setClickRaise(bool);
  void setAltTabMode(bool);
  void setTraverseAll(bool);
  void setRollOverDesktops(bool);
  void setShowPopupinfo(bool);

  QButtonGroup *fcsBox;
  QComboBox *focusCombo;
  QCheckBox *autoRaiseOn;
  QCheckBox *delayFocusOn;
  QCheckBox *clickRaiseOn;
  KIntNumInput *autoRaise;
  KIntNumInput *delayFocus;

  QButtonGroup *kbdBox;
  QCheckBox    *altTabPopup;
  QCheckBox    *traverseAll;
  QCheckBox    *rollOverDesktops;
  QCheckBox    *showPopupinfo;

  KConfig *config;
  bool     standAlone;
};

class KMovingConfig : public KCModule
{
  Q_OBJECT
public:
  KMovingConfig( bool _standAlone, KConfig *config, QWidget *parent=0, const char* name=0 );
  ~KMovingConfig();

  void load();
  void save();
  void defaults();

private slots:
  void setAnimations( bool );
  void setMinimizeAnimSpeed( int );
	void changed() { emit KCModule::changed(true); }
  void slotBrdrSnapChanged( int );
  void slotWndwSnapChanged( int );

private:
  int getMove( void );
  int getMinimizeAnimSpeed( void );
  int getResizeOpaque ( void );
  bool getGeometryTip( void ); //KS
  int getPlacement( void ); //CT

  void setMove(int);
  void setResizeOpaque(int);
  void setGeometryTip(bool); //KS
  void setPlacement(int); //CT
  void setMoveResizeMaximized(bool);

  QButtonGroup *windowsBox;
  QCheckBox *opaque;
  QCheckBox *resizeOpaqueOn;
  QCheckBox *geometryTipOn;
  // the window animations ([Effects])
  QCheckBox* animationsOn;
  QLabel* minimizeAnimLabel;
  QComboBox* minimizeAnimCombo;
  QCheckBox* maximizeAnimOn;
  QCheckBox* openCloseAnimOn;
  QLabel* minimizeAnimSpeedLabel;
  QSlider *minimizeAnimSlider;
  QLabel *minimizeAnimSlowLabel, *minimizeAnimFastLabel;
  QCheckBox *moveResizeMaximized;

  QComboBox *placementCombo;

  KConfig *config;
  bool     standAlone;

  int getBorderSnapZone();
  void setBorderSnapZone( int );
  int getWindowSnapZone();
  void setWindowSnapZone( int );

  QVButtonGroup *MagicBox;
  KIntNumInput *BrdrSnap, *WndwSnap;
  QCheckBox *OverlapSnap;

};

class KAdvancedConfig : public KCModule
{
  Q_OBJECT
public:
  KAdvancedConfig( bool _standAlone, KConfig *config, QWidget *parent=0, const char* name=0 );
  ~KAdvancedConfig();

  void load();
  void save();
  void defaults();

private slots:
  void shadeHoverChanged(bool);
  void overviewCornerChanged(bool);

  //copied from kcontrol/konq/kwindesktop, aleXXX
  void setEBorders();

  void changed() { emit KCModule::changed(true); }

private:

  int getShadeHoverInterval (void );
  void setAnimateShade(bool);
  void setShadeHover(bool);
  void setShadeHoverInterval(int);

  QCheckBox *animateShade;
  QButtonGroup *shBox;
  QCheckBox *shadeHoverOn;
  KIntNumInput *shadeHover;

  KConfig *config;
  bool     standAlone;

  int getElectricBorders( void );
  int getElectricBorderDelay();
  void setElectricBorders( int );
  void setElectricBorderDelay( int );

  QVButtonGroup *electricBox;
  QRadioButton *active_disable;
  QRadioButton *active_move;
  QRadioButton *active_always;
  KIntNumInput *delays;
  
  void setFocusStealing( int );
  void setHideUtilityWindowsForInactive( bool );

  QComboBox* focusStealing;
  QCheckBox* hideUtilityWindowsForInactive;

  QCheckBox* overviewCorner;
  QComboBox* overviewCornerPosition;
  QLabel* overviewCornerPositionLabel;
  KIntNumInput* overviewCornerDelay;
  KIntNumInput* overviewAnimationTime;
  QCheckBox* overviewSuperKey;
  QCheckBox* overviewShowMinimized;
};

/*
 The compositing page (KWin composites itself, compositor.h in kwin): on or
 off, the backend, full screen windows drawn directly, the shadows and the
 window opacity rules ([Compositing] and [Translucency] in kwinrc).
*/
class KCompositingConfig : public KCModule
{
  Q_OBJECT
public:
  KCompositingConfig( bool _standAlone, KConfig *config, QWidget *parent=0, const char* name=0 );
  ~KCompositingConfig();

  void load();
  void save();
  void defaults();

private slots:
  void updateEnabled();
  void updateStatus();

private:
  // what KWin uses without a setting: on when the X server renders on a GPU
  static bool enabledByDefault();
  QCheckBox *enabled;
  QLabel *status;
  QComboBox *backend;
  QCheckBox *unredirectFullscreen;
  QTabWidget *tabs;
  QCheckBox *shadows;
  KIntNumInput *shadowSize;
  KIntNumInput *shadowOpacity;
  KIntNumInput *shadowOffsetX;
  KIntNumInput *shadowOffsetY;
  QCheckBox *activeWindowTransparency;
  QCheckBox *inactiveWindowTransparency;
  QCheckBox *movingWindowTransparency;
  QCheckBox *dockWindowTransparency;
  QCheckBox *keepAboveAsActive;
  KIntNumInput *activeWindowOpacity;
  KIntNumInput *inactiveWindowOpacity;
  KIntNumInput *movingWindowOpacity;
  KIntNumInput *dockWindowOpacity;
  KConfig *config;
  bool     standAlone;
};
#endif
