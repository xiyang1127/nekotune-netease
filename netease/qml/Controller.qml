import QtQuick
import NekoTune 1.0
QtObject {
    id: root
    property var status: ({bitrate: "320", word_lyrics: true, translation: true})
    property bool ready: false
    property bool busy: false
    property bool actionBusy: false
    property bool startingDownload: false
    property var tracks: []
    property var earlyDownloads: []
    property string downloadTask: ""
    property string downloadMessage: ""
    property string message: ""
    property bool failed: false
    property int generation: 0
    property string submittedQuery: ""
    property string submittedMode: "search"
    property string cursor: ""
    property int page: 1
    property bool searched: false
    property var api: ExtensionApi {
        onEvent: function(event) {
            if (event.event === "extension.nekotune.netease.status") root.acceptStatus(event.data)
            if (event.event === "music.download") {
                if (root.startingDownload) root.earlyDownloads = root.earlyDownloads.concat([event])
                else root.downloadEvent(event)
            }
        }
    }
    function errorKey(error) {
        const match = String(error).match(/netease_[a-z_]+/)
        return match ? match[0] : "failed"
    }
    function acceptStatus(value) {
        if (JSON.stringify(status) !== JSON.stringify(value)) {
            ++generation
            busy = false
            tracks = []; cursor = ""; searched = false
        }
        status = value
        ready = true
    }
    function refresh() {
        return api.invoke("status").then(value => root.acceptStatus(value)).catch(error => {
            root.message = root.errorKey(error); root.failed = true
        })
    }
    function browse(mode, query, pageNumber) {
        if (!ready || busy) return
        const revision = ++generation
        busy = true; message = ""; failed = false
        const params = mode === "search" ? {query: query, cursor: String(pageNumber)} : {id: query}
        api.invoke(mode, params).then(result => {
            if (revision !== root.generation) return
            root.tracks = result.tracks || []
            root.cursor = result.cursor || ""
            root.submittedQuery = query; root.submittedMode = mode
            root.page = pageNumber; root.searched = true; root.busy = false
        }).catch(error => {
            if (revision !== root.generation) return
            root.message = root.errorKey(error); root.failed = true; root.busy = false
        })
    }
    function action(track, kind) {
        if (!ready || actionBusy || (kind === "download" && downloadTask)) return
        actionBusy = true; message = ""; failed = false
        startingDownload = kind === "download"
        earlyDownloads = []
        const method = kind === "download" ? "music.download" : "music.enqueue"
        api.call(method, {source: api.ownerId + "/music", track: track, play: kind === "play"}).then(result => {
            root.actionBusy = false; root.startingDownload = false
            if (kind === "download") {
                root.downloadTask = result.task; root.downloadMessage = "downloading"
                for (const event of root.earlyDownloads) root.downloadEvent(event)
                root.earlyDownloads = []
            } else root.message = kind === "play" ? "playing" : "queued"
        }).catch(error => {
            root.actionBusy = false; root.startingDownload = false; root.earlyDownloads = []
            root.message = root.errorKey(error); root.failed = true
        })
    }
    function downloadEvent(event) {
        if (!downloadTask || event.task !== downloadTask) return
        if (event.state === "running") return
        downloadTask = ""; downloadMessage = ""
        failed = event.state !== "finished" && event.state !== "cancelled"
        message = event.state === "finished" ? "downloaded" : event.state === "cancelled" ? "cancelled" : errorKey(event.message)
    }
    function cancel() {
        if (downloadTask) api.call("music.cancel", {task: downloadTask}).catch(error => {
            root.message = root.errorKey(error); root.failed = true
        })
    }
    Component.onCompleted: refresh()
    Component.onDestruction: ++generation
}
