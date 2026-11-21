#! /bin/sh
# kconf_update script for khotkeysrc (all of it on stdin): disable the
# action imported from printscreen.khotkeys if it still runs plain
# "ksnapshot" on Print and nothing else.
awk '
/^\[.*\]$/ { group = substr($0, 2, length($0) - 2); next }
{
    i = index($0, "=")
    if (i > 0)
        value[group SUBSEP substr($0, 1, i - 1)] = substr($0, i + 1)
    groups[group] = 1
}
END {
    for (g in groups) {
        if (value[g SUBSEP "Type"] != "COMMAND_URL_SHORTCUT_ACTION_DATA") continue
        if (value[g SUBSEP "Enabled"] != "true") continue
        if (value[g "Actions" SUBSEP "ActionsCount"] != "1") continue
        if (value[g "Actions0" SUBSEP "CommandURL"] != "ksnapshot") continue
        if (value[g "Triggers" SUBSEP "TriggersCount"] != "1") continue
        if (value[g "Triggers0" SUBSEP "Type"] != "SHORTCUT") continue
        if (value[g "Triggers0" SUBSEP "Key"] != "Print") continue
        print "[" g "]"
        print "Enabled=false"
    }
}'
