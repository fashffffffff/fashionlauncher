# Built-in QML UI themes

Each `*.json` here is a built-in theme, listed in the UI as `builtin:<file name>`.
User themes use the same format and live in `<data dir>/ui-themes` (`user:<file name>`).

- `base.json` is the complete schema: every key, with default values. It is not
  shown as a theme. Every other theme is deep-merged over it, so a preset only lists
  what differs. The loader is `../UiTheme.cpp`.
- If you add a key, add it to `base.json` first, and document it in
  `docs/THEMING.md` (the user guide, in Russian).
- Built-in presets should define every `colors` key. Minimum contrast (WCAG):
  `text` on `bg` 7:1, `textMuted` on `surface` 4.5:1, `onAccent` on `accent` 4.5:1.
- `background.image` is a file name relative to the theme's folder. Modes:
  `screenshot`, `image`, `pattern`, `solid`.
- User theme files are watched, so saving one reloads it live.
- New built-in files must also be listed in the Qt resource file, so they get
  bundled under `:/qmlui/themes`.
