# Project State — Fashion Launcher (Prism Launcher fork, repo ffprism)

## Goal
Fork of Prism Launcher (Minecraft launcher, C++/Qt6) with a brand-new QML UI.
Hard requirements: Microsoft login AND offline login both work; offline must NOT require
owning Minecraft via a Microsoft account (like the FreesmLauncher fork).

## Architecture
- `launcher/` — Prism core + legacy Qt Widgets UI (`launcher/ui/...`), still used as fallback dialogs.
- `launcher/qmlui/` — new QML shell. Sources and assets are GLOBBED by `launcher/CMakeLists.txt`
  (qrc generated into the build dir), so adding files never needs build-file edits.
  - C++: `QmlShell` (engine, translator reload on language change, context objects, registers QML type
    `FFPrism.WorldsModel`), `UiTheme` (JSON themes deep-merged over `themes/base.json`; sections colors/shape/type/
    background/motion/layout), `InstancesModel`, `AccountsModel` (MSA browser+device code, offline), `LauncherBridge`
    (launch, languages/setLanguage, onboarding flag, classic-dialog fallbacks via hidden `MainWindow`),
    feature bridges `settings/SettingsBridge` (`Settings`), `create/CreateBridge` (`Creator`),
    `resources/ResourcesBridge` (`Resources`), `backgrounds/BackgroundsBridge` (`Backgrounds`), `worlds/WorldsModel`.
  - QML (`qml/`, flat dir; singletons `Style` = tokens incl. `Style.panel()`, `Router` = navigation):
    `Main.qml` (sidebar + StackView), `Sidebar`, `Page*` (Home, Instances, Instance, Browse, Appearance, Settings,
    Create), instance tabs `Tab*` (each has `instanceId`, `instance`; resource tabs also `kind`), `Onboarding`.
- Theme scope: `UiTheme` keeps personal sections in `ui-themes/_personal.json` when `QmlThemeKeepLayout`
  (shape/type/layout/motion) or `QmlThemeKeepBackground` is on; `setToken` then writes there, not to the theme.
  Why: users want a theme's colors without its layout. Files starting with `_` are not listed as themes.
- Version/loader editing: `PageVersion.qml` (reuses `CreateVanilla.qml` picker) + `LauncherBridge::changeVersion`
  (PackProfile: set net.minecraft, one loader at a time, drops stale intermediary/hashed, resolve Online).
- Free memory while playing: `LauncherBridge` (game pid appears) → `Application::unloadShellForGame()` closes the
  QML window when `QmlUnloadWhilePlaying` (default on) or `CloseAfterLaunch`; `controllerFinished` reopens it
  unless `QuitAfterGameStop`. Why: QML shell costs ~100 MB more than classic.
- Java presets live in `qml/JavaPresets.js` (balanced G1 default applied once via `QmlJvmPresetApplied`; smooth =
  ZGC with IgnoreUnrecognizedVMOptions so Java 8/17 still start). `SettingsBridge::recommendedMaxMemory`.
- Accent from background: `UiTheme::setAccentImage` (hue histogram) when `QmlAccentFromBackground`.
- `CommandPalette.qml` (Ctrl/Cmd+K, `Router.openPalette()`); `themes/blocky.json` uses bundled Pixelify Sans.
- Memory recommendation keeps a fixed system reserve (2/3/4 GB by RAM size), no 8 GB cap, ~3 GB + 32 MB per mod.
  Why: user said heavy packs need 12 GB and 8 GB laptops should still get 6 GB.
- `InstancesView` (QML type = InstancesModel with sortMode/group) for the Instances page so the shared `Instances`
  (hotbar, sidebar "Recent") stays sorted by recent play. Window-level DropArea for .zip/.mrpack import is declared
  under the pages so page drop zones win. Log sharing: `LauncherBridge::shareLatestLog` (PasteUpload, anonymized).
- `launcher/Application.cpp` — QML shell by default (`UseClassicUI` = classic), falls back to classic if QML fails;
  emits `launchStarted`; setup-wizard login page only in classic UI.

- Global jobs: `qmlui/jobs/TaskCenter` (QML `Tasks`, singleton `TaskCenter::instance()->track(task, title, kind, instanceId)`)
  — every long task must be tracked there; `TaskIndicator.qml` pill at the window bottom (StackView reserves a strip),
  pending "create" cards on PageInstances, per-instance status line on tiles.
- QML mode never shows the classic setup wizard: `createSetupWizard` applies quiet defaults (language = system locale,
  auto Java). App icon: `setWindowIcon(logo())` at shell start, POST_BUILD copies .icns into the build-tree bundle,
  `FashionLauncher.icon` (Icon Composer) rebuilt from our art (compiled only with Xcode 26 actool).
- UI languages: table of 15 in `LauncherBridge` (en ru es pt_BR de fr it pl uk tr zh_CN ja ko cs id), but
  `languages()` offers only the `ready` list (en_US, ru) until a .ts is complete. Why: half-English UI looks broken.
  `i18n/qmlui_<code>.ts`; script
  `launcher/qmlui/i18n/update.sh` re-runs lupdate for all and reports unfinished counts.

- 2026-11-08: merged into main: play stats, game settings transfer, Quick Play Continue, Storage cleanup, crash
  doctor (translations re-merged by script from each branch; 1592 strings, 15 langs complete).
- Play statistics: `qmlui/stats/PlayStats` (QML `Stats`, singleton). `BaseInstance::setMinecraftRunning(false)` calls
  `PlayStats::record` (only when RecordGameTime is on, sessions >= 30 s) -> `<data>/stats/sessions.jsonl`. One-time
  backfill from `logs/YYYY-MM-DD-N.log.gz` + `latest.log` (first/last `[HH:mm:ss]`), marker `stats/backfill.done`.
  `summary(firstDayOfWeek)` mixes in Prism's per-instance totals (authoritative for totals/top instances). UI:
  `InstancesStats.qml`, tab on PageInstances via `Router.instancesTab` ("grid"|"stats"); snapshot page `stats`.
- Game settings transfer: `tools/GameOptions` (QML `GameOptions`: summary/plan/apply/undo) merges options.txt (+optionsof,
  optionsshaders with '=', sodium-options.json) by category; converts keys LWJGL2↔GLFW across 1.13, lang case (1.11),
  graphicsMode (1.16), mainHand quotes (1.19); target MC version from PackProfile, else its DataVersion. New target
  file gets the source's `version:` so the game's DataFixers upgrade it. Backups `<file>.bak-<date>` (3 kept).
  UI `GameOptionsDialog` + `InstanceChooser`, opened via `Router.copyGameOptions({source,target})`; toast with button
  `Router.toastAction(msg, label, fn)`. Snapshot: `FFPRISM_PAGE=gameopts:<src>[:<target|*>]`.
- Worktrees: libraries/libnbtplusplus submodule is empty — copy it from /Users/fash/projects/ffprism/PrismLauncher before configuring.
- Quick Play "Continue" (branch qml-quickplay): `qmlui/quickplay/QuickPlayBridge` (QML `QuickPlay`: support/last/
  worlds/servers/play). World = trait `feature:is_quick_play_singleplayer` (1.20+), server = any non-legacy version
  (old ones get --server/--port). Traits read from cached `meta/net.minecraft/<v>.json` while components still
  resolve (async). Last target = newest of level.dat LastPlayed (World needs `loadMetadata()`), last "Connecting to"
  in logs/latest.log, instance setting `QmlLastQuickPlay`. One-shot target: `LauncherBridge::launchTo` (kept across
  pre-launch hold). UI: `ContinueButton` (Home, instance header), `QuickPlayDialog` via `Router.pickQuickPlay`.
  Snapshot pages `home:<id>`, `quickplay:<id>`.
- QuietButton: implicitWidth from its parts (labels collapsed to 0 after 64b9ccec7, fixed on merge); `compact` folds a
  button to its icon with the label as tooltip — Home folds the secondary actions when the row doesn't fit.
- New worktrees need `git submodule update --init` before configuring.
- Free up disk space: `qmlui/storage/StorageScan` (worker logic, rules in its header: unused libraries/assets from
  resolved launch profiles, cache/ except qmlui + top-level files, old logs, leftovers, dedupe via ContentStore,
  extra snapshots + store gc, screenshots opt-in; never saves/ or symlinks; hard-link-aware sizes) +
  `StorageBridge` (QML `Storage`, jobs in TaskCenter, dev env FFPRISM_STORAGE_LIST / FFPRISM_STORAGE_CLEAN).
  UI `SettingsStorage.qml` = Settings → Storage; `FFPRISM_PAGE=settings:storage`. meta/ never cleaned (offline
  instances need it). Done on branch qml-cleanup (commit e84a2018b), tested on devdata with backup/restore, 15 langs.
  Note: devdata's fake instances (bogus loader versions) make libraries/assets show "can't check" — by design.
- Crash doctor: `qmlui/doctor/CrashRules` (pure matcher, tests `tests/CrashRules_test.cpp` + `tests/testdata/CrashRules/`,
  run `build/LauncherTests CrashRules_test`) + `CrashDoctor` singleton (QML `Doctor`, hooks `Application::launchStarted`,
  pending crash survives the shell being unloaded) + `CrashDoctorDialog.qml` (DialogHost, kind "crash"; TabLog button).
  Rule: only causes the log states (loader dependency resolution, OOM, Java class version, GLFW/OpenGL, corrupt jar in
  mods/); never mixin/conflict/stack-trace guesses — those show "unknown" + excerpt. Why: in big packs the mods named in
  an error are often not the culprits. Debug: `FFPRISM_PAGE=crash:<instanceId>:<log path>`.

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

- Translations (after every UI change), in `launcher/qmlui`: `lupdate qml/*.qml qml/*.js $(find . -name "*.cpp")
  -ts i18n/qmlui_ru.ts -locations none -no-obsolete`, fill unfinished entries (plurals: 3 Russian forms), `lrelease`.

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
- 2026-11-08 (branch glm-mc-upgrade): "Move to another Minecraft version" (modded instances).
  `qmlui/upgrade/` — `UpgradeLogic.h` (pure: classify/delta/defaultName/loader rules, tests
  `tests/Upgrade_test.cpp`), `UpgradePlanTask` (Modrinth batched by hash + CurseForge per project,
  loader availability check, icons, up to 2 Modrinth alternatives per missing mod), `UpgradeBridge`
  (QML `Upgrade`: startPlan/apply; apply = InstanceCopyTask → LauncherBridge::changeVersion on the
  copy → ResourceDownloadTask replacements → disable rest (*.disabled) → UPGRADE-REPORT.txt in the
  copy; kind "upgrade" in TaskCenter). UI `UpgradeDialog.qml` (page), one action in TabOverview,
  debug `FFPRISM_PAGE=upgrade:<id>[:<mc>[:apply]]`. Verified end-to-end on /tmp/devdata-up with a
  fabric 1.20.1 test instance (sodium replaced, starlight disabled + ScalableLux suggestion).
  Note: this worktree's shell runs under Rosetta — configure with -DCMAKE_OSX_ARCHITECTURES=arm64.
- 2026-11-06 (Antigravity):
  - Русский перевод завершён на 100% (1379 из 1379 строк переведены, `qmlui_ru.qm` собран).
  - Устранён краш (SIGSEGV 139) на выходе из приложения в QmlShell/Application.
  - Устранён зомби-процесс `fashion-net` при закрытии.
  - Настроен полноценный автономный деплой macOS (`macdeployqt` с флагом `-qmldir`), исключающий внешние зависимости Homebrew.
  - Собраны релизные пакеты: `FashionLauncher.dmg` (96 МБ) и `FashionLauncher.zip` (107 МБ).
  - Установлен в `/Applications/Fashion Launcher.app`.
  - Ветка `try-all` обновлена и готова к тестированию.
- Rebranded to Fashion Launcher (binary `fashionlauncher`, app id `io.github.fashffffffff.FashionLauncher`; build
  output `build/fashionlauncher.app`; config `fashionlauncher.cfg` falls back to `prismlauncher.cfg`). Icon from
  Gemini in `program_info/` (source `fashion-icon-source.png`). MSA client ID and CurseForge key are still Prism's —
  must be replaced before any public release.
- 2026-11-07: `main` = everything (all feature branches, 15 languages 100%, CI for Linux/Windows), pushed to
  github.com/fashffffffff/fashionlauncher. Build dirs were wiped; debug is re-configured from scratch in `build/`.
- Visual pass 1 (user: "do it, but with QUALITY"; gradients read as AI slop — never use decorative gradients, no
  glow circles): block buttons (`Style.blockDepth`, `Style.edge()`, face presses down), `Style.inkOn()` auto-contrast,
  stepped hard shadow on the Home title, hotbar slots as blocks, sidebar Create is a neutral block (Play is the only
  loud button), instance cards without a cover show a staggered pattern of the instance icon.
- Visual pass 2: display font Dela Gothic One (user's pick; Unbounded read as "the font every AI picks"; Tektur and Unbounded stay selectable),
  house themes `fashion-dark.json` (default, `kDefaultTheme`) and `fashion.json` (light; accent #C42D70 so white ink
  passes 4.5:1), sorted first in the list; selected hotbar item glows via MultiEffect shadow along its silhouette
  (user: glow is fine if it looks like real glow, not a flat circle). Next: 12 Gemini wallpapers (anime + Minecraft)
  generating into `/Users/fash/projects/ffprism/wallgen` → add to `qmlui/wallpapers/wallpapers.json`.
- Skins fix: `SkinList::update` no longer forces the account's arm model/cape onto the existing entry — saving
  index.json re-triggers update via the folder watcher, so every local slim/cape change was instantly reverted.
- Running side sessions (worktrees `/Users/fash/projects/ffprism/wt/<branch>`, from try-all): qml-share (pack
  manifests/links, API used by p2p), qml-p2p (Rust sidecar `tools/fashion-net`, friends, play together; host and
  joiner choose server-only vs all mods + exclusions, reuse matching instance), qml-store (hardlink store + 5
  snapshots + rollback), qml-tools (pre-launch checks, changelogs, import from launchers, pack settings), qml-visual
  (panorama, shader backgrounds, pack covers), qml-gallery (community gallery + UI sounds), qml-skins (3D skins,
  servers on Home/tab). Next: review & merge each into try-all, translate RU, build, verify, ask to push.
- Pending: anime/extra wallpapers (Gemini 429); public gallery repo fashffffffff/fashion-gallery needs user OK.

- Languages (2026-11-06): new UI offers exactly 15 (table `g_uiLanguages` in LauncherBridge.cpp: Prism key, qm code,
  native name). Setting "Language" stores Prism's key (Simplified Chinese = `zh`, file `qmlui_zh_CN`); other saved keys
  are mapped to the closest (`supportedLanguage`) on start and when the translations index arrives. setLanguage also
  calls `updateLanguage` so Prism's core .po downloads.

## Gotchas
- `anyAccountIsValid()` really means "some MS account owns Minecraft" — don't use it to mean "has accounts".
- Multiple Claude sessions may share this checkout: don't switch branches/stash; keep to your own files.
- QML: don't name custom props like FINAL base props (`flat`, Canvas `top`, AbstractButton `icon`) — the component fails to load.
- zsh: unquoted `--include=*.cpp` globs fail ("no matches found") — quote them.
