#!/bin/sh

# Rewrites `var X` → `const X` for variables flagged by the compiler as
# set-once. Review the changes before committing — set-once doesn't always
# mean const (enums may need manual handling).

usage() {
        echo "usage: $(basename $0) <compiler_log> <source_file> <output_file>"
}

if [ "$#" -ne 3 ] ; then
        usage
        exit 1
fi

TMPFILE="$(mktemp)"
cp "$2" "${TMPFILE}"

for i in $(grep ' is only set once, at declaration. It should be a const.' "$1" \
           | cut -d' ' -f1 | tr -s \\n ' ') ; do
        echo -n "replacing $i..."
        sed -i s/"var $i\([ =;]\)"/"const $i\1"/g "${TMPFILE}"
        echo "done !"
done

mv "${TMPFILE}" "$3"
echo "all done !"
