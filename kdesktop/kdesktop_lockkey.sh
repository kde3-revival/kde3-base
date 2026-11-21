#! /bin/sh
# kconf_update script for kdeglobals [Global Shortcuts]: Lock Session with
# only the old default (3 or 4 modifier keys scheme) gets the lock key too.
sed -n -e 's/^Lock Session=default(Alt+Ctrl+L)$/Lock Session=default(Alt+Ctrl+L);default(XF86ScreenSaver)/p' \
       -e 's/^Lock Session=default(Win+ScrollLock)$/Lock Session=default(Win+ScrollLock);default(XF86ScreenSaver)/p'
