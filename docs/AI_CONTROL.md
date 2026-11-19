# Controlling Fashion Launcher with AI assistants

Fashion Launcher can be driven by AI agents such as Claude Code, Claude Desktop, Codex, Cursor and anything
else that speaks [MCP](https://modelcontextprotocol.io), or by scripts through a JSON command line.

Everything stays on your computer: the running launcher listens on a local socket that only your OS user can
open (no network port). Account passwords and tokens are never returned. Deleting an instance always asks you
in the launcher window first.

## Connect an AI app

**Settings → AI assistants** has a *Connect* button for Claude Code, Claude Desktop and Codex. It adds the
launcher to that app's MCP servers (config files are backed up as `*.before-fashion-launcher` before the first
change). Restart the app, or open a new Claude Code session, and ask it things like:

- “Create a 1.21.1 Fabric instance called Survival and add Sodium, Iris and Mod Menu”
- “Why did my modpack crash?”
- “Start Survival and join play.example.org”

For any other MCP app, copy the snippet from the same page, which looks like:

```json
{
  "mcpServers": {
    "fashion-launcher": {
      "command": "/Applications/Fashion Launcher.app/Contents/MacOS/fashionlauncher",
      "args": ["mcp"]
    }
  }
}
```

On Windows the command is the path to `fashionlauncher.exe`; on Linux the launcher binary or AppImage.

## Command line

The same commands work from a terminal and print JSON:

```sh
fashionlauncher ctl help                 # list of commands
fashionlauncher ctl help install_mods    # arguments of one command
fashionlauncher ctl list_instances
fashionlauncher ctl search_mods Survival --query sodium
fashionlauncher ctl install_mods Survival --project-ids AANobbMI,mOgUt4GM
fashionlauncher ctl launch Survival --server play.example.org
fashionlauncher ctl get_log Survival --lines 100 --level error
```

The first bare word is the instance (id or name). `--json '{…}'` passes arguments as JSON. If the launcher is
not running, the command starts it. Add `-d <folder>` to talk to a launcher started with a custom data folder.

## Commands

| Command | What it does |
|---|---|
| `status` | Version, data folder, default account, running games, background jobs |
| `list_instances`, `get_instance` | Instances and their versions, loader, memory, counts |
| `list_minecraft_versions` | Installable Minecraft versions |
| `create_instance` | New instance (vanilla, Fabric, Quilt, Forge, NeoForge); waits until ready |
| `rename_instance`, `set_instance_settings` | Name, memory, JVM arguments |
| `delete_instance` | Deletes after you confirm in the launcher |
| `list_mods`, `set_mod_enabled`, `remove_mod` | Mods, resource packs and shaders (removal goes to the trash) |
| `search_mods`, `install_mods` | Modrinth / CurseForge search and install with dependencies |
| `list_worlds` | Singleplayer worlds |
| `launch`, `stop` | Start (optionally into a server or world) and stop the game |
| `get_log`, `diagnose_crash` | Game log and crash explanation |
| `list_accounts`, `set_default_account` | Player names and the default account (no tokens) |

The protocol is one JSON object per line over the local socket: `{"id":1,"cmd":"list_instances","args":{}}` →
`{"id":1,"ok":true,"result":…}`. The command list with JSON schemas is in
`launcher/qmlui/control/ControlSpec.cpp` (`fashionlauncher ctl tools` prints it).
