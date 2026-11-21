#! /bin/sh
# Deletes "Window to Next Screen=none" and the like for the four KWin actions
# whose default shortcuts use Meta; a shortcut the user chose is kept.
while IFS= read -r line; do
    case "$line" in
        "Window to Next Screen=none" | "Window to Previous Screen=none" | \
        "Window Maximize No Toggle=none" | "Window Restore=none")
            echo "# DELETE ${line%=none}"
            ;;
    esac
done
