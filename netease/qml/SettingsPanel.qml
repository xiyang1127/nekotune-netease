pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "qrc:/qml/components" as UI
Item {
    id: root
    required property var client
    required property var translator
    property bool saving: false
    property bool failed: false
    property string message: ""
    property string selectedBitrate: "320"
    property bool wordLyrics: true
    property bool translation: true
    readonly property bool dirty: selectedBitrate !== client.status.bitrate
        || wordLyrics !== client.status.word_lyrics || translation !== client.status.translation
    function t(key) { return translator.text(key, translator.language) }
    function sync() {
        selectedBitrate = client.status.bitrate || "320"
        wordLyrics = client.status.word_lyrics !== false
        translation = client.status.translation !== false
    }
    Connections { target: root.client; function onStatusChanged() { root.sync() } }
    Component.onCompleted: sync()
    ScrollView {
        id: scroll; anchors.fill: parent; contentWidth: availableWidth; clip: true
        ColumnLayout {
            width: scroll.availableWidth; spacing: 14
            Label { text: root.t("settings"); color: UI.Theme.textPrimary; font.pixelSize: 24; font.bold: true }
            Label { Layout.fillWidth: true; text: root.t("direct_help"); wrapMode: Text.WordWrap; color: UI.Theme.textMuted }
            RowLayout {
                Label { text: root.t("bitrate"); color: UI.Theme.textSecondary }
                Repeater {
                    model: ["128", "192", "320"]
                    UI.TextButton {
                        required property string modelData
                        objectName: "neteaseBitrate" + modelData
                        text: modelData; subtle: root.selectedBitrate !== modelData
                        enabled: root.client.ready && !root.saving
                        Accessible.name: root.t("bitrate") + " " + modelData + " kbps"
                        onClicked: root.selectedBitrate = modelData
                    }
                }
                Label { text: "kbps"; color: UI.Theme.textMuted }
            }
            RowLayout {
                Label { text: root.t("word_lyrics"); color: UI.Theme.textSecondary }
                UI.TextButton {
                    objectName: "neteaseWordLyrics"
                    text: root.t(root.wordLyrics ? "on" : "off"); subtle: !root.wordLyrics
                    enabled: root.client.ready && !root.saving
                    onClicked: root.wordLyrics = !root.wordLyrics
                }
            }
            RowLayout {
                Label { text: root.t("translation"); color: UI.Theme.textSecondary }
                UI.TextButton {
                    objectName: "neteaseTranslation"
                    text: root.t(root.translation ? "on" : "off"); subtle: !root.translation
                    enabled: root.client.ready && !root.saving
                    onClicked: root.translation = !root.translation
                }
            }
            Label { Layout.fillWidth: true; text: root.t("lyrics_help"); wrapMode: Text.WordWrap; color: UI.Theme.textMuted }
            Label { Layout.fillWidth: true; text: root.t("permission_help"); wrapMode: Text.WordWrap; color: UI.Theme.textMuted }
            UI.TextButton {
                objectName: "neteaseSave"; text: root.t("save")
                enabled: root.client.ready && !root.saving && root.dirty
                onClicked: {
                    root.saving = true; root.message = ""; root.failed = false
                    root.client.api.invoke("config.set", {bitrate: root.selectedBitrate, word_lyrics: root.wordLyrics, translation: root.translation}).then(value => {
                        root.client.acceptStatus(value); root.message = "saved"; root.saving = false
                    }).catch(error => {
                        root.message = root.client.errorKey(error); root.failed = true; root.saving = false
                    })
                }
            }
            Label {
                Layout.fillWidth: true; visible: Boolean(root.message || root.client.message)
                text: root.t(root.message || root.client.message); wrapMode: Text.WordWrap; textFormat: Text.PlainText
                color: root.failed || root.client.failed ? UI.Theme.statusError : UI.Theme.statusSuccess
            }
        }
    }
}
