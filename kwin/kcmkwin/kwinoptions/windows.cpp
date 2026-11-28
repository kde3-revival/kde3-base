/*
 * windows.cpp
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
 *
 *
 */

#include <config.h>

#include <qdir.h>
#include <qlayout.h>
#include <qgroupbox.h>
#include <qslider.h>
#include <qwhatsthis.h>
#include <qvbuttongroup.h>
#include <qcheckbox.h>
#include <qradiobutton.h>
#include <qlabel.h>
#include <qcombobox.h>
#include <kmessagebox.h>

#include <kactivelabel.h>
#include <klocale.h>
#include <kcolorbutton.h>
#include <kconfig.h>
#include <knuminput.h>
#include <kapplication.h>
#include <kdialog.h>
#include <dcopclient.h>
#include <kglobal.h>
#include <kprocess.h>
#include <qtabwidget.h>
#include <qstylesheet.h>
#include <qtimer.h>

#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include "windows.h"


// kwin config keywords
#define KWIN_FOCUS                 "FocusPolicy"
#define KWIN_PLACEMENT             "Placement"
#define KWIN_MOVE                  "MoveMode"
#define KWIN_MINIMIZE_ANIM         "AnimateMinimize"
#define KWIN_MINIMIZE_ANIM_SPEED   "AnimateMinimizeSpeed"
#define KWIN_RESIZE_OPAQUE         "ResizeMode"
#define KWIN_GEOMETRY              "GeometryTip"
#define KWIN_AUTORAISE_INTERVAL    "AutoRaiseInterval"
#define KWIN_AUTORAISE             "AutoRaise"
#define KWIN_DELAYFOCUS_INTERVAL   "DelayFocusInterval"
#define KWIN_DELAYFOCUS            "DelayFocus"
#define KWIN_CLICKRAISE            "ClickRaise"
#define KWIN_ANIMSHADE             "AnimateShade"
#define KWIN_MOVE_RESIZE_MAXIMIZED "MoveResizeMaximizedWindows"
#define KWIN_ALTTABMODE            "AltTabStyle"
#define KWIN_TRAVERSE_ALL          "TraverseAll"
#define KWIN_SHOW_POPUP            "ShowPopup"
#define KWIN_ROLL_OVER_DESKTOPS    "RollOverDesktops"
#define KWIN_SHADEHOVER            "ShadeHover"
#define KWIN_SHADEHOVER_INTERVAL   "ShadeHoverInterval"
#define KWIN_FOCUS_STEALING        "FocusStealingPreventionLevel"
#define KWIN_HIDE_UTILITY          "HideUtilityWindowsForInactive"

// kwm config keywords
#define KWM_ELECTRIC_BORDER                  "ElectricBorders"
#define KWM_ELECTRIC_BORDER_DELAY            "ElectricBorderDelay"

//CT 15mar 98 - magics
#define KWM_BRDR_SNAP_ZONE                   "BorderSnapZone"
#define KWM_BRDR_SNAP_ZONE_DEFAULT           10
#define KWM_WNDW_SNAP_ZONE                   "WindowSnapZone"
#define KWM_WNDW_SNAP_ZONE_DEFAULT           10

#define MAX_BRDR_SNAP                          100
#define MAX_WNDW_SNAP                          100
#define MAX_EDGE_RES                          1000


KFocusConfig::~KFocusConfig ()
{
    if (standAlone)
        delete config;
}

// removed the LCD display over the slider - this is not good GUI design :) RNolden 051701
KFocusConfig::KFocusConfig (bool _standAlone, KConfig *_config, QWidget * parent, const char *)
    : KCModule(parent, "kcmkwm"), config(_config), standAlone(_standAlone)
{
    QString wtstr;
    QBoxLayout *lay = new QVBoxLayout (this, 0, KDialog::spacingHint());

    //iTLabel = new QLabel(i18n("  Allowed overlap:\n"
    //                         "(% of desktop space)"),
    //             plcBox);
    //iTLabel->setAlignment(AlignTop|AlignHCenter);
    //pLay->addWidget(iTLabel,1,1);

    //interactiveTrigger = new QSpinBox(0, 500, 1, plcBox);
    //pLay->addWidget(interactiveTrigger,1,2);

    //pLay->addRowSpacing(2,KDialog::spacingHint());

    //lay->addWidget(plcBox);

    // focus policy
    fcsBox = new QButtonGroup(i18n("Focus"),this);
    fcsBox->setColumnLayout( 0, Qt::Horizontal );

    QBoxLayout *fLay = new QVBoxLayout(fcsBox->layout(),
        KDialog::spacingHint());

    QBoxLayout *cLay = new QHBoxLayout(fLay);
    QLabel *fLabel = new QLabel(i18n("&Policy:"), fcsBox);
    cLay->addWidget(fLabel, 0);
    focusCombo =  new QComboBox(false, fcsBox);
    focusCombo->insertItem(i18n("Click to Focus"), CLICK_TO_FOCUS);
    focusCombo->insertItem(i18n("Focus Follows Mouse"), FOCUS_FOLLOWS_MOUSE);
    focusCombo->insertItem(i18n("Focus Under Mouse"), FOCUS_UNDER_MOUSE);
    focusCombo->insertItem(i18n("Focus Strictly Under Mouse"), FOCUS_STRICTLY_UNDER_MOUSE);
    cLay->addWidget(focusCombo,1 ,Qt::AlignLeft);
    fLabel->setBuddy(focusCombo);

    // FIXME, when more policies have been added to KWin
    wtstr = i18n("The focus policy is used to determine the active window, i.e."
                                      " the window you can work in. <ul>"
                                      " <li><em>Click to focus:</em> A window becomes active when you click into it."
                                      " This is the behavior you might know from other operating systems.</li>"
                                      " <li><em>Focus follows mouse:</em> Moving the mouse pointer actively on to a"
                                      " normal window activates it. New windows will receive the focus,"
                                      " without you having to point the mouse at them explicitly."
                                      " Very practical if you are using the mouse a lot.</li>"
                                      " <li><em>Focus under mouse:</em> The window that happens to be under the"
                                      " mouse pointer is active. If the mouse points nowhere, the last window"
                                      " that was under the mouse has focus."
                                      " New windows will not automatically receive the focus.</li>"
                                      " <li><em>Focus strictly under mouse:</em> Only the window under the mouse pointer is"
                                      " active. If the mouse points nowhere, nothing has focus."
                                      " </ul>"
                                      "Note that 'Focus under mouse' and 'Focus strictly under mouse' prevent certain"
                                      " features such as the Alt+Tab walk through windows dialog in the KDE mode"
                                      " from working properly."
                         );
    QWhatsThis::add( focusCombo, wtstr);
    QWhatsThis::add(fLabel, wtstr);

    connect(focusCombo, SIGNAL(activated(int)), this, SLOT(setAutoRaiseEnabled()) );

    // autoraise delay
    autoRaiseOn = new QCheckBox(i18n("Auto &raise"), fcsBox);
    fLay->addWidget(autoRaiseOn);
    connect(autoRaiseOn,SIGNAL(toggled(bool)), this, SLOT(autoRaiseOnTog(bool)));

    autoRaise = new KIntNumInput(500, fcsBox);
    autoRaise->setLabel(i18n("Dela&y:"), Qt::AlignVCenter|Qt::AlignLeft);
    autoRaise->setRange(0, 3000, 100, true);
    autoRaise->setSteps(100,100);
    autoRaise->setSuffix(i18n(" msec"));
    fLay->addWidget(autoRaise);

    connect(focusCombo, SIGNAL(activated(int)), this, SLOT(setDelayFocusEnabled()) );

    delayFocusOn = new QCheckBox(i18n("Delay focus"), fcsBox);
    fLay->addWidget(delayFocusOn);
    connect(delayFocusOn,SIGNAL(toggled(bool)), this, SLOT(delayFocusOnTog(bool)));

    delayFocus = new KIntNumInput(500, fcsBox);
    delayFocus->setLabel(i18n("Dela&y:"), Qt::AlignVCenter|Qt::AlignLeft);
    delayFocus->setRange(0, 3000, 100, true);
    delayFocus->setSteps(100,100);
    delayFocus->setSuffix(i18n(" msec"));
    fLay->addWidget(delayFocus);

    clickRaiseOn = new QCheckBox(i18n("C&lick raise active window"), fcsBox);
    connect(clickRaiseOn,SIGNAL(toggled(bool)), this, SLOT(clickRaiseOnTog(bool)));
    fLay->addWidget(clickRaiseOn);

//     fLay->addColSpacing(0,QMAX(autoRaiseOn->sizeHint().width(),
//                                clickRaiseOn->sizeHint().width()) + 15);

    QWhatsThis::add( autoRaiseOn, i18n("When this option is enabled, a window in the background will automatically"
                                       " come to the front when the mouse pointer has been over it for some time.") );
    wtstr = i18n("This is the delay after which the window that the mouse pointer is over will automatically"
                 " come to the front.");
    QWhatsThis::add( autoRaise, wtstr );

    QWhatsThis::add( clickRaiseOn, i18n("When this option is enabled, the active window will be brought to the"
                                        " front when you click somewhere into the window contents. To change"
                                        " it for inactive windows, you need to change the settings"
                                        " in the Actions tab.") );

    QWhatsThis::add( delayFocusOn, i18n("When this option is enabled, there will be a delay after which the"
                                        " window the mouse pointer is over will become active (receive focus).") );
    QWhatsThis::add( delayFocus, i18n("This is the delay after which the window the mouse pointer is over"
                                       " will automatically receive focus.") );

    lay->addWidget(fcsBox);

    kbdBox = new QButtonGroup(i18n("Navigation"), this);
    kbdBox->setColumnLayout( 0, Qt::Horizontal );
    QVBoxLayout *kLay = new QVBoxLayout(kbdBox->layout(), KDialog::spacingHint());

    altTabPopup = new QCheckBox( i18n("Show window list while switching windows"), kbdBox );
    kLay->addWidget( altTabPopup );

    wtstr = i18n("Hold down the Alt key and press the Tab key repeatedly to walk"
                 " through the windows on the current desktop (the Alt+Tab"
                 " combination can be reconfigured).\n\n"
                 "If this checkbox is checked"
                 " a popup widget is shown, displaying the icons of all windows to"
                 " walk through and the title of the currently selected one.\n\n"
                 "Otherwise, the focus is passed to a new window each time Tab"
                 " is pressed, with no popup widget.  In addition, the previously"
                 " activated window will be sent to the back in this mode.");
    QWhatsThis::add( altTabPopup, wtstr );
    connect(focusCombo, SIGNAL(activated(int)), this, SLOT(updateAltTabMode()));

    traverseAll = new QCheckBox( i18n( "&Traverse windows on all desktops" ), kbdBox );
    kLay->addWidget( traverseAll );

    wtstr = i18n( "Leave this option disabled if you want to limit walking through"
                  " windows to the current desktop." );
    QWhatsThis::add( traverseAll, wtstr );

    rollOverDesktops = new QCheckBox( i18n("Desktop navi&gation wraps around"), kbdBox );
    kLay->addWidget(rollOverDesktops);

    wtstr = i18n( "Enable this option if you want keyboard or active desktop border navigation beyond"
                  " the edge of a desktop to take you to the opposite edge of the new desktop." );
    QWhatsThis::add( rollOverDesktops, wtstr );

    showPopupinfo = new QCheckBox( i18n("Popup desktop name on desktop &switch"), kbdBox );
    kLay->addWidget(showPopupinfo);

    wtstr = i18n( "Enable this option if you wish to see the current desktop"
                  " name popup whenever the current desktop is changed." );
    QWhatsThis::add( showPopupinfo, wtstr );

    lay->addWidget(kbdBox);

    lay->addStretch();

    // Any changes goes to slotChanged()
    connect(focusCombo, SIGNAL(activated(int)), SLOT(changed()));
    connect(fcsBox, SIGNAL(clicked(int)), SLOT(changed()));
    connect(autoRaise, SIGNAL(valueChanged(int)), SLOT(changed()));
    connect(delayFocus, SIGNAL(valueChanged(int)), SLOT(changed()));
    connect(altTabPopup, SIGNAL(clicked()), SLOT(changed()));
    connect(traverseAll, SIGNAL(clicked()), SLOT(changed()));
    connect(rollOverDesktops, SIGNAL(clicked()), SLOT(changed()));
    connect(showPopupinfo, SIGNAL(clicked()), SLOT(changed()));

    load();
}


int KFocusConfig::getFocus()
{
    return focusCombo->currentItem();
}

void KFocusConfig::setFocus(int foc)
{
    focusCombo->setCurrentItem(foc);

    // this will disable/hide the auto raise delay widget if focus==click
    setAutoRaiseEnabled();
    updateAltTabMode();
}

void KFocusConfig::updateAltTabMode()
{
    // not KDE-style Alt+Tab with unreasonable focus policies
    altTabPopup->setEnabled( focusCombo->currentItem() == 0 || focusCombo->currentItem() == 1 );
}

void KFocusConfig::setAutoRaiseInterval(int tb)
{
    autoRaise->setValue(tb);
}

void KFocusConfig::setDelayFocusInterval(int tb)
{
    delayFocus->setValue(tb);
}

int KFocusConfig::getAutoRaiseInterval()
{
    return autoRaise->value();
}

int KFocusConfig::getDelayFocusInterval()
{
    return delayFocus->value();
}

void KFocusConfig::setAutoRaise(bool on)
{
    autoRaiseOn->setChecked(on);
}

void KFocusConfig::setDelayFocus(bool on)
{
    delayFocusOn->setChecked(on);
}

void KFocusConfig::setClickRaise(bool on)
{
    clickRaiseOn->setChecked(on);
}

void KFocusConfig::setAutoRaiseEnabled()
{
    // the auto raise related widgets are: autoRaise
    if ( focusCombo->currentItem() != CLICK_TO_FOCUS )
    {
        autoRaiseOn->setEnabled(true);
        autoRaiseOnTog(autoRaiseOn->isChecked());
    }
    else
    {
        autoRaiseOn->setEnabled(false);
        autoRaiseOnTog(false);
    }
}

void KFocusConfig::setDelayFocusEnabled()
{
    // the delayed focus related widgets are: delayFocus
    if ( focusCombo->currentItem() != CLICK_TO_FOCUS )
    {
        delayFocusOn->setEnabled(true);
        delayFocusOnTog(delayFocusOn->isChecked());
    }
    else
    {
        delayFocusOn->setEnabled(false);
        delayFocusOnTog(false);
    }
}

void KFocusConfig::autoRaiseOnTog(bool a) {
    autoRaise->setEnabled(a);
    clickRaiseOn->setEnabled( !a );
}

void KFocusConfig::delayFocusOnTog(bool a) {
    delayFocus->setEnabled(a);
}

void KFocusConfig::clickRaiseOnTog(bool ) {
}

void KFocusConfig::setAltTabMode(bool a) {
    altTabPopup->setChecked(a);
}

void KFocusConfig::setTraverseAll(bool a) {
    traverseAll->setChecked(a);
}

void KFocusConfig::setRollOverDesktops(bool a) {
    rollOverDesktops->setChecked(a);
}

void KFocusConfig::setShowPopupinfo(bool a) {
    showPopupinfo->setChecked(a);
}

void KFocusConfig::load( void )
{
    QString key;

    config->setGroup( "Windows" );

    key = config->readEntry(KWIN_FOCUS);
    if( key == "ClickToFocus")
        setFocus(CLICK_TO_FOCUS);
    else if( key == "FocusFollowsMouse")
        setFocus(FOCUS_FOLLOWS_MOUSE);
    else if(key == "FocusUnderMouse")
        setFocus(FOCUS_UNDER_MOUSE);
    else if(key == "FocusStrictlyUnderMouse")
        setFocus(FOCUS_STRICTLY_UNDER_MOUSE);

    int k = config->readNumEntry(KWIN_AUTORAISE_INTERVAL,750);
    setAutoRaiseInterval(k);

    k = config->readNumEntry(KWIN_DELAYFOCUS_INTERVAL,750);
    setDelayFocusInterval(k);

    key = config->readEntry(KWIN_AUTORAISE);
    setAutoRaise(key == "on");
    key = config->readEntry(KWIN_DELAYFOCUS);
    setDelayFocus(key == "on");
    key = config->readEntry(KWIN_CLICKRAISE);
    setClickRaise(key != "off");
    setAutoRaiseEnabled();      // this will disable/hide the auto raise delay widget if focus==click
    setDelayFocusEnabled();

    key = config->readEntry(KWIN_ALTTABMODE, "KDE");
    setAltTabMode(key == "KDE");

    setRollOverDesktops( config->readBoolEntry(KWIN_ROLL_OVER_DESKTOPS, true ));

    config->setGroup( "PopupInfo" );
    setShowPopupinfo( config->readBoolEntry(KWIN_SHOW_POPUP, false ));

    config->setGroup( "TabBox" );
    setTraverseAll( config->readBoolEntry(KWIN_TRAVERSE_ALL, false ));

    config->setGroup("Desktops");
    emit KCModule::changed(false);
}

void KFocusConfig::save( void )
{
    int v;

    config->setGroup( "Windows" );

    v = getFocus();
    if (v == CLICK_TO_FOCUS)
        config->writeEntry(KWIN_FOCUS,"ClickToFocus");
    else if (v == FOCUS_UNDER_MOUSE)
        config->writeEntry(KWIN_FOCUS,"FocusUnderMouse");
    else if (v == FOCUS_STRICTLY_UNDER_MOUSE)
        config->writeEntry(KWIN_FOCUS,"FocusStrictlyUnderMouse");
    else
        config->writeEntry(KWIN_FOCUS,"FocusFollowsMouse");

    v = getAutoRaiseInterval();
    if (v <0) v = 0;
    config->writeEntry(KWIN_AUTORAISE_INTERVAL,v);

    v = getDelayFocusInterval();
    if (v <0) v = 0;
    config->writeEntry(KWIN_DELAYFOCUS_INTERVAL,v);

    if (autoRaiseOn->isChecked())
        config->writeEntry(KWIN_AUTORAISE, "on");
    else
        config->writeEntry(KWIN_AUTORAISE, "off");

    if (delayFocusOn->isChecked())
        config->writeEntry(KWIN_DELAYFOCUS, "on");
    else
        config->writeEntry(KWIN_DELAYFOCUS, "off");

    if (clickRaiseOn->isChecked())
        config->writeEntry(KWIN_CLICKRAISE, "on");
    else
        config->writeEntry(KWIN_CLICKRAISE, "off");

    if (altTabPopup->isChecked())
        config->writeEntry(KWIN_ALTTABMODE, "KDE");
    else
        config->writeEntry(KWIN_ALTTABMODE, "CDE");

    config->writeEntry( KWIN_ROLL_OVER_DESKTOPS, rollOverDesktops->isChecked());

    config->setGroup( "PopupInfo" );
    config->writeEntry( KWIN_SHOW_POPUP, showPopupinfo->isChecked());

    config->setGroup( "TabBox" );
    config->writeEntry( KWIN_TRAVERSE_ALL , traverseAll->isChecked());

    config->setGroup("Desktops");

    if (standAlone)
    {
        config->sync();
        if ( !kapp->dcopClient()->isAttached() )
            kapp->dcopClient()->attach();
        kapp->dcopClient()->send("kwin*", "", "reconfigure()", "");
    }
    emit KCModule::changed(false);
}

void KFocusConfig::defaults()
{
    setAutoRaiseInterval(0);
    setDelayFocusInterval(0);
    setFocus(CLICK_TO_FOCUS);
    setAutoRaise(false);
    setDelayFocus(false);
    setClickRaise(true);
    setAltTabMode(true);
    setTraverseAll( false );
    setRollOverDesktops(true);
    setShowPopupinfo(false);
    emit KCModule::changed(true);
}

KAdvancedConfig::~KAdvancedConfig ()
{
    if (standAlone)
        delete config;
}

KAdvancedConfig::KAdvancedConfig (bool _standAlone, KConfig *_config, QWidget *parent, const char *)
    : KCModule(parent, "kcmkwm"), config(_config), standAlone(_standAlone)
{
    QString wtstr;
    QBoxLayout *lay = new QVBoxLayout (this, 0, KDialog::spacingHint());

    //iTLabel = new QLabel(i18n("  Allowed overlap:\n"
    //                         "(% of desktop space)"),
    //             plcBox);
    //iTLabel->setAlignment(AlignTop|AlignHCenter);
    //pLay->addWidget(iTLabel,1,1);

    //interactiveTrigger = new QSpinBox(0, 500, 1, plcBox);
    //pLay->addWidget(interactiveTrigger,1,2);

    //pLay->addRowSpacing(2,KDialog::spacingHint());

    //lay->addWidget(plcBox);

    shBox = new QVButtonGroup(i18n("Shading"), this);

    animateShade = new QCheckBox(i18n("Anima&te"), shBox);
    QWhatsThis::add(animateShade, i18n("Animate the action of reducing the window to its titlebar (shading)"
                                       " as well as the expansion of a shaded window") );

    shadeHoverOn = new QCheckBox(i18n("&Enable hover"), shBox);

    connect(shadeHoverOn, SIGNAL(toggled(bool)), this, SLOT(shadeHoverChanged(bool)));

    shadeHover = new KIntNumInput(500, shBox);
    shadeHover->setLabel(i18n("Dela&y:"), Qt::AlignVCenter|Qt::AlignLeft);
    shadeHover->setRange(0, 3000, 100, true);
    shadeHover->setSteps(100, 100);
    shadeHover->setSuffix(i18n(" msec"));

    QWhatsThis::add(shadeHoverOn, i18n("If Shade Hover is enabled, a shaded window will un-shade automatically "
                                       "when the mouse pointer has been over the title bar for some time."));

    wtstr = i18n("Sets the time in milliseconds before the window unshades "
                "when the mouse pointer goes over the shaded window.");
    QWhatsThis::add(shadeHover, wtstr);

    lay->addWidget(shBox);

    // Any changes goes to slotChanged()
    connect(animateShade, SIGNAL(toggled(bool)), SLOT(changed()));
    connect(shadeHoverOn, SIGNAL(toggled(bool)), SLOT(changed()));
    connect(shadeHover, SIGNAL(valueChanged(int)), SLOT(changed()));

    electricBox = new QVButtonGroup(i18n("Active Desktop Borders"), this);
    electricBox->setMargin(15);

    QWhatsThis::add( electricBox, i18n("If this option is enabled, moving the mouse to a screen border"
       " will change your desktop. This is e.g. useful if you want to drag windows from one desktop"
       " to the other.") );
    active_disable = new QRadioButton(i18n("D&isabled"), electricBox);
    active_move    = new QRadioButton(i18n("Only &when moving windows"), electricBox);
    active_always  = new QRadioButton(i18n("A&lways enabled"), electricBox);

    delays = new KIntNumInput(10, electricBox);
    delays->setRange(0, MAX_EDGE_RES, 50, true);
    delays->setSuffix(i18n(" msec"));
    delays->setLabel(i18n("Desktop &switch delay:"));
    QWhatsThis::add( delays, i18n("Here you can set a delay for switching desktops using the active"
       " borders feature. Desktops will be switched after the mouse has been pushed against a screen border"
       " for the specified number of milliseconds.") );

    connect( electricBox, SIGNAL(clicked(int)), this, SLOT(setEBorders()));

    // Any changes goes to slotChanged()
    connect(electricBox, SIGNAL(clicked(int)), SLOT(changed()));
    connect(delays, SIGNAL(valueChanged(int)), SLOT(changed()));

    lay->addWidget(electricBox);

    QGroupBox* overviewBox = new QGroupBox(i18n("Window Overview"), this);
    overviewBox->setColumnLayout(0, Qt::Vertical);
    QGridLayout* overviewLay = new QGridLayout(overviewBox->layout(), 6, 1, KDialog::spacingHint());
    overviewCorner = new QCheckBox(i18n("Show the overview when the mouse pointer &touches a screen corner"), overviewBox);
    overviewLay->addWidget(overviewCorner, 0, 0);
    overviewCornerPositionLabel = new QLabel(i18n("Co&rner:"), overviewBox);
    overviewCornerPosition = new QComboBox(overviewBox);
    overviewCornerPosition->insertItem(i18n("Top left"));
    overviewCornerPosition->insertItem(i18n("Top right"));
    overviewCornerPosition->insertItem(i18n("Bottom left"));
    overviewCornerPosition->insertItem(i18n("Bottom right"));
    overviewCornerPositionLabel->setBuddy(overviewCornerPosition);
    QHBoxLayout* cornerLay = new QHBoxLayout(KDialog::spacingHint());
    cornerLay->addWidget(overviewCornerPositionLabel);
    cornerLay->addWidget(overviewCornerPosition);
    cornerLay->addStretch();
    overviewLay->addLayout(cornerLay, 1, 0);
    overviewCornerDelay = new KIntNumInput(50, overviewBox);
    overviewCornerDelay->setRange(0, 2000, 10, true);
    overviewCornerDelay->setSuffix(i18n(" msec"));
    overviewCornerDelay->setLabel(i18n("Corner &delay:"));
    overviewLay->addWidget(overviewCornerDelay, 2, 0);
    overviewAnimationTime = new KIntNumInput(250, overviewBox);
    overviewAnimationTime->setRange(0, 2000, 10, true);
    overviewAnimationTime->setSuffix(i18n(" msec"));
    overviewAnimationTime->setLabel(i18n("A&nimation duration:"));
    overviewLay->addWidget(overviewAnimationTime, 3, 0);
    overviewSuperKey = new QCheckBox(i18n("Show the overview when the &Super (Windows) key is pressed alone"), overviewBox);
    overviewLay->addWidget(overviewSuperKey, 4, 0);
    overviewShowMinimized = new QCheckBox(i18n("Show minimi&zed windows"), overviewBox);
    overviewLay->addWidget(overviewShowMinimized, 5, 0);
    QWhatsThis::add(overviewBox, i18n("The window overview shows all windows of the current desktop"
       " side by side; click one (or choose it with the arrow keys, or type a part of its title, and"
       " press Enter) to activate it, or press Escape. It can also be shown with a keyboard shortcut"
       " (\"Show Window Overview\" in the Keyboard Shortcuts module)."));
    QWhatsThis::add(overviewCorner, i18n("If this option is enabled, moving the mouse pointer into the"
       " chosen corner of the screen shows the window overview; moving it there again hides it."
       " Nothing happens while a mouse button is pressed or when the active window is full screen."));
    QWhatsThis::add(overviewCornerDelay, i18n("How long the mouse pointer must stay in the corner"
       " before the overview is shown."));
    QWhatsThis::add(overviewSuperKey, i18n("If this option is enabled, pressing and releasing the Super"
       " key (the key with the Windows logo) without any other key shows the window overview, or hides"
       " it. Shortcuts with the Super key (Meta in KDE) still work. Disable it if the Super key"
       " alone does something else for you, e.g. switches the keyboard layout."));
    QWhatsThis::add(overviewShowMinimized, i18n("If this option is enabled, the overview shows the minimized"
       " windows too (those the taskbar shows), slightly darkened, among the others. Choosing one"
       " restores it."));
    QWhatsThis::add(overviewAnimationTime, i18n("The duration of the zoom out and zoom in animations;"
       " 0 disables them."));
    connect(overviewCorner, SIGNAL(toggled(bool)), this, SLOT(overviewCornerChanged(bool)));
    connect(overviewCorner, SIGNAL(toggled(bool)), SLOT(changed()));
    connect(overviewCornerPosition, SIGNAL(activated(int)), SLOT(changed()));
    connect(overviewCornerDelay, SIGNAL(valueChanged(int)), SLOT(changed()));
    connect(overviewAnimationTime, SIGNAL(valueChanged(int)), SLOT(changed()));
    connect(overviewSuperKey, SIGNAL(toggled(bool)), SLOT(changed()));
    connect(overviewShowMinimized, SIGNAL(toggled(bool)), SLOT(changed()));
    lay->addWidget(overviewBox);

    QHBoxLayout* focusStealingLayout = new QHBoxLayout( lay,KDialog::spacingHint());
    QLabel* focusStealingLabel = new QLabel( i18n( "Focus stealing prevention level:" ), this );
    focusStealing = new QComboBox( this );
    focusStealing->insertItem( i18n( "Focus Stealing Prevention Level", "None" ));
    focusStealing->insertItem( i18n( "Focus Stealing Prevention Level", "Low" ));
    focusStealing->insertItem( i18n( "Focus Stealing Prevention Level", "Normal" ));
    focusStealing->insertItem( i18n( "Focus Stealing Prevention Level", "High" ));
    focusStealing->insertItem( i18n( "Focus Stealing Prevention Level", "Extreme" ));
    focusStealingLabel->setBuddy( focusStealing );
    focusStealingLayout->addWidget( focusStealingLabel );
    focusStealingLayout->addWidget( focusStealing, AlignLeft );
    wtstr = i18n( "<p>This option specifies how much KWin will try to prevent unwanted focus stealing "
                  "caused by unexpected activation of new windows. (Note: This feature does not "
                  "work with the Focus Under Mouse or Focus Strictly Under Mouse focus policies.)"
                  "<ul>"
                  "<li><em>None:</em> Prevention is turned off "
                  "and new windows always become activated.</li>"
                  "<li><em>Low:</em> Prevention is enabled; when some window does not have support "
                  "for the underlying mechanism and KWin cannot reliably decide whether to "
                  "activate the window or not, it will be activated. This setting may have both "
                  "worse and better results than normal level, depending on the applications.</li>"
                  "<li><em>Normal:</em> Prevention is enabled.</li>"
                  "<li><em>High:</em> New windows get activated only if no window is currently active "
                  "or if they belong to the currently active application. This setting is probably "
                  "not really usable when not using mouse focus policy.</li>"
                  "<li><em>Extreme:</em> All windows must be explicitly activated by the user.</li>"
                  "</ul></p>"
                  "<p>Windows that are prevented from stealing focus are marked as demanding attention, "
                  "which by default means their taskbar entry will be highlighted. This can be changed "
                  "in the Notifications control module.</p>" );
    QWhatsThis::add( focusStealing, wtstr );
    QWhatsThis::add( focusStealingLabel, wtstr );
    connect(focusStealing, SIGNAL(activated(int)), SLOT(changed()));
    
    hideUtilityWindowsForInactive = new QCheckBox( i18n( "Hide utility windows for inactive applications" ), this );
    QWhatsThis::add( hideUtilityWindowsForInactive,
        i18n( "When turned on, utility windows (tool windows, torn-off menus,...) of inactive applications will be"
              " hidden and will be shown only when the application becomes active. Note that applications"
              " have to mark the windows with the proper window type for this feature to work." ));
    connect(hideUtilityWindowsForInactive, SIGNAL(toggled(bool)), SLOT(changed()));
    lay->addWidget( hideUtilityWindowsForInactive );

    lay->addStretch();
    load();

}

void KAdvancedConfig::setShadeHover(bool on) {
    shadeHoverOn->setChecked(on);
    shadeHover->setEnabled(on);
}

void KAdvancedConfig::setShadeHoverInterval(int k) {
    shadeHover->setValue(k);
}

int KAdvancedConfig::getShadeHoverInterval() {

    return shadeHover->value();
}

void KAdvancedConfig::shadeHoverChanged(bool a) {
    shadeHover->setEnabled(a);
}

void KAdvancedConfig::setAnimateShade(bool a) {
    animateShade->setChecked(a);
}

void KAdvancedConfig::setFocusStealing(int l) {
    l = KMAX( 0, KMIN( 4, l ));
    focusStealing->setCurrentItem(l);
}

void KAdvancedConfig::setHideUtilityWindowsForInactive(bool s) {
    hideUtilityWindowsForInactive->setChecked( s );
}

void KAdvancedConfig::load( void )
{
    config->setGroup( "Windows" );

    setAnimateShade(config->readBoolEntry(KWIN_ANIMSHADE, true));
    setShadeHover(config->readBoolEntry(KWIN_SHADEHOVER, false));
    setShadeHoverInterval(config->readNumEntry(KWIN_SHADEHOVER_INTERVAL, 250));

    setElectricBorders(config->readNumEntry(KWM_ELECTRIC_BORDER, 0));
    setElectricBorderDelay(config->readNumEntry(KWM_ELECTRIC_BORDER_DELAY, 150));

//    setFocusStealing( config->readNumEntry(KWIN_FOCUS_STEALING, 2 ));
    // TODO default to low for now
    setFocusStealing( config->readNumEntry(KWIN_FOCUS_STEALING, 1 ));
    setHideUtilityWindowsForInactive( config->readBoolEntry( KWIN_HIDE_UTILITY, true ));

    config->setGroup( "Overview" );
    overviewCorner->setChecked( config->readBoolEntry( "HotCorner", true ));
    QString corner = config->readEntry( "HotCornerPosition", "TopLeft" );
    overviewCornerPosition->setCurrentItem( corner == "TopRight" ? 1 : corner == "BottomLeft" ? 2
        : corner == "BottomRight" ? 3 : 0 );
    overviewCornerDelay->setValue( config->readNumEntry( "HotCornerDelay", 50 ));
    overviewAnimationTime->setValue( config->readNumEntry( "AnimationTime", 250 ));
    overviewSuperKey->setChecked( config->readBoolEntry( "SuperKey", true ));
    overviewShowMinimized->setChecked( config->readBoolEntry( "ShowMinimized", true ));
    overviewCornerChanged( overviewCorner->isChecked());

    emit KCModule::changed(false);
}

void KAdvancedConfig::save( void )
{
    int v;

    config->setGroup( "Windows" );
    config->writeEntry(KWIN_ANIMSHADE, animateShade->isChecked());
    if (shadeHoverOn->isChecked())
        config->writeEntry(KWIN_SHADEHOVER, "on");
    else
        config->writeEntry(KWIN_SHADEHOVER, "off");

    v = getShadeHoverInterval();
    if (v<0) v = 0;
    config->writeEntry(KWIN_SHADEHOVER_INTERVAL, v);

    config->writeEntry(KWM_ELECTRIC_BORDER, getElectricBorders());
    config->writeEntry(KWM_ELECTRIC_BORDER_DELAY,getElectricBorderDelay());

    config->writeEntry(KWIN_FOCUS_STEALING, focusStealing->currentItem());
    config->writeEntry(KWIN_HIDE_UTILITY, hideUtilityWindowsForInactive->isChecked());

    config->setGroup( "Overview" );
    static const char* const corners[] = { "TopLeft", "TopRight", "BottomLeft", "BottomRight" };
    config->writeEntry( "HotCorner", overviewCorner->isChecked());
    config->writeEntry( "HotCornerPosition", corners[ overviewCornerPosition->currentItem() & 3 ] );
    config->writeEntry( "HotCornerDelay", overviewCornerDelay->value());
    config->writeEntry( "AnimationTime", overviewAnimationTime->value());
    config->writeEntry( "SuperKey", overviewSuperKey->isChecked());
    config->writeEntry( "ShowMinimized", overviewShowMinimized->isChecked());
    config->setGroup( "Windows" );

    if (standAlone)
    {
        config->sync();
        if ( !kapp->dcopClient()->isAttached() )
            kapp->dcopClient()->attach();
        kapp->dcopClient()->send("kwin*", "", "reconfigure()", "");
    }
    emit KCModule::changed(false);
}

void KAdvancedConfig::defaults()
{
    setAnimateShade(true);
    setShadeHover(false);
    setShadeHoverInterval(250);
    setElectricBorders(0);
    setElectricBorderDelay(150);
//    setFocusStealing(2);
    // TODO default to low for now
    setFocusStealing(1);
    setHideUtilityWindowsForInactive( true );
    overviewCorner->setChecked( true );
    overviewCornerPosition->setCurrentItem( 0 );
    overviewCornerDelay->setValue( 50 );
    overviewAnimationTime->setValue( 250 );
    overviewSuperKey->setChecked( true );
    overviewShowMinimized->setChecked( true );
    overviewCornerChanged( true );
    emit KCModule::changed(true);
}

void KAdvancedConfig::overviewCornerChanged( bool on )
{
    overviewCornerPositionLabel->setEnabled( on );
    overviewCornerPosition->setEnabled( on );
    overviewCornerDelay->setEnabled( on );
}

void KAdvancedConfig::setEBorders()
{
    delays->setEnabled(!active_disable->isChecked());
}

int KAdvancedConfig::getElectricBorders()
{
    if (active_move->isChecked())
       return 1;
    if (active_always->isChecked())
       return 2;
    return 0;
}

int KAdvancedConfig::getElectricBorderDelay()
{
    return delays->value();
}

void KAdvancedConfig::setElectricBorders(int i){
    switch(i)
    {
      case 1: active_move->setChecked(true); break;
      case 2: active_always->setChecked(true); break;
      default: active_disable->setChecked(true); break;
    }
    setEBorders();
}

void KAdvancedConfig::setElectricBorderDelay(int delay)
{
    delays->setValue(delay);
}


KMovingConfig::~KMovingConfig ()
{
    if (standAlone)
        delete config;
}

KMovingConfig::KMovingConfig (bool _standAlone, KConfig *_config, QWidget *parent, const char *)
    : KCModule(parent, "kcmkwm"), config(_config), standAlone(_standAlone)
{
    QString wtstr;
    QBoxLayout *lay = new QVBoxLayout (this, 0, KDialog::spacingHint());

    windowsBox = new QButtonGroup(i18n("Windows"), this);
    windowsBox->setColumnLayout( 0, Qt::Horizontal );

    QBoxLayout *wLay = new QVBoxLayout (windowsBox->layout(), KDialog::spacingHint());

    QBoxLayout *bLay = new QVBoxLayout;
    wLay->addLayout(bLay);

    opaque = new QCheckBox(i18n("Di&splay content in moving windows"), windowsBox);
    bLay->addWidget(opaque);
    QWhatsThis::add( opaque, i18n("Enable this option if you want a window's content to be fully shown"
                                  " while moving it, instead of just showing a window 'skeleton'. The result may not be satisfying"
                                  " on slow machines without graphic acceleration.") );

    resizeOpaqueOn = new QCheckBox(i18n("Display content in &resizing windows"), windowsBox);
    bLay->addWidget(resizeOpaqueOn);
    QWhatsThis::add( resizeOpaqueOn, i18n("Enable this option if you want a window's content to be shown"
                                          " while resizing it, instead of just showing a window 'skeleton'. The result may not be satisfying"
                                          " on slow machines.") );

    geometryTipOn = new QCheckBox(i18n("Display window &geometry when moving or resizing"), windowsBox);
    bLay->addWidget(geometryTipOn);
    QWhatsThis::add(geometryTipOn, i18n("Enable this option if you want a window's geometry to be displayed"
                                        " while it is being moved or resized. The window position relative"
                                        " to the top-left corner of the screen is displayed together with"
                                        " its size."));

    // the window animations (drawn by KWin from the window contents, see
    // kwin/effects.cpp; they need the Composite extension)
    animationsOn = new QCheckBox(i18n("Animate &windows"), windowsBox);
    QWhatsThis::add( animationsOn, i18n("Enable this option if you want windows to be animated when they are"
                                        " minimized, restored, maximized, opened or closed. Disable it to have"
                                        " no window animations at all (reduced motion). The animations need the"
                                        " Composite extension of the X server and the window overview option"
                                        " to keep the windows redirected.") );
    bLay->addWidget(animationsOn);

    QGridLayout *rLay = new QGridLayout(5,3);
    bLay->addLayout(rLay);
    rLay->setColStretch(0,0);
    rLay->setColStretch(1,1);
    rLay->addColSpacing(0, 20);

    minimizeAnimLabel = new QLabel(i18n("Minimi&ze and restore:"), windowsBox);
    rLay->addWidget(minimizeAnimLabel,0,0);
    minimizeAnimCombo = new QComboBox(false, windowsBox);
    minimizeAnimCombo->insertItem(i18n("Scale into the taskbar"));
    minimizeAnimCombo->insertItem(i18n("Genie"));
    minimizeAnimCombo->insertItem(i18n("No animation"));
    minimizeAnimLabel->setBuddy(minimizeAnimCombo);
    rLay->addMultiCellWidget(minimizeAnimCombo,0,0,1,2);
    wtstr = i18n("How a window goes into its taskbar button when it is minimized, and comes back when it is"
                 " restored: <b>Scale</b> shrinks it into the button and fades it out, <b>Genie</b> pulls it"
                 " into the button through a narrowing funnel.");
    QWhatsThis::add( minimizeAnimLabel, wtstr );
    QWhatsThis::add( minimizeAnimCombo, wtstr );

    maximizeAnimOn = new QCheckBox(i18n("Animate ma&ximize and restore"), windowsBox);
    QWhatsThis::add( maximizeAnimOn, i18n("Enable this option if you want a window to grow smoothly to its"
                                          " maximized size and back. The application is resized once, its new"
                                          " contents fade in when it has painted them.") );
    rLay->addMultiCellWidget(maximizeAnimOn,1,1,0,2);

    openCloseAnimOn = new QCheckBox(i18n("Animate &opening and closing windows"), windowsBox);
    QWhatsThis::add( openCloseAnimOn, i18n("Enable this option if you want new windows to fade in when they"
                                           " have painted their contents, and closed windows to fade out.") );
    rLay->addMultiCellWidget(openCloseAnimOn,2,2,0,2);

    minimizeAnimSpeedLabel = new QLabel(i18n("Animation s&peed:"), windowsBox);
    rLay->addWidget(minimizeAnimSpeedLabel,3,0);
    minimizeAnimSlider = new QSlider(0,10,10,0,QSlider::Horizontal, windowsBox);
    minimizeAnimSlider->setSteps(1, 1);
    // QSlider::Below clashes with a X11/X.h #define
    #undef Below
    minimizeAnimSlider->setTickmarks(QSlider::Below);
    minimizeAnimSpeedLabel->setBuddy(minimizeAnimSlider);
    rLay->addMultiCellWidget(minimizeAnimSlider,3,3,1,2);

    connect(animationsOn, SIGNAL(toggled(bool)), this, SLOT(setAnimations(bool)));
    connect(minimizeAnimSlider, SIGNAL(valueChanged(int)), this, SLOT(setMinimizeAnimSpeed(int)));

    minimizeAnimSlowLabel= new QLabel(i18n("Slow"),windowsBox);
    minimizeAnimSlowLabel->setAlignment(Qt::AlignTop|Qt::AlignLeft);
    rLay->addWidget(minimizeAnimSlowLabel,4,1);

    minimizeAnimFastLabel= new QLabel(i18n("Fast"),windowsBox);
    minimizeAnimFastLabel->setAlignment(Qt::AlignTop|Qt::AlignRight);
    rLay->addWidget(minimizeAnimFastLabel,4,2);

    wtstr = i18n("Here you can set the speed of the window animations.");
    QWhatsThis::add( minimizeAnimSpeedLabel, wtstr );
    QWhatsThis::add( minimizeAnimSlider, wtstr );
    QWhatsThis::add( minimizeAnimSlowLabel, wtstr );
    QWhatsThis::add( minimizeAnimFastLabel, wtstr );

    moveResizeMaximized = new QCheckBox( i18n("Allow moving and resizing o&f maximized windows"), windowsBox);
    bLay->addWidget(moveResizeMaximized);
    QWhatsThis::add(moveResizeMaximized, i18n("When enabled, this feature activates the border of maximized windows"
                                              " and allows you to move or resize them,"
                                              " just like for normal windows"));

    QBoxLayout *vLay = new QHBoxLayout(bLay);

    QLabel *plcLabel = new QLabel(i18n("&Placement:"),windowsBox);

    placementCombo = new QComboBox(false, windowsBox);
    placementCombo->insertItem(i18n("Smart"), SMART_PLACEMENT);
    placementCombo->insertItem(i18n("Maximizing"), MAXIMIZING_PLACEMENT);
    placementCombo->insertItem(i18n("Cascade"), CASCADE_PLACEMENT);
    placementCombo->insertItem(i18n("Random"), RANDOM_PLACEMENT);
    placementCombo->insertItem(i18n("Centered"), CENTERED_PLACEMENT);
    placementCombo->insertItem(i18n("Zero-Cornered"), ZEROCORNERED_PLACEMENT);
    // CT: disabling is needed as long as functionality misses in kwin
    //placementCombo->insertItem(i18n("Interactive"), INTERACTIVE_PLACEMENT);
    //placementCombo->insertItem(i18n("Manual"), MANUAL_PLACEMENT);
    placementCombo->setCurrentItem(SMART_PLACEMENT);

    // FIXME, when more policies have been added to KWin
    wtstr = i18n("The placement policy determines where a new window"
                 " will appear on the desktop."
                 " <ul>"
                 " <li><em>Smart</em> will try to achieve a minimum overlap of windows</li>"
                 " <li><em>Maximizing</em> will try to maximize every window to fill the whole screen."
                 " It might be useful to selectively affect placement of some windows using"
                 " the window-specific settings.</li>"
                 " <li><em>Cascade</em> will cascade the windows</li>"
                 " <li><em>Random</em> will use a random position</li>"
                 " <li><em>Centered</em> will place the window centered</li>"
                 " <li><em>Zero-Cornered</em> will place the window in the top-left corner</li>"
                 "</ul>") ;

    QWhatsThis::add( plcLabel, wtstr);
    QWhatsThis::add( placementCombo, wtstr);

    plcLabel->setBuddy(placementCombo);
    vLay->addWidget(plcLabel, 0);
    vLay->addWidget(placementCombo, 1, Qt::AlignLeft);

    bLay->addSpacing(10);

    lay->addWidget(windowsBox);

    //iTLabel = new QLabel(i18n("  Allowed overlap:\n"
    //                         "(% of desktop space)"),
    //             plcBox);
    //iTLabel->setAlignment(AlignTop|AlignHCenter);
    //pLay->addWidget(iTLabel,1,1);

    //interactiveTrigger = new QSpinBox(0, 500, 1, plcBox);
    //pLay->addWidget(interactiveTrigger,1,2);

    //pLay->addRowSpacing(2,KDialog::spacingHint());

    //lay->addWidget(plcBox);


    //CT 15mar98 - add EdgeResistance, BorderAttractor, WindowsAttractor config
    MagicBox = new QVButtonGroup(i18n("Snap Zones"), this);
    MagicBox->setMargin(15);

    BrdrSnap = new KIntNumInput(10, MagicBox);
    BrdrSnap->setSpecialValueText( i18n("none") );
    BrdrSnap->setRange( 0, MAX_BRDR_SNAP);
    BrdrSnap->setLabel(i18n("&Border snap zone:"));
    BrdrSnap->setSteps(1,10);
    QWhatsThis::add( BrdrSnap, i18n("Here you can set the snap zone for screen borders, i.e."
                                    " the 'strength' of the magnetic field which will make windows snap to the border when"
                                    " moved near it.") );

    WndwSnap = new KIntNumInput(10, MagicBox);
    WndwSnap->setSpecialValueText( i18n("none") );
    WndwSnap->setRange( 0, MAX_WNDW_SNAP);
    WndwSnap->setLabel(i18n("&Window snap zone:"));
    BrdrSnap->setSteps(1,10);
    QWhatsThis::add( WndwSnap, i18n("Here you can set the snap zone for windows, i.e."
                                    " the 'strength' of the magnetic field which will make windows snap to each other when"
                                    " they're moved near another window.") );

    OverlapSnap=new QCheckBox(i18n("Snap windows onl&y when overlapping"),MagicBox);
    QWhatsThis::add( OverlapSnap, i18n("Here you can set that windows will be only"
                                       " snapped if you try to overlap them, i.e. they will not be snapped if the windows"
                                       " comes only near another window or border.") );

    lay->addWidget(MagicBox);
    lay->addStretch();

    load();

    // Any changes goes to slotChanged()
    connect( opaque, SIGNAL(clicked()), SLOT(changed()));
    connect( resizeOpaqueOn, SIGNAL(clicked()), SLOT(changed()));
    connect( geometryTipOn, SIGNAL(clicked()), SLOT(changed()));
    connect( animationsOn, SIGNAL(clicked() ), SLOT(changed()));
    connect( minimizeAnimCombo, SIGNAL(activated(int)), SLOT(changed()));
    connect( maximizeAnimOn, SIGNAL(clicked() ), SLOT(changed()));
    connect( openCloseAnimOn, SIGNAL(clicked() ), SLOT(changed()));
    connect( minimizeAnimSlider, SIGNAL(valueChanged(int)), SLOT(changed()));
    connect( moveResizeMaximized, SIGNAL(toggled(bool)), SLOT(changed()));
    connect( placementCombo, SIGNAL(activated(int)), SLOT(changed()));
    connect( BrdrSnap, SIGNAL(valueChanged(int)), SLOT(changed()));
    connect( BrdrSnap, SIGNAL(valueChanged(int)), SLOT(slotBrdrSnapChanged(int)));
    connect( WndwSnap, SIGNAL(valueChanged(int)), SLOT(changed()));
    connect( WndwSnap, SIGNAL(valueChanged(int)), SLOT(slotWndwSnapChanged(int)));
    connect( OverlapSnap, SIGNAL(clicked()), SLOT(changed()));

    // To get suffix to BrdrSnap and WndwSnap inputs with default values.
    slotBrdrSnapChanged(BrdrSnap->value());
    slotWndwSnapChanged(WndwSnap->value());
}

int KMovingConfig::getMove()
{
    return (opaque->isChecked())? OPAQUE : TRANSPARENT;
}

void KMovingConfig::setMove(int trans)
{
    opaque->setChecked(trans == OPAQUE);
}

void KMovingConfig::setGeometryTip(bool showGeometryTip)
{
    geometryTipOn->setChecked(showGeometryTip);
}

bool KMovingConfig::getGeometryTip()
{
    return geometryTipOn->isChecked();
}

// placement policy --- CT 31jan98 ---
int KMovingConfig::getPlacement()
{
    return placementCombo->currentItem();
}

void KMovingConfig::setPlacement(int plac)
{
    placementCombo->setCurrentItem(plac);
}

int KMovingConfig::getMinimizeAnimSpeed()
{
    return minimizeAnimSlider->value();
}

void KMovingConfig::setAnimations(bool anim)
{
    animationsOn->setChecked( anim );
    minimizeAnimLabel->setEnabled( anim );
    minimizeAnimCombo->setEnabled( anim );
    maximizeAnimOn->setEnabled( anim );
    openCloseAnimOn->setEnabled( anim );
    minimizeAnimSpeedLabel->setEnabled( anim );
    minimizeAnimSlider->setEnabled( anim );
    minimizeAnimSlowLabel->setEnabled( anim );
    minimizeAnimFastLabel->setEnabled( anim );
}

void KMovingConfig::setMinimizeAnimSpeed(int speed)
{
    minimizeAnimSlider->setValue(speed);
}

int KMovingConfig::getResizeOpaque()
{
    return (resizeOpaqueOn->isChecked())? RESIZE_OPAQUE : RESIZE_TRANSPARENT;
}

void KMovingConfig::setResizeOpaque(int opaque)
{
    resizeOpaqueOn->setChecked(opaque == RESIZE_OPAQUE);
}

void KMovingConfig::setMoveResizeMaximized(bool a) {
    moveResizeMaximized->setChecked(a);
}

void KMovingConfig::slotBrdrSnapChanged(int value) {
    BrdrSnap->setSuffix(i18n(" pixel", " pixels", value));
}

void KMovingConfig::slotWndwSnapChanged(int value) {
    WndwSnap->setSuffix(i18n(" pixel", " pixels", value));
}

void KMovingConfig::load( void )
{
    QString key;

    config->setGroup( "Windows" );

    key = config->readEntry(KWIN_MOVE, "Opaque");
    if( key == "Transparent")
        setMove(TRANSPARENT);
    else if( key == "Opaque")
        setMove(OPAQUE);

    // the window animations; the old minimize animation's keys when not set
    // (keep the defaults consistent with kwin's options.cpp)
    bool anim = config->readBoolEntry(KWIN_MINIMIZE_ANIM, true );
    int animSpeed = config->readNumEntry(KWIN_MINIMIZE_ANIM_SPEED, 5);
    config->setGroup( "Effects" );
    QString minimizeAnim = config->readEntry( "MinimizeAnimation" ).lower();
    if( minimizeAnim.isEmpty())
        minimizeAnim = anim ? "scale" : "none";
    minimizeAnimCombo->setCurrentItem( minimizeAnim == "none" ? 2 : minimizeAnim == "genie" ? 1 : 0 );
    maximizeAnimOn->setChecked( config->readBoolEntry( "MaximizeAnimation", true ));
    openCloseAnimOn->setChecked( config->readBoolEntry( "OpenCloseAnimation", true ));
    animSpeed = config->readNumEntry( "AnimationSpeed", animSpeed );
    if( animSpeed < 1 ) animSpeed = 0;
    if( animSpeed > 10 ) animSpeed = 10;
    setAnimations( config->readBoolEntry( "Enabled", true ));
    setMinimizeAnimSpeed( animSpeed );
    config->setGroup( "Windows" );

    // DF: please keep the default consistent with kwin (options.cpp line 145)
    key = config->readEntry(KWIN_RESIZE_OPAQUE, "Opaque");
    if( key == "Opaque")
        setResizeOpaque(RESIZE_OPAQUE);
    else if ( key == "Transparent")
        setResizeOpaque(RESIZE_TRANSPARENT);

    //KS 10Jan2003 - Geometry Tip during window move/resize
    bool showGeomTip = config->readBoolEntry(KWIN_GEOMETRY, false);
    setGeometryTip( showGeomTip );

    // placement policy --- CT 19jan98 ---
    key = config->readEntry(KWIN_PLACEMENT, "Centered");
    //CT 13mar98 interactive placement
//   if( key.left(11) == "interactive") {
//     setPlacement(INTERACTIVE_PLACEMENT);
//     int comma_pos = key.find(',');
//     if (comma_pos < 0)
//       interactiveTrigger->setValue(0);
//     else
//       interactiveTrigger->setValue (key.right(key.length()
//                           - comma_pos).toUInt(0));
//     iTLabel->setEnabled(true);
//     interactiveTrigger->show();
//   }
//   else {
//     interactiveTrigger->setValue(0);
//     iTLabel->setEnabled(false);
//     interactiveTrigger->hide();
    if( key == "Random")
        setPlacement(RANDOM_PLACEMENT);
    else if( key == "Cascade")
        setPlacement(CASCADE_PLACEMENT); //CT 31jan98
    //CT 31mar98 manual placement
    else if( key == "manual")
        setPlacement(MANUAL_PLACEMENT);
    else if( key == "Centered")
        setPlacement(CENTERED_PLACEMENT);
    else if( key == "ZeroCornered")
        setPlacement(ZEROCORNERED_PLACEMENT);
    else if( key == "Maximizing")
        setPlacement(MAXIMIZING_PLACEMENT);
    else
        setPlacement(SMART_PLACEMENT);
//  }

    setMoveResizeMaximized(config->readBoolEntry(KWIN_MOVE_RESIZE_MAXIMIZED, false));

    int v;

    v = config->readNumEntry(KWM_BRDR_SNAP_ZONE, KWM_BRDR_SNAP_ZONE_DEFAULT);
    if (v > MAX_BRDR_SNAP) setBorderSnapZone(MAX_BRDR_SNAP);
    else if (v < 0) setBorderSnapZone (0);
    else setBorderSnapZone(v);

    v = config->readNumEntry(KWM_WNDW_SNAP_ZONE, KWM_WNDW_SNAP_ZONE_DEFAULT);
    if (v > MAX_WNDW_SNAP) setWindowSnapZone(MAX_WNDW_SNAP);
    else if (v < 0) setWindowSnapZone (0);
    else setWindowSnapZone(v);

    OverlapSnap->setChecked(config->readBoolEntry("SnapOnlyWhenOverlapping",false));
    emit KCModule::changed(false);
}

void KMovingConfig::save( void )
{
    int v;

    config->setGroup( "Windows" );

    v = getMove();
    if (v == TRANSPARENT)
        config->writeEntry(KWIN_MOVE,"Transparent");
    else
        config->writeEntry(KWIN_MOVE,"Opaque");

    config->writeEntry(KWIN_GEOMETRY, getGeometryTip());

    // placement policy --- CT 31jan98 ---
    v =getPlacement();
    if (v == RANDOM_PLACEMENT)
        config->writeEntry(KWIN_PLACEMENT, "Random");
    else if (v == CASCADE_PLACEMENT)
        config->writeEntry(KWIN_PLACEMENT, "Cascade");
    else if (v == CENTERED_PLACEMENT)
        config->writeEntry(KWIN_PLACEMENT, "Centered");
    else if (v == ZEROCORNERED_PLACEMENT)
        config->writeEntry(KWIN_PLACEMENT, "ZeroCornered");
    else if (v == MAXIMIZING_PLACEMENT)
        config->writeEntry(KWIN_PLACEMENT, "Maximizing");
//CT 13mar98 manual and interactive placement
//   else if (v == MANUAL_PLACEMENT)
//     config->writeEntry(KWIN_PLACEMENT, "Manual");
//   else if (v == INTERACTIVE_PLACEMENT) {
//       QString tmpstr = QString("Interactive,%1").arg(interactiveTrigger->value());
//       config->writeEntry(KWIN_PLACEMENT, tmpstr);
//   }
    else
        config->writeEntry(KWIN_PLACEMENT, "Smart");

    // (the old keys too, for older KWins)
    config->writeEntry(KWIN_MINIMIZE_ANIM, animationsOn->isChecked() && minimizeAnimCombo->currentItem() != 2);
    config->writeEntry(KWIN_MINIMIZE_ANIM_SPEED, getMinimizeAnimSpeed());
    config->setGroup( "Effects" );
    config->writeEntry( "Enabled", animationsOn->isChecked());
    const int minimizeAnim = minimizeAnimCombo->currentItem();
    config->writeEntry( "MinimizeAnimation", minimizeAnim == 2 ? "None" : minimizeAnim == 1 ? "Genie" : "Scale" );
    config->writeEntry( "MaximizeAnimation", maximizeAnimOn->isChecked());
    config->writeEntry( "OpenCloseAnimation", openCloseAnimOn->isChecked());
    config->writeEntry( "AnimationSpeed", getMinimizeAnimSpeed());
    config->setGroup( "Windows" );

    v = getResizeOpaque();
    if (v == RESIZE_OPAQUE)
        config->writeEntry(KWIN_RESIZE_OPAQUE, "Opaque");
    else
        config->writeEntry(KWIN_RESIZE_OPAQUE, "Transparent");

    config->writeEntry(KWIN_MOVE_RESIZE_MAXIMIZED, moveResizeMaximized->isChecked());


    config->writeEntry(KWM_BRDR_SNAP_ZONE,getBorderSnapZone());
    config->writeEntry(KWM_WNDW_SNAP_ZONE,getWindowSnapZone());
    config->writeEntry("SnapOnlyWhenOverlapping",OverlapSnap->isChecked());

    if (standAlone)
    {
        config->sync();
        if ( !kapp->dcopClient()->isAttached() )
            kapp->dcopClient()->attach();
        kapp->dcopClient()->send("kwin*", "", "reconfigure()", "");
    }
    emit KCModule::changed(false);
}

void KMovingConfig::defaults()
{
    setMove(OPAQUE);
    setResizeOpaque(RESIZE_TRANSPARENT);
    setGeometryTip(false);
    setPlacement(CENTERED_PLACEMENT);
    setMoveResizeMaximized(false);

    //copied from kcontrol/konq/kwindesktop, aleXXX
    setWindowSnapZone(KWM_WNDW_SNAP_ZONE_DEFAULT);
    setBorderSnapZone(KWM_BRDR_SNAP_ZONE_DEFAULT);
    OverlapSnap->setChecked(false);

    setAnimations( true );
    minimizeAnimCombo->setCurrentItem( 0 );
    maximizeAnimOn->setChecked( true );
    openCloseAnimOn->setChecked( true );
    setMinimizeAnimSpeed( 5 );
    emit KCModule::changed(true);
}

int KMovingConfig::getBorderSnapZone() {
  return BrdrSnap->value();
}

void KMovingConfig::setBorderSnapZone(int pxls) {
  BrdrSnap->setValue(pxls);
}

int KMovingConfig::getWindowSnapZone() {
  return WndwSnap->value();
}

void KMovingConfig::setWindowSnapZone(int pxls) {
  WndwSnap->setValue(pxls);
}

KCompositingConfig::~KCompositingConfig ()
{
    if (standAlone)
        delete config;
}

KCompositingConfig::KCompositingConfig (bool _standAlone, KConfig *_config, QWidget *parent, const char *)
    : KCModule(parent, "kcmkwm"), config(_config), standAlone(_standAlone)
{
  QVBoxLayout *lay = new QVBoxLayout (this, 0, KDialog::spacingHint());

  enabled = new QCheckBox(i18n("&Enable compositing"), this);
  QWhatsThis::add(enabled, i18n("KWin draws all windows itself: with soft shadows, translucent windows"
    " (window opacity, applications with an alpha channel) and without tearing (each monitor is"
    " updated at its refresh).  Needs an X server that renders on the graphics card; Ctrl+Alt+Shift+F12"
    " suspends and resumes it."));
  lay->addWidget(enabled);
  status = new QLabel(this);
  lay->addWidget(status);

  QHBoxLayout *hLay = new QHBoxLayout(lay);
  QLabel *label = new QLabel(i18n("&Rendering:"), this);
  hLay->addWidget(label);
  backend = new QComboBox(false, this);
  backend->insertItem(i18n("Automatic"));
  backend->insertItem(i18n("OpenGL ES (graphics card)"));
  backend->insertItem(i18n("XRender (X server)"));
  label->setBuddy(backend);
  QWhatsThis::add(backend, i18n("Automatic uses OpenGL ES when the X server renders on the graphics card"
    " (DRI3), else XRender."));
  hLay->addWidget(backend);
  hLay->addStretch();

  unredirectFullscreen = new QCheckBox(i18n("Draw &full screen windows directly"), this);
  QWhatsThis::add(unredirectFullscreen, i18n("A full screen window (a game, a video) with nothing above it"
    " is drawn by the X server directly, without the compositor: faster and with less latency."));
  lay->addWidget(unredirectFullscreen);

  tabs = new QTabWidget(this);
  lay->addWidget(tabs);

  // the shadows
  QWidget *sPage = new QWidget(tabs);
  QVBoxLayout *sLay = new QVBoxLayout(sPage, KDialog::marginHint(), KDialog::spacingHint());
  shadows = new QCheckBox(i18n("Soft &shadows below windows"), sPage);
  QWhatsThis::add(shadows, i18n("The active window has the larger shadow, inactive windows smaller ones,"
    " menus and tooltips small ones; none for the desktop, panels and full screen or maximized windows."));
  sLay->addWidget(shadows);
  shadowSize = new KIntNumInput(12, sPage);
  shadowSize->setRange(2, 40, 1, true);
  shadowSize->setSuffix(i18n(" px"));
  shadowSize->setLabel(i18n("Size (active window):"));
  sLay->addWidget(shadowSize);
  shadowOpacity = new KIntNumInput(shadowSize, 50, sPage);
  shadowOpacity->setRange(5, 100, 5, true);
  shadowOpacity->setSuffix("%");
  shadowOpacity->setLabel(i18n("Darkness:"));
  sLay->addWidget(shadowOpacity);
  shadowOffsetX = new KIntNumInput(shadowOpacity, 0, sPage);
  shadowOffsetX->setRange(-16, 16, 1, true);
  shadowOffsetX->setSuffix(i18n(" px"));
  shadowOffsetX->setLabel(i18n("Horizontal offset:"));
  sLay->addWidget(shadowOffsetX);
  shadowOffsetY = new KIntNumInput(shadowOffsetX, 3, sPage);
  shadowOffsetY->setRange(-16, 16, 1, true);
  shadowOffsetY->setSuffix(i18n(" px"));
  shadowOffsetY->setLabel(i18n("Vertical offset:"));
  sLay->addWidget(shadowOffsetY);
  sLay->addStretch();
  tabs->addTab(sPage, i18n("Shadows"));

  // the window opacity rules
  QWidget *oPage = new QWidget(tabs);
  QVBoxLayout *oLay = new QVBoxLayout(oPage, KDialog::marginHint(), KDialog::spacingHint());
  QGridLayout *gLay = new QGridLayout(oLay, 4, 2, KDialog::spacingHint());
  gLay->setColStretch(1, 1);
  activeWindowTransparency = new QCheckBox(i18n("Active windows:"), oPage);
  gLay->addWidget(activeWindowTransparency, 0, 0);
  activeWindowOpacity = new KIntNumInput(100, oPage);
  activeWindowOpacity->setRange(0, 100);
  activeWindowOpacity->setSuffix("%");
  gLay->addWidget(activeWindowOpacity, 0, 1);
  inactiveWindowTransparency = new QCheckBox(i18n("Inactive windows:"), oPage);
  gLay->addWidget(inactiveWindowTransparency, 1, 0);
  inactiveWindowOpacity = new KIntNumInput(100, oPage);
  inactiveWindowOpacity->setRange(0, 100);
  inactiveWindowOpacity->setSuffix("%");
  gLay->addWidget(inactiveWindowOpacity, 1, 1);
  movingWindowTransparency = new QCheckBox(i18n("Moving windows:"), oPage);
  gLay->addWidget(movingWindowTransparency, 2, 0);
  movingWindowOpacity = new KIntNumInput(100, oPage);
  movingWindowOpacity->setRange(0, 100);
  movingWindowOpacity->setSuffix("%");
  gLay->addWidget(movingWindowOpacity, 2, 1);
  dockWindowTransparency = new QCheckBox(i18n("Dock windows:"), oPage);
  gLay->addWidget(dockWindowTransparency, 3, 0);
  dockWindowOpacity = new KIntNumInput(100, oPage);
  dockWindowOpacity->setRange(0, 100);
  dockWindowOpacity->setSuffix("%");
  gLay->addWidget(dockWindowOpacity, 3, 1);
  keepAboveAsActive = new QCheckBox(i18n("Treat 'keep above' windows as active ones"), oPage);
  oLay->addWidget(keepAboveAsActive);
  oLay->addStretch();
  tabs->addTab(oPage, i18n("Opacity"));
  lay->addStretch();

  connect(enabled, SIGNAL(toggled(bool)), SLOT(updateEnabled()));
  connect(shadows, SIGNAL(toggled(bool)), SLOT(updateEnabled()));
  connect(activeWindowTransparency, SIGNAL(toggled(bool)), SLOT(updateEnabled()));
  connect(inactiveWindowTransparency, SIGNAL(toggled(bool)), SLOT(updateEnabled()));
  connect(movingWindowTransparency, SIGNAL(toggled(bool)), SLOT(updateEnabled()));
  connect(dockWindowTransparency, SIGNAL(toggled(bool)), SLOT(updateEnabled()));

  connect(enabled, SIGNAL(toggled(bool)), SLOT(changed()));
  connect(backend, SIGNAL(activated(int)), SLOT(changed()));
  connect(unredirectFullscreen, SIGNAL(toggled(bool)), SLOT(changed()));
  connect(shadows, SIGNAL(toggled(bool)), SLOT(changed()));
  connect(shadowSize, SIGNAL(valueChanged(int)), SLOT(changed()));
  connect(shadowOpacity, SIGNAL(valueChanged(int)), SLOT(changed()));
  connect(shadowOffsetX, SIGNAL(valueChanged(int)), SLOT(changed()));
  connect(shadowOffsetY, SIGNAL(valueChanged(int)), SLOT(changed()));
  connect(activeWindowTransparency, SIGNAL(toggled(bool)), SLOT(changed()));
  connect(inactiveWindowTransparency, SIGNAL(toggled(bool)), SLOT(changed()));
  connect(movingWindowTransparency, SIGNAL(toggled(bool)), SLOT(changed()));
  connect(dockWindowTransparency, SIGNAL(toggled(bool)), SLOT(changed()));
  connect(keepAboveAsActive, SIGNAL(toggled(bool)), SLOT(changed()));
  connect(activeWindowOpacity, SIGNAL(valueChanged(int)), SLOT(changed()));
  connect(inactiveWindowOpacity, SIGNAL(valueChanged(int)), SLOT(changed()));
  connect(movingWindowOpacity, SIGNAL(valueChanged(int)), SLOT(changed()));
  connect(dockWindowOpacity, SIGNAL(valueChanged(int)), SLOT(changed()));

  load();
}

bool KCompositingConfig::enabledByDefault()
{
    // (like KWin: Compositor::gpuAvailable())
    Display* dpy = qt_xdisplay();
    int opcode, ev, err;
    return XQueryExtension(dpy, "DRI3", &opcode, &ev, &err) && XQueryExtension(dpy, "Present", &opcode, &ev, &err);
}

void KCompositingConfig::updateEnabled()
{
  const bool on = enabled->isChecked();
  backend->setEnabled(on);
  unredirectFullscreen->setEnabled(on);
  tabs->setEnabled(on);
  shadowSize->setEnabled(shadows->isChecked());
  shadowOpacity->setEnabled(shadows->isChecked());
  shadowOffsetX->setEnabled(shadows->isChecked());
  shadowOffsetY->setEnabled(shadows->isChecked());
  activeWindowOpacity->setEnabled(activeWindowTransparency->isChecked());
  inactiveWindowOpacity->setEnabled(inactiveWindowTransparency->isChecked());
  movingWindowOpacity->setEnabled(movingWindowTransparency->isChecked());
  dockWindowOpacity->setEnabled(dockWindowTransparency->isChecked());
}

// what KWin says (it may be off although enabled: no GPU, another compositor)
void KCompositingConfig::updateStatus()
{
  QByteArray data, reply;
  QCString reply_type;
  if (!kapp->dcopClient()->isAttached())
      kapp->dcopClient()->attach();
  QString text;
  if (kapp->dcopClient()->call("kwin", "KWinInterface", "compositingStatus()", data, reply_type, reply)
      && reply_type == "QString")
  {
      QDataStream stream(reply, IO_ReadOnly);
      stream >> text;
  }
  status->setText(text.isEmpty() ? QString::null : "<i>" + QStyleSheet::escape(text) + "</i>");
}

void KCompositingConfig::load( void )
{
  // (like KWin: KDE 3.5's UseTranslucency=true when there is no setting)
  config->setGroup("Notification Messages");
  const bool oldTranslucency = config->readBoolEntry("UseTranslucency", false);
  config->setGroup("Compositing");
  enabled->setChecked(config->hasKey("Enabled") ? config->readBoolEntry("Enabled", true)
      : oldTranslucency || enabledByDefault());
  QString b = config->readEntry("Backend", "auto").lower();
  backend->setCurrentItem(b == "gles" ? 1 : b == "xrender" ? 2 : 0);
  unredirectFullscreen->setChecked(config->readBoolEntry("UnredirectFullscreen", true));
  shadows->setChecked(config->readBoolEntry("Shadows", true));
  shadowSize->setValue(config->readNumEntry("ShadowSize", 12));
  shadowOpacity->setValue(config->readNumEntry("ShadowOpacity", 50));
  shadowOffsetX->setValue(config->readNumEntry("ShadowOffsetX", 1));
  shadowOffsetY->setValue(config->readNumEntry("ShadowOffsetY", 1));

  config->setGroup("Translucency");
  activeWindowTransparency->setChecked(config->readBoolEntry("TranslucentActiveWindows", false));
  inactiveWindowTransparency->setChecked(config->readBoolEntry("TranslucentInactiveWindows", false));
  movingWindowTransparency->setChecked(config->readBoolEntry("TranslucentMovingWindows", false));
  dockWindowTransparency->setChecked(config->readBoolEntry("TranslucentDocks", false));
  keepAboveAsActive->setChecked(config->readBoolEntry("TreatKeepAboveAsActive", true));
  activeWindowOpacity->setValue(config->readNumEntry("ActiveWindowOpacity", 100));
  inactiveWindowOpacity->setValue(config->readNumEntry("InactiveWindowOpacity", 75));
  movingWindowOpacity->setValue(config->readNumEntry("MovingWindowOpacity", 50));
  dockWindowOpacity->setValue(config->readNumEntry("DockOpacity", 80));

  updateEnabled();
  updateStatus();
  emit KCModule::changed(false);
}

void KCompositingConfig::save( void )
{
  // (the old key is replaced by Enabled)
  config->setGroup("Notification Messages");
  config->deleteEntry("UseTranslucency");
  config->setGroup("Compositing");
  // (no setting: the default follows the X server)
  if (enabled->isChecked() == enabledByDefault())
      config->deleteEntry("Enabled");
  else
      config->writeEntry("Enabled", enabled->isChecked());
  static const char* const backends[] = { "auto", "gles", "xrender" };
  config->writeEntry("Backend", backends[backend->currentItem()]);
  config->writeEntry("UnredirectFullscreen", unredirectFullscreen->isChecked());
  config->writeEntry("Shadows", shadows->isChecked());
  config->writeEntry("ShadowSize", shadowSize->value());
  config->writeEntry("ShadowOpacity", shadowOpacity->value());
  config->writeEntry("ShadowOffsetX", shadowOffsetX->value());
  config->writeEntry("ShadowOffsetY", shadowOffsetY->value());

  config->setGroup("Translucency");
  config->writeEntry("TranslucentActiveWindows", activeWindowTransparency->isChecked());
  config->writeEntry("TranslucentInactiveWindows", inactiveWindowTransparency->isChecked());
  config->writeEntry("TranslucentMovingWindows", movingWindowTransparency->isChecked());
  config->writeEntry("TranslucentDocks", dockWindowTransparency->isChecked());
  config->writeEntry("TreatKeepAboveAsActive", keepAboveAsActive->isChecked());
  config->writeEntry("ActiveWindowOpacity", activeWindowOpacity->value());
  config->writeEntry("InactiveWindowOpacity", inactiveWindowOpacity->value());
  config->writeEntry("MovingWindowOpacity", movingWindowOpacity->value());
  config->writeEntry("DockOpacity", dockWindowOpacity->value());

  if (standAlone)
  {
    config->sync();
    if ( !kapp->dcopClient()->isAttached() )
        kapp->dcopClient()->attach();
    kapp->dcopClient()->send("kwin*", "", "reconfigure()", "");
  }
  // (KWin applies it: the status a bit later)
  QTimer::singleShot(1500, this, SLOT(updateStatus()));
  emit KCModule::changed(false);
}

void KCompositingConfig::defaults()
{
  enabled->setChecked(enabledByDefault());
  backend->setCurrentItem(0);
  unredirectFullscreen->setChecked(true);
  shadows->setChecked(true);
  shadowSize->setValue(12);
  shadowOpacity->setValue(50);
  shadowOffsetX->setValue(1);
  shadowOffsetY->setValue(1);
  activeWindowTransparency->setChecked(false);
  inactiveWindowTransparency->setChecked(false);
  movingWindowTransparency->setChecked(false);
  dockWindowTransparency->setChecked(false);
  keepAboveAsActive->setChecked(true);
  activeWindowOpacity->setValue(100);
  inactiveWindowOpacity->setValue(75);
  movingWindowOpacity->setValue(50);
  dockWindowOpacity->setValue(80);
  updateEnabled();
  emit KCModule::changed(true);
}

#include "windows.moc"
