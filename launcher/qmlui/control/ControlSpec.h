// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QJsonArray>
#include <QString>

/**
 * The launcher's remote-control commands, described once for every client: the MCP server
 * (`fashionlauncher mcp`), the command line (`fashionlauncher ctl …`) and the control server inside
 * the running launcher (which rejects names it doesn't know).
 *
 * Each entry: { name, description, inputSchema } — the MCP tool shape (JSON Schema for arguments).
 */
namespace ControlSpec {

/// All commands as MCP tool objects
QJsonArray tools();

/// Whether a command name exists
bool has(const QString& name);

/// Base name of the per-user registry folder that running launchers announce themselves in
QString registryDir();

/// Protocol version spoken between the clients and the control server
constexpr int protocolVersion = 1;

}  // namespace ControlSpec
