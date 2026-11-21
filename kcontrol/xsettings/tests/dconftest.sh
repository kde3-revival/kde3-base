#!/bin/sh
# Tests of the KDE 3 session's dconf database: startkde's
# kde3_dconf_setup/kde3_dconf_cleanup and the writer of kded_xsettings,
# with a temporary HOME and a private session bus (dbus-run-session, the
# system's dconf-service).  The user's real dconf database, systemd user
# manager and KDE settings are not touched: systemctl is a stub.
#
#   dconftest.sh <startkde> <dconfsettingstest>

startkde=$1
writer=$2
if test -z "$IN_PRIVATE_BUS"; then
  t=`mktemp -d /var/tmp/kde3-dconf-test.XXXXXX`
  mkdir -m 700 -p "$t/home" "$t/run" "$t/bin" "$t/share"
  cat >"$t/bin/systemctl" <<EOS
#!/bin/sh
echo "\$*" >>"$t/systemctl.log"
EOS
  cp "$t/bin/systemctl" "$t/bin/systemd-run"
  chmod +x "$t/bin/systemctl" "$t/bin/systemd-run"
  env -i PATH="$t/bin:$PATH" HOME="$t/home" XDG_RUNTIME_DIR="$t/run" \
    XDG_DATA_DIRS="$t/share:/usr/share" KDEHOME="$t/home/.kde" T="$t" \
    IN_PRIVATE_BUS=1 dbus-run-session -- sh "$0" "$@"
  r=$?
  rm -rf "$t"
  exit $r
fi

fail() { echo "FAIL: $*"; exit 1; }
db=$HOME/.config/dconf
# the functions of startkde
eval "`sed -n '/^kde3_dconf_setup()/,/^}/p;/^kde3_dconf_cleanup()/,/^}/p' "$startkde"`"

# the GNOME settings of the user
gsettings set org.gnome.desktop.interface document-font-name 'Adwaita Sans 12'
gsettings set org.gnome.desktop.interface monospace-font-name 'Adwaita Mono 11'
gsettings set org.gnome.desktop.interface gtk-theme 'Custom-Theme'
sleep 1
test -f "$db/user" || fail "no user database"
before=`sha256sum <"$db/user"`

# opted out: nothing
ksmserverrc_general_separatedconf=false
kde3_dconf_profile=
kde3_dconf_setup
test -z "$DCONF_PROFILE" && test ! -e "$db/kde3" || fail "opt-out"

# a default profile whose writable database is another one: nothing
mkdir -p "$T/share/dconf/profile"
printf 'system-db:site\nuser-db:user\n' >"$T/share/dconf/profile/user"
ksmserverrc_general_separatedconf=true
kde3_dconf_setup
test -z "$DCONF_PROFILE" || fail "odd default profile"

# the default profile's system databases follow the session's
printf '# site\nuser-db:user\n\nsystem-db:local  # x\n' >"$T/share/dconf/profile/user"
kde3_dconf_setup
test "$DCONF_PROFILE" = "$XDG_RUNTIME_DIR/kde3/dconf-profile" || fail "DCONF_PROFILE=$DCONF_PROFILE"
test "`cat "$DCONF_PROFILE"`" = "user-db:kde3
system-db:local" || fail "profile: `cat $DCONF_PROFILE`"
cmp -s "$db/user" "$db/kde3" || fail "first login: not a copy"
rm "$T/share/dconf/profile/user"
kde3_dconf_setup
test "`cat "$DCONF_PROFILE"`" = "user-db:kde3" || fail "plain profile"

# KDE's fonts, written by kded_xsettings' writer
mkdir -p "$KDEHOME/share/config"
cat >"$KDEHOME/share/config/kdeglobals" <<EOS
[General]
font=DejaVu Serif,13,-1,5,50,0,0,0,0,0
fixed=Hack,9,-1,5,50,0,0,0,0,0
background=40,40,40
EOS
"$writer" --write || fail "writer"
sleep 1
g() { gsettings get org.gnome.desktop.interface $1; }
test "`g document-font-name`" = "'DejaVu Serif 13'" || fail "kde3 document font: `g document-font-name`"
test "`g monospace-font-name`" = "'Hack 9'" || fail "kde3 monospace font"
test "`g font-name`" = "'DejaVu Serif 13'" || fail "kde3 font"
test "`g color-scheme`" = "'prefer-dark'" || fail "kde3 colour scheme"
test "`g gtk-theme`" = "'Custom-Theme'" || fail "kde3: seeded key lost"
# a change of KDE's font
sed -i 's/^font=DejaVu Serif,13/font=DejaVu Sans,11/' "$KDEHOME/share/config/kdeglobals"
"$writer" --write || fail "writer 2"
sleep 1
test "`g document-font-name`" = "'DejaVu Sans 11'" || fail "kde3 font change"

# the user's database is as it was, and GNOME (no DCONF_PROFILE) reads it
test "`sha256sum <"$db/user"`" = "$before" || fail "the user's database changed"
p=$DCONF_PROFILE
unset DCONF_PROFILE
test "`g document-font-name`" = "'Adwaita Sans 12'" || fail "user document font"
test "`g monospace-font-name`" = "'Adwaita Mono 11'" || fail "user monospace font"

# a later login: the session's database is kept
DCONF_PROFILE=$p gsettings set org.gnome.desktop.interface gtk-theme 'Kde3-Only'
sleep 1
kde3_dconf_setup
test "`g gtk-theme`" = "'Kde3-Only'" || fail "second login re-seeded"
test "`DCONF_PROFILE= sh -c 'unset DCONF_PROFILE; gsettings get org.gnome.desktop.interface gtk-theme'`" = "'Custom-Theme'" || fail "user gtk-theme"
# an inherited session profile is not kept when opted out
ksmserverrc_general_separatedconf=false
kde3_dconf_setup
test -z "$DCONF_PROFILE" || fail "inherited DCONF_PROFILE kept"
ksmserverrc_general_separatedconf=true
kde3_dconf_setup

# logout: the variable leaves the systemd user manager, the portals stop
kde3_dconf_cleanup
grep -qxe "--user unset-environment DCONF_PROFILE" "$T/systemctl.log" || fail "no unset-environment"
grep -qxe "--user stop xdg-desktop-portal.service xdg-desktop-portal-gtk.service xdg-desktop-portal-gnome.service" "$T/systemctl.log" || fail "portals not stopped"
test "`sha256sum <"$db/user"`" = "$before" || fail "the user's database changed"
echo "dconftest: all passed"
