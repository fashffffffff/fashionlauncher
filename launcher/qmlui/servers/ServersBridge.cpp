// SPDX-License-Identifier: GPL-3.0-only
#include "ServersBridge.h"

#include <QQmlEngine>

#include "ServerListModel.h"

ServersBridge::ServersBridge(QObject* parent) : QObject(parent)
{
    static bool registered = false;
    if (!registered) {
        registered = true;
        qmlRegisterType<ServerListModel>("FFPrism.Servers", 1, 0, "ServersModel");
    }
}
