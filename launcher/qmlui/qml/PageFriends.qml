import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Friends and playing together without a server: your code, requests, invites, friends list.
Item {
    id: page
    property var params: ({})

    readonly property var hostSession: Friends.hosting.invites && Friends.hosting.invites.length ? Friends.hosting.invites[0] : null
    readonly property var sortedFriends: Friends.friends.slice().sort((a, b) => {
        const rank = f => f.pending ? 2 : f.online ? 0 : 1
        return rank(a) - rank(b) || a.name.localeCompare(b.name)
    })

    FriendsPlayDialog { id: playDialog }
    FriendsJoinDialog { id: joinDialog }
    Timer {
        // test hook for snapshots
        running: Friends.testDialog().length > 0
        interval: 15000
        onTriggered: {
            if (Friends.testDialog() === "play") playDialog.openFor(Router.selectedId, [])
            else if (Friends.invites.length) joinDialog.openFor(Friends.invites[0])
        }
    }

    ScrollView {
        id: scroll
        anchors.fill: parent
        contentWidth: availableWidth
        clip: true

        RowLayout {
            width: scroll.availableWidth - Style.px(64)
            x: Style.px(32)
            y: Style.px(28)
            spacing: Style.px(24)

            // ---- main column ----
            ColumnLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                Layout.bottomMargin: Style.px(56)
                spacing: Style.px(16)

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Style.px(12)
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Style.px(4)
                        Txt { text: qsTr("Friends"); font.family: Style.display; font.pixelSize: Style.fontHero * 0.6 }
                        Txt {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            muted: true
                            text: qsTr("Play together without a server or any setup. Your computers connect directly.")
                        }
                    }
                    PrimaryButton {
                        text: qsTr("Play together"); iconName: "play"
                        enabled: Friends.available
                        onClicked: playDialog.openFor(page.params.instanceId || Router.selectedId, [])
                    }
                }

                // Invites to you
                Repeater {
                    model: Friends.invites
                    delegate: FriendsInviteCard {
                        Layout.fillWidth: true
                        invite: modelData
                        onJoinRequested: inv => joinDialog.openFor(inv)
                    }
                }

                // Your session as host
                FriendsHostCard {
                    visible: !!page.hostSession
                    Layout.fillWidth: true
                    session: page.hostSession || ({})
                }

                // Friend requests
                Rectangle {
                    visible: Friends.requests.length > 0
                    Layout.fillWidth: true
                    implicitHeight: reqCol.implicitHeight + Style.px(32)
                    radius: Style.radius
                    color: Style.panel(0)
                    border.width: 1
                    border.color: Style.panelBorder
                    ColumnLayout {
                        id: reqCol
                        anchors { left: parent.left; right: parent.right; top: parent.top; margins: Style.px(16) }
                        spacing: Style.px(8)
                        Txt { text: qsTr("Friend requests"); font.weight: Font.DemiBold }
                        Repeater {
                            model: Friends.requests
                            delegate: RowLayout {
                                Layout.fillWidth: true
                                spacing: Style.px(12)
                                FriendsAvatar { name: modelData.name || "?"; online: true }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 0
                                    Txt { text: modelData.name || qsTr("Someone"); font.weight: Font.DemiBold; Layout.fillWidth: true }
                                    Txt { text: qsTr("wants to be friends"); muted: true; font.pixelSize: Style.fontSmall }
                                }
                                PrimaryButton { text: qsTr("Accept"); implicitHeight: Style.px(40); implicitWidth: Style.px(110); onClicked: Friends.acceptRequest(modelData.key) }
                                QuietButton { text: qsTr("Decline"); onClicked: Friends.declineRequest(modelData.key) }
                            }
                        }
                    }
                }

                // Friends list
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: listCol.implicitHeight + Style.px(32)
                    radius: Style.radius
                    color: Style.panel(0)
                    border.width: 1
                    border.color: Style.panelBorder
                    ColumnLayout {
                        id: listCol
                        anchors { left: parent.left; right: parent.right; top: parent.top; margins: Style.px(16) }
                        spacing: Style.px(4)
                        RowLayout {
                            Layout.fillWidth: true
                            Txt { text: qsTr("Your friends"); font.weight: Font.DemiBold; Layout.fillWidth: true }
                            Txt {
                                visible: Friends.friends.length > 0
                                muted: true
                                font.pixelSize: Style.fontSmall
                                text: qsTr("%1 online").arg(Friends.onlineCount)
                            }
                        }
                        Txt {
                            visible: Friends.friends.length === 0
                            Layout.fillWidth: true
                            Layout.topMargin: Style.px(6)
                            wrapMode: Text.WordWrap
                            muted: true
                            text: qsTr("No friends yet. Copy your code and send it to a friend, or paste theirs on the right.")
                        }
                        Repeater {
                            model: page.sortedFriends
                            delegate: FriendRow {
                                Layout.fillWidth: true
                                friend: modelData
                                onInvite: playDialog.openFor(page.params.instanceId || Router.selectedId, [modelData.key])
                            }
                        }
                    }
                }
            }

            // ---- side column ----
            FriendsCodeCard {
                Layout.preferredWidth: Math.max(Style.px(350), Math.min(Style.px(420), scroll.availableWidth * 0.4))
                Layout.minimumWidth: Style.px(320)
                Layout.alignment: Qt.AlignTop
            }
        }
    }
}
