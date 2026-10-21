import QtQuick
Item {
    objectName: "neteasePluginSettings"
    Controller { id: controller }
    Translator { id: pluginTranslator }
    SettingsPanel { anchors.fill: parent; client: controller; translator: pluginTranslator }
}
