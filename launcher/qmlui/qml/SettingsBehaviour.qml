import QtQuick
import QtQuick.Layouts

// What the launcher does when a game starts and stops. Global or per instance.
SettingsSection {
    id: bs
    group: "behaviour"
    title: qsTr("When you play")
    description: qsTr("What the launcher and the game log window do while a game runs.")

    SettingsSwitch {
        label: qsTr("Close the launcher when the game opens")
        checked: bs.val("CloseAfterLaunch") === true
        onToggled: (on) => bs.put("CloseAfterLaunch", on)
    }
    SettingsSwitch {
        label: qsTr("Quit the launcher when the game closes")
        checked: bs.val("QuitAfterGameStop") === true
        onToggled: (on) => bs.put("QuitAfterGameStop", on)
    }
    SettingsSwitch {
        label: qsTr("Show the game log when the game starts")
        checked: bs.val("ShowConsole") === true
        onToggled: (on) => bs.put("ShowConsole", on)
    }
    SettingsSwitch {
        label: qsTr("Show the game log if the game crashes")
        checked: bs.val("ShowConsoleOnError") === true
        onToggled: (on) => bs.put("ShowConsoleOnError", on)
    }
    SettingsSwitch {
        label: qsTr("Close the game log when the game exits normally")
        checked: bs.val("AutoCloseConsole") === true
        onToggled: (on) => bs.put("AutoCloseConsole", on)
    }
}
