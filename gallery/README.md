# Fashion Launcher gallery

Themes and backgrounds shown in **Appearance → Find more themes**. The launcher reads a single static
`index.json`; there is no server. The public gallery lives in a GitHub repository
(default index: `https://raw.githubusercontent.com/fashffffffff/fashion-gallery/main/index.json`, changeable in the
gallery page). This folder is a working sample with the same layout: the launcher falls back to it when the
public index can't be reached, and you can copy it as the starting point of the gallery repository.

## index.json

```json
{
    "version": 1,
    "items": [
        {
            "id": "copper-age",
            "type": "theme",
            "name": "Copper Age",
            "author": "Someone",
            "description": "One or two sentences.",
            "preview": "previews/copper-age.jpg",
            "file": "themes/copper-age.json",
            "tags": ["dark", "warm"],
            "downloads": 1280
        }
    ]
}
```

- `id`: unique, lowercase letters, digits, `-` and `_`.
- `type`: `theme` or `background`.
- `file`, `preview`: relative to `index.json` or full `https://` addresses. `preview` is optional for themes (the
  launcher draws a preview from the theme colors); for backgrounds use a ~480×270 JPEG.
- `colors` (optional, themes): `{ "bg", "surface", "text", "accent" }` to skip downloading the theme for its card.
- `downloads` is optional and only displayed.

## Rules the launcher enforces

Themes: at most 64 KB, JSON with only the keys of `launcher/qmlui/themes/base.json` (plus `description` and the
background keys `wallpaper`, `video`, `slideshow`, `interval`, `color`); colors as `#RRGGBB` or `#RRGGBBAA`; file names
in `background` must be plain names without folders. Anything else is refused.

Backgrounds: at most 40 MB; `png`, `jpg`, `webp` (checked to really be an image) or `mp4`, `webm`, `mov`, `m4v`.

## Submitting

1. In the launcher: Appearance → **Open themes folder**, take your theme's `.json` file.
2. Fork the gallery repository, add the file under `themes/` (or your picture under `backgrounds/` with a preview
   under `previews/`), add an entry to `index.json`, and open a pull request.

Only submit work you made yourself or are allowed to share.
