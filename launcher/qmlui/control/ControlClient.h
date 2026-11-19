// SPDX-License-Identifier: GPL-3.0-only
#pragma once

/**
 * The client side of the launcher's remote control, run instead of the launcher when the first
 * argument is "ctl" or "mcp" (see main.cpp). Finds the running launcher (starting it when needed)
 * and talks to its ControlServer.
 *
 *   fashionlauncher ctl [-d DIR] help [command]
 *   fashionlauncher ctl [-d DIR] <command> [instance] [--arg value …] [--json '{…}']
 *   fashionlauncher mcp [-d DIR]          — MCP server on stdin/stdout for AI agents
 */
namespace ControlClient {

/// Whether argv asks for a client mode (ctl / mcp)
bool wanted(int argc, char* argv[]);

/// Run the client and return the process exit code
int run(int argc, char* argv[]);

}  // namespace ControlClient
