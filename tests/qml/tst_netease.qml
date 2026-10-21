import QtQuick
import QtQuick.Controls
import QtTest
import "../../netease/qml" as Plugin

Rectangle {
    id: scene
    width: 980; height: 680; color: "#0E0D14"
    Plugin.Translator { id: pluginTranslator }
    QtObject {
        id: fakeApi
        property string ownerId: "nekotune.netease"
        property var calls: []
        property var resolveSearch
        property var rejectSearch
        property bool failSave: false
        function invoke(service, params) {
            calls = calls.concat([{method: service, params: params}])
            if (service === "status") return Promise.resolve({bitrate: "320", word_lyrics: true, translation: true})
            if (service === "config.set") return failSave ? Promise.reject(new Error("netease_invalid_config")) : Promise.resolve(params)
            return new Promise((resolve, reject) => { resolveSearch = resolve; rejectSearch = reject })
        }
        function call(method, params) {
            calls = calls.concat([{method: method, params: params}])
            return Promise.resolve({task: "download-task", queue_id: 1})
        }
    }
    Component { id: controllerComponent; Plugin.Controller { api: fakeApi } }
    Component {
        id: panelComponent
        Plugin.MusicPanel { anchors.fill: parent; anchors.margins: 20; client: tests.controller; translator: pluginTranslator }
    }
    Component {
        id: settingsComponent
        Plugin.SettingsPanel { anchors.fill: parent; anchors.margins: 20; client: tests.controller; translator: pluginTranslator }
    }
    TestCase {
        id: tests
        name: "NeteasePlugin"
        when: windowShown
        property var controller
        property var panel
        function init() {
            scene.width = 980; pluginTranslator.language = "zh"
            fakeApi.calls = []; fakeApi.failSave = false
            fakeApi.resolveSearch = undefined; fakeApi.rejectSearch = undefined
            controller = createTemporaryObject(controllerComponent, scene)
            verify(controller !== null)
            tryCompare(controller, "ready", true)
            panel = createTemporaryObject(panelComponent, scene)
            verify(panel !== null)
        }
        function cleanup() {
            panel.destroy()
            wait(0)
            controller.destroy()
            wait(0)
        }
        function screenshot(name) {
            const file = testFixtures.screenshotPath(name)
            if (file) { waitForRendering(panel); grabImage(scene).save(file) }
        }
        function seed() {
            controller.tracks = [
                {id: "1", title: "使一颗心免于哀伤 · A long song title for narrow windows", artist: "知更鸟 / HOYO-MiX / Chevy", album: "崩坏星穹铁道 · INSIDE", cover_url: "qrc:/artwork/default-cover.png"},
                {id: "2", title: "<b>Plain song title</b>", artist: "Artist", album: "Album", cover_url: ""}
            ]
            const list = findChild(panel, "neteaseResults")
            tryCompare(list, "count", 2)
            tryVerify(() => list.itemAtIndex(1) !== null)
            return list
        }
        function test_paginationUsesSubmittedQueryAndDropsLateResults() {
            const input = findChild(panel, "neteaseQuery")
            input.text = "Night"
            mouseClick(findChild(panel, "neteaseSearch"))
            verify(controller.busy)
            fakeApi.resolveSearch({tracks: [{id: "1", title: "Night"}], cursor: "2"})
            tryCompare(controller, "busy", false)
            input.text = "Unsubmitted edit"
            mouseClick(findChild(panel, "neteaseNext"))
            compare(fakeApi.calls[fakeApi.calls.length - 1].params.query, "Night")
            compare(fakeApi.calls[fakeApi.calls.length - 1].params.cursor, "2")
            controller.acceptStatus({bitrate: "128", word_lyrics: true, translation: true})
            fakeApi.resolveSearch({tracks: [{id: "9", title: "Late response"}], cursor: "3"})
            wait(0)
            compare(controller.tracks.length, 0)
            verify(findChild(panel, "neteaseSearch").enabled)
            verify(!controller.busy)
        }
        function test_queueAndDownloadActionsDoNotSelectOrAutoplay() {
            const list = seed()
            const row = list.itemAtIndex(0)
            mouseClick(findChild(row, "neteaseEnqueue"))
            tryCompare(controller, "actionBusy", false)
            compare(list.currentIndex, -1)
            compare(fakeApi.calls[fakeApi.calls.length - 1].method, "music.enqueue")
            compare(fakeApi.calls[fakeApi.calls.length - 1].params.play, false)
            mouseClick(findChild(row, "neteaseDownload"))
            tryCompare(controller, "downloadTask", "download-task")
            compare(list.currentIndex, -1)
            verify(!findChild(row, "neteaseDownload").enabled)
            controller.downloadEvent({task: "other-task", state: "finished"})
            compare(controller.downloadTask, "download-task")
            controller.downloadEvent({task: "download-task", state: "finished"})
            compare(controller.message, "downloaded")
            mouseClick(row, 90, 30)
            compare(list.currentIndex, 0)
            keyClick(Qt.Key_Return)
            tryCompare(controller, "actionBusy", false)
            compare(fakeApi.calls[fakeApi.calls.length - 1].params.play, true)
        }
        function test_narrowLayoutCoverFallbackAndLanguages() {
            const list = seed()
            tryCompare(findChild(list.itemAtIndex(0), "neteaseCover"), "status", Image.Ready)
            compare(findChild(list.itemAtIndex(1), "neteaseCover").visible, false)
            compare(findChild(list.itemAtIndex(1), "neteaseTrackTitle").textFormat, Text.PlainText)
            screenshot("netease-music-zh")
            scene.width = 600; pluginTranslator.language = "en"
            waitForRendering(panel)
            const row = list.itemAtIndex(0), button = findChild(row, "neteaseDownload")
            verify(button.mapToItem(row, button.width, 0).x <= row.width)
            verify(findChild(row, "neteaseTrackTitle").width > 0)
            verify(findChild(row, "neteaseTrackTitle").truncated)
            screenshot("netease-music-narrow-en")
        }
        function test_settingsKeepDraftWithoutServiceConfiguration() {
            panel.destroy()
            panel = createTemporaryObject(settingsComponent, scene)
            verify(panel !== null)
            const save = findChild(panel, "neteaseSave")
            verify(findChild(panel, "neteaseServiceUrl") === null)
            verify(findChild(panel, "neteaseEnabled") === null)
            compare(panel.selectedBitrate, "320")
            verify(!save.enabled)
            mouseClick(findChild(panel, "neteaseBitrate128"))
            mouseClick(findChild(panel, "neteaseWordLyrics"))
            fakeApi.failSave = true
            mouseClick(save)
            tryCompare(panel, "saving", false)
            compare(panel.selectedBitrate, "128")
            compare(panel.wordLyrics, false)
            compare(panel.message, "netease_invalid_config")
            fakeApi.failSave = false
            mouseClick(save)
            tryCompare(panel, "message", "saved")
            compare(controller.status.bitrate, "128")
            compare(controller.status.word_lyrics, false)
            verify(!save.enabled)
            screenshot("netease-settings-zh")
            pluginTranslator.language = "en"
            screenshot("netease-settings-en")
        }
    }
}
