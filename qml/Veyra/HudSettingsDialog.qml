// 帧率 HUD 设置：显示开关、四角位置、大小、逐行内容。改动立即生效。
import QtQuick
import QtQuick.Layouts

DialogLayer {
    id: hudDialog
    objectName: "hud-dialog"
    key: "hud"
    glyph: "gauge"
    title: qsTr("帧率 HUD")
    sub: qsTr("位置 / 大小 / 显示内容 · 改动立即生效")
    dialogWidth: 640
    actions: [{ label: qsTr("完成"), primary: true }]
    onActionTriggered: label => dialogHost.close()

    readonly property int corner: Number(veyra.preferences.hudCorner !== undefined ? veyra.preferences.hudCorner : 0)
    readonly property int hudItems: Number(veyra.preferences.hudItems !== undefined ? veyra.preferences.hudItems : 127)
    readonly property int lastCorner: corner >= 1 && corner <= 6 ? corner : 2
    readonly property int offsetY: Number(veyra.preferences.hudOffsetY !== undefined ? veyra.preferences.hudOffsetY : 96)
    readonly property int scalePercent: Number(veyra.preferences.hudScale !== undefined ? veyra.preferences.hudScale : 100)
    readonly property var cornerOptions: [
        { id: "1", label: qsTr("左上") },
        { id: "5", label: qsTr("左上 · 置顶") },
        { id: "2", label: qsTr("右上") },
        { id: "6", label: qsTr("右上 · 置顶") },
        { id: "3", label: qsTr("左下") },
        { id: "4", label: qsTr("右下") }
    ]
    function cornerLabel(c) {
        for (const o of cornerOptions) if (Number(o.id) === c) return o.label
        return qsTr("右上")
    }
    function setItem(bit, on) {
        const next = on ? (hudItems | bit) : (hudItems & ~bit)
        veyra.setPreference("hudItems", next)
    }

    DialogSection { text: qsTr("显示") }
    DialogGroup {
        VRow {
            label: qsTr("帧率与延迟 HUD")
            hint: qsTr("边玩边看帧率与各段延迟；串流到 1080p 屏幕觉得小，把下面的「大小」调到 150% 或 200%")
            VSwitch {
                objectName: "hud-show"
                checked: hudDialog.corner > 0
                onToggled: v => veyra.setPreference("hudCorner", v ? hudDialog.lastCorner : 0)
            }
        }
        VRow {
            label: qsTr("位置")
            hint: qsTr("「置顶」贴住画面上边缘；左上/右上按下面的下移量留出空间")
            VSelect {
                objectName: "hud-corner"
                implicitWidth: 170
                value: hudDialog.cornerLabel(hudDialog.lastCorner)
                options: hudDialog.cornerOptions
                onPicked: id => veyra.setPreference("hudCorner", Number(id))
            }
        }
        VRow {
            label: qsTr("上边距 / 下移量")
            hint: qsTr("仅「左上」「右上」生效；0 px = 贴住画面上边缘，「置顶」两项也贴边")
            VSelect {
                objectName: "hud-offset"
                implicitWidth: 110
                value: hudDialog.offsetY + " px"
                options: [0, 24, 48, 72, 96, 120, 160, 200].map(v => ({ id: String(v), label: v + " px" }))
                onPicked: id => veyra.setPreference("hudOffsetY", Number(id))
            }
        }
        VRow {
            label: qsTr("大小")
            hint: qsTr("按比例缩放字号与内边距，只影响叠加层")
            VSelect {
                objectName: "hud-scale"
                implicitWidth: 120
                value: hudDialog.scalePercent + "%"
                options: [
                    { id: "100", label: "100%" },
                    { id: "125", label: "125%" },
                    { id: "150", label: "150%" },
                    { id: "175", label: "175%" },
                    { id: "200", label: "200%" }
                ]
                onPicked: id => veyra.setPreference("hudScale", Number(id))
            }
        }
    }

    DialogSection { text: qsTr("显示内容") }
    DialogGroup {
        VRow {
            label: qsTr("帧率")
            VSwitch { objectName: "hud-item-fps"; checked: (hudDialog.hudItems & 1) !== 0; onToggled: v => hudDialog.setItem(1, v) }
        }
        VRow {
            label: qsTr("1% low")
            VSwitch { objectName: "hud-item-low"; checked: (hudDialog.hudItems & 2) !== 0; onToggled: v => hudDialog.setItem(2, v) }
        }
        VRow {
            label: qsTr("采集延迟")
            VSwitch { objectName: "hud-item-capture"; checked: (hudDialog.hudItems & 4) !== 0; onToggled: v => hudDialog.setItem(4, v) }
        }
        VRow {
            label: qsTr("引擎延迟")
            VSwitch { objectName: "hud-item-engine"; checked: (hudDialog.hudItems & 8) !== 0; onToggled: v => hudDialog.setItem(8, v) }
        }
        VRow {
            label: qsTr("上屏等待")
            VSwitch { objectName: "hud-item-scanout"; checked: (hudDialog.hudItems & 16) !== 0; onToggled: v => hudDialog.setItem(16, v) }
        }
        VRow {
            label: qsTr("上屏总延迟")
            VSwitch { objectName: "hud-item-total"; checked: (hudDialog.hudItems & 32) !== 0; onToggled: v => hudDialog.setItem(32, v) }
        }
        VRow {
            label: qsTr("处理耗时")
            VSwitch { objectName: "hud-item-chain"; checked: (hudDialog.hudItems & 64) !== 0; onToggled: v => hudDialog.setItem(64, v) }
        }
    }
    DialogNote { text: qsTr("全部内容关掉时 HUD 自动隐藏。想做「只显示帧率」，把除「帧率」以外的开关都关掉即可。") }
}
