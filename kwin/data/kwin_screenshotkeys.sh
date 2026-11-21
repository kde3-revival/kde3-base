#! /bin/sh
# kconf_update script for kdeglobals [Global Shortcuts]: Desktop Screenshot
# with only the old default (3 or 4 modifier keys scheme) gets Shift+Print.
sed -n -e 's/^Desktop Screenshot=default(Ctrl+Print)$/Desktop Screenshot=default(Shift+Print);default(Ctrl+Print)/p' \
       -e 's/^Desktop Screenshot=default(Win+Print)$/Desktop Screenshot=default(Shift+Print);default(Win+Print)/p'
