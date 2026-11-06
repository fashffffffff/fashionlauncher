#!/bin/sh
# Re-extracts the QML shell's strings into every qmlui_<lang>.ts, compiles them to .qm
# and prints how many strings each language still has untranslated.
#
#   launcher/qmlui/i18n/update.sh            # lupdate + lrelease + report
#   launcher/qmlui/i18n/update.sh --report   # only the report
#
# The .qm files are picked up by launcher/CMakeLists.txt (glob of qmlui/i18n/*.qm), so a
# rebuild bundles them. Language codes must match LauncherBridge's g_uiLanguages table.
set -e
cd "$(dirname "$0")/.."

LANGS="ru es pt_BR de fr it pl uk tr zh_CN ja ko cs id"

if [ "$1" != "--report" ]; then
    for l in $LANGS; do
        ts="i18n/qmlui_$l.ts"
        lang=""
        [ -f "$ts" ] || lang="-source-language en -target-language $l"  # only for a brand-new file
        # shellcheck disable=SC2086
        lupdate -silent -locations none -no-obsolete -extensions cpp,h,qml,js $lang . -ts "$ts"
        lrelease -silent "$ts" -qm "i18n/qmlui_$l.qm"
    done
fi

printf '%-6s %6s %6s\n' lang total todo
for l in $LANGS; do
    ts="i18n/qmlui_$l.ts"
    total=$(grep -c '<message' "$ts" || true)
    todo=$(grep -c 'type="unfinished"' "$ts" || true)
    printf '%-6s %6s %6s\n' "$l" "$total" "$todo"
done
