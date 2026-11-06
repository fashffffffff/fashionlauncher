# Recommended settings inside a modpack

Modpack authors can tell Fashion Launcher how much memory and which Java arguments their pack needs.
Put a file named `fashion-launcher.json` at the **root** of the `.mrpack` or `.zip` (next to
`modrinth.index.json` or `manifest.json`, not inside `overrides/`).

```json
{
  "memory": { "min": 2048, "max": 8192 },
  "jvmArgs": "-XX:+UseG1GC -XX:MaxGCPauseMillis=50",
  "notes": "Shaders need at least 10 GB of memory."
}
```

| Field        | Type   | Meaning |
|--------------|--------|---------|
| `memory.max` | number | Maximum memory for the game, in MiB. |
| `memory.min` | number | Starting memory, in MiB. Optional; it can't be more than `max`. |
| `jvmArgs`    | string | Extra Java arguments, on one line (up to 4096 characters). |
| `notes`      | string | Anything the player should know. It goes into the instance's notes. |

Every field is optional. Anything else in the file is ignored, so other launchers can add their own fields.

## What happens on import

When someone imports the pack (from a file, a link, or from Modrinth/CurseForge in the launcher):

- `memory` turns on the instance's own memory settings with these values. If `max` is more than 90% of
  the computer's memory, the launcher lowers it and says so.
- `jvmArgs` turns on the instance's own Java arguments with this text.
- `notes` and a short summary of what changed are added to the instance's notes.
- The player sees a message such as “This modpack set 8 GB of memory.”

Players can change all of it later in the instance's settings.

## Exporting

`Tools.writePackSettings(instanceId, archivePath)` (QML) writes the instance's own memory and Java
argument settings into an exported archive, replacing an older `fashion-launcher.json`. With no
`archivePath` it writes the file into the instance folder, so an instance export (`.zip`) carries it at
its root. If the instance uses the default settings, nothing is written.
