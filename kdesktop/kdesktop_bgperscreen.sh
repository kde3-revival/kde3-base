#! /bin/sh
# kconf_update script for kdesktoprc: with a background per screen
# (DrawBackgroundPerScreen_<N>=true) common to all screens (CommonScreen
# not false), the settings of desktop N were in [Desktop<N>Screen0]; move
# them into [Desktop<N>], where they are read now.
awk '
/^\[.*\]$/ { group = substr($0, 2, length($0) - 2); next }
group == "Background Common" && /^CommonScreen=/ { common = substr($0, 14) }
group == "Background Common" && /^DrawBackgroundPerScreen_[0-9]+=/ {
    split($0, kv, "="); n = substr(kv[1], 25); perScreen[n] = kv[2]
}
group ~ /^Desktop[0-9]+Screen0$/ {
    n = substr(group, 8, length(group) - 14)
    entries[n] = entries[n] $0 "\n"; screen0[n] = 1
}
END {
    if (common == "false")
        exit
    for (n in screen0) {
        if (perScreen[n] != "true")
            continue
        printf "# DELETEGROUP [Desktop%s]\n", n
        printf "# DELETEGROUP [Desktop%sScreen0]\n", n
        printf "[Desktop%s]\n%s", n, entries[n]
    }
}'
