# Project State — ffprism (Prism Launcher fork)

## Goal
Fork of Prism Launcher (Minecraft launcher, C++/Qt6) with a brand-new QML UI.
Hard requirements: Microsoft login AND offline login both work; offline must NOT require
owning Minecraft via a Microsoft account (like the FreesmLauncher fork).

## Architecture
- `launcher/` — core + legacy Qt Widgets UI (`launcher/ui/...`).
- `launcher/qmlui/` — new QML shell: `QmlShell` (engine/window), `UiTheme` (JSON themes, base.json deep-merged,
  user themes in `<data>/ui-themes`, live reload), `InstancesModel`/`AccountsModel` (QML models; MSA browser+device-code
  sign-in, offline add), `LauncherBridge` (launch/kill/folders; unported features run classic dialogs via a hidden
  `MainWindow` + `invokeMethod` on its slots), `ImageProviders`. QML in `qmlui/qml/` (singleton `Style.qml` = tokens),
  themes in `qmlui/themes/`, fonts Unbounded + Golos Text, `i18n/qmlui_<code>.ts` → `.qm` (15 languages). All listed in `qmlui.qrc`
  (regenerate when adding files); `Q_INIT_RESOURCE(qmlui)` in `main.cpp`.
- `launcher/Application.cpp` — `showMainWindow()` opens the QML shell unless setting `UseClassicUI`; falls back to the
  classic window if QML fails to load. URL/file imports use `showClassicWindow()`. Emits `launchStarted` for progress.
- `launcher/LaunchController.cpp` — account selection (`decideAccount`), launch mode
  (`decideLaunchMode`), session setup (`login`) and launch.
- `launcher/minecraft/auth/` — `AccountList`, `MinecraftAccount` (`ownsMinecraft()` is false for Offline).

## Commands
- Build (Ninja, Debug, already configured in `build/`):
  `JAVA_HOME=/opt/homebrew/opt/openjdk@17/libexec/openjdk.jdk/Contents/Home cmake --build build`
- Single file: `ninja -C build launcher/CMakeFiles/Launcher_logic.dir/<path>.cpp.o`
- Output: `build/prismlauncher.app` (Release: `build-release/`). Qt comes from Homebrew.
- Run with test data only: `-d /Users/fash/projects/ffprism/devdata`. Window snapshot:
  `FFPRISM_SNAPSHOT=/tmp/x.png FFPRISM_PAGE=play|library|appearance|accounts FFPRISM_SNAPSHOT_QUIT=1`
  (never full-screen screencapture — user's private apps are on screen).
- Strings: `launcher/qmlui/i18n/update.sh` (lupdate+lrelease for all 14 .ts, prints todo per language; `--report` only).
  Fill gaps: `i18n/tsjson.py export <lang> out.json` → translate → `tsjson.py apply <lang> in.json` (validates
  placeholders/HTML/plural count), then update.sh again. .qm files are globbed into the qrc by CMake.

## Decisions (what + why)
- Offline gating removed (uncommitted, 2026-11-05):
  - `AccountListPage::on_actionAddOffline_triggered` — no `anyAccountIsValid()` check; offline
    accounts can be added without an MS account.
  - `LaunchController::decideAccount` — "No Accounts" box only when `count()==0`; profile-select
    dialog offered whenever `count()>0`.
  - `LaunchController::decideLaunchMode` — Offline account → `LaunchMode::Offline` + Continue
    immediately (no MS refresh, no demo fallback). MSA path unchanged.
  - `VersionPage::on_actionDownload_All_triggered` — only blocks when there are no accounts.
  - Why: offline-only users must be able to add accounts, download and play.
- MS skin upload and MSA refresh/reauth logic intentionally untouched.
- Setup wizard login page only in classic UI with zero accounts (QML has its own account panel).
- Kept C++ core + QML instead of a Rust rewrite: core is the value; RAM cost of QML (~150 MB vs 33 MB footprint,
  Release) is negligible next to Minecraft. Blur renders at 1/4 size to save memory.

## Current work + Next steps
- Done: Play (hotbar, backdrop), Library, Appearance editor, accounts panel, 5 themes, RU translation. Uncommitted.
- Next: runtime-test real launch (offline + MSA); port to QML: new/import instance, instance editor, settings,
  launch progress dialog (`LaunchController::onProgressRequested`), console; CloseAfterLaunch for QML window;
  unload QML while the game runs; rebrand (name, MSA client ID, CurseForge key) before any release.

- Languages (2026-11-06): new UI offers exactly 15 (table `g_uiLanguages` in LauncherBridge.cpp: Prism key, qm code,
  native name). Setting "Language" stores Prism's key (Simplified Chinese = `zh`, file `qmlui_zh_CN`); other saved keys
  are mapped to the closest (`supportedLanguage`) on start and when the translations index arrives. setLanguage also
  calls `updateLanguage` so Prism's core .po downloads.

## Gotchas
- `anyAccountIsValid()` really means "some MS account owns Minecraft" — don't use it to mean "has accounts".
- Multiple Claude sessions may share this checkout: don't switch branches/stash; keep to your own files.
- QML: don't name custom props like FINAL base props (`flat`, Canvas `top`) — the component fails to load.
- zsh: unquoted `--include=*.cpp` globs fail ("no matches found") — quote them.
