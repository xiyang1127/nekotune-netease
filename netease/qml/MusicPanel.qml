pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "qrc:/qml/components" as UI
Item {
    id: root
    required property var client
    required property var translator
    property string mode: "search"
    signal settingsRequested()
    function t(key) { return translator.text(key, translator.language) }
    function search() { client.browse(mode, query.text.trim(), 1) }
    ColumnLayout {
        anchors.fill: parent; spacing: 12
        RowLayout {
            Layout.fillWidth: true
            Label { Layout.fillWidth: true; text: root.t("title"); color: UI.Theme.textPrimary; font.pixelSize: 24; font.bold: true }
            UI.TextButton { text: root.t("open_settings"); subtle: true; onClicked: root.settingsRequested() }
        }
        Label {
            Layout.fillWidth: true; text: root.t("direct_help")
            wrapMode: Text.WordWrap; color: UI.Theme.textMuted
        }
        RowLayout {
            Repeater {
                model: ["search", "playlist", "song"]
                UI.TextButton {
                    required property string modelData
                    text: root.t(modelData); subtle: root.mode !== modelData
                    onClicked: root.mode = modelData
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            UI.InputField {
                id: query; objectName: "neteaseQuery"
                Layout.fillWidth: true; Layout.minimumWidth: 0
                placeholderText: root.t(root.mode === "search" ? "query_hint" : "id_hint")
                Accessible.name: placeholderText
                maximumLength: root.mode === "search" ? 128 : 20
                enabled: root.client.ready
                onAccepted: if (!root.client.busy && text.trim()) root.search()
            }
            UI.TextButton {
                objectName: "neteaseSearch"
                text: root.t(root.client.busy ? "loading" : root.mode === "search" ? "search" : "browse")
                enabled: root.client.ready && !root.client.busy && query.text.trim().length > 0
                onClicked: root.search()
            }
        }
        Label {
            Layout.fillWidth: true; visible: Boolean(root.client.message)
            text: root.t(root.client.message); textFormat: Text.PlainText; wrapMode: Text.WordWrap
            color: root.client.failed ? UI.Theme.statusError : UI.Theme.statusSuccess
        }
        ListView {
            id: results; objectName: "neteaseResults"
            Layout.fillWidth: true; Layout.fillHeight: true; clip: true
            model: root.client.tracks; spacing: 4; currentIndex: -1
            onModelChanged: currentIndex = -1
            ScrollBar.vertical: ScrollBar {}
            delegate: Rectangle {
                id: row
                required property var modelData
                required property int index
                width: results.width; height: 78; radius: UI.Theme.radiusSm
                color: results.currentIndex === index ? UI.Theme.bgSelected : UI.Theme.bgSurface
                border.color: activeFocus ? UI.Theme.accentPrimary : "transparent"
                Keys.onReturnPressed: if (results.currentIndex === index) root.client.action(modelData, "play")
                Keys.onEnterPressed: if (results.currentIndex === index) root.client.action(modelData, "play")
                MouseArea {
                    anchors.fill: parent
                    onClicked: { results.currentIndex = row.index; row.forceActiveFocus() }
                    onDoubleClicked: root.client.action(row.modelData, "play")
                }
                RowLayout {
                    anchors.fill: parent; anchors.margins: 10; spacing: 10
                    Item {
                        Layout.preferredWidth: 48; Layout.preferredHeight: 48
                        Image { anchors.fill: parent; source: "qrc:/artwork/default-cover.png"; fillMode: Image.PreserveAspectCrop }
                        Image {
                            objectName: "neteaseCover"; anchors.fill: parent
                            source: row.modelData.cover_url || ""; asynchronous: true; mipmap: true
                            fillMode: Image.PreserveAspectCrop; visible: status === Image.Ready
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true; Layout.minimumWidth: 0; spacing: 4
                        Label {
                            objectName: "neteaseTrackTitle"; Layout.fillWidth: true; Layout.minimumWidth: 0
                            text: row.modelData.title || root.t("untitled"); textFormat: Text.PlainText
                            color: UI.Theme.textPrimary; elide: Text.ElideRight; font.bold: true
                        }
                        Label {
                            Layout.fillWidth: true; Layout.minimumWidth: 0
                            text: [row.modelData.artist, row.modelData.album].filter(Boolean).join(" · ")
                            textFormat: Text.PlainText; color: UI.Theme.textMuted; elide: Text.ElideRight
                        }
                    }
                    UI.TextButton { text: root.t("play"); subtle: true; enabled: !root.client.actionBusy && root.client.ready; onClicked: root.client.action(row.modelData, "play") }
                    UI.TextButton { objectName: "neteaseEnqueue"; text: root.t("enqueue"); subtle: true; enabled: !root.client.actionBusy && root.client.ready; onClicked: root.client.action(row.modelData, "enqueue") }
                    UI.TextButton {
                        objectName: "neteaseDownload"; text: root.t("download"); subtle: true
                        enabled: !root.client.actionBusy && !root.client.downloadTask && root.client.ready
                        onClicked: root.client.action(row.modelData, "download")
                    }
                }
            }
            Label {
                anchors.centerIn: parent; width: parent.width; horizontalAlignment: Text.AlignHCenter
                visible: results.count === 0; wrapMode: Text.WordWrap; color: UI.Theme.textMuted
                text: root.t(root.client.busy ? "loading" : root.client.searched ? "empty" : "start")
            }
        }
        RowLayout {
            Layout.fillWidth: true; visible: Boolean(root.client.downloadTask)
            Label { Layout.fillWidth: true; text: root.t("downloading"); color: UI.Theme.textSecondary }
            UI.TextButton { text: root.t("cancel"); subtle: true; onClicked: root.client.cancel() }
        }
        RowLayout {
            Layout.fillWidth: true
            Label { Layout.fillWidth: true; text: root.t("count").replace("{count}", results.count); color: UI.Theme.textMuted }
            UI.TextButton {
                objectName: "neteasePrevious"; text: root.t("previous"); subtle: true
                visible: root.client.submittedMode === "search"; enabled: !root.client.busy && root.client.page > 1
                onClicked: root.client.browse("search", root.client.submittedQuery, root.client.page - 1)
            }
            Label { text: root.t("page").replace("{page}", root.client.page); color: UI.Theme.textMuted; visible: root.client.submittedMode === "search" }
            UI.TextButton {
                objectName: "neteaseNext"; text: root.t("next"); subtle: true
                visible: root.client.submittedMode === "search"; enabled: !root.client.busy && Boolean(root.client.cursor)
                onClicked: root.client.browse("search", root.client.submittedQuery, Number(root.client.cursor))
            }
        }
    }
}
