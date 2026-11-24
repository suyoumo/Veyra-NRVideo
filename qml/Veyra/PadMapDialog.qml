// 按键映射: which Xbox button feeds each PS5 button, the touch-gesture keys and the
// controller identity the console is told is connected. Changes apply immediately.
import QtQuick
import QtQuick.Layouts

DialogLayer {
    id: padDialog
    objectName: "padmap-dialog"
    key: "padmap"
    glyph: "gamepad"
    title: qsTr("按键映射")
    sub: qsTr("Xbox 手柄 → PS5 · 改动立即生效")
    dialogWidth: 760
    actions: [
        { label: qsTr("恢复默认") },
        { label: qsTr("完成"), primary: true }
    ]
    onActionTriggered: label => {
        if (label === qsTr("恢复默认")) { veyra.ps5PadReset(); return }
        dialogHost.close()
    }

    readonly property var pad: veyra.ps5Pad
    // PS5 logical buttons, in ControllerState bit order.
    readonly property var bindings: [
        { key: "cross", label: qsTr("✕ Cross") },
        { key: "circle", label: qsTr("○ Circle") },
        { key: "square", label: qsTr("□ Square") },
        { key: "triangle", label: qsTr("△ Triangle") },
        { key: "dpad_up", label: qsTr("十字键 ↑") },
        { key: "dpad_down", label: qsTr("十字键 ↓") },
        { key: "dpad_left", label: qsTr("十字键 ←") },
        { key: "dpad_right", label: qsTr("十字键 →") },
        { key: "l1", label: "L1" },
        { key: "r1", label: "R1" },
        { key: "l3", label: "L3" },
        { key: "r3", label: "R3" },
        { key: "options", label: qsTr("Options") },
        { key: "share", label: qsTr("Share") },
        { key: "touchpad", label: qsTr("触控板点击") },
        { key: "ps", label: qsTr("PS 键") }
    ]
    readonly property var sourceOptions: [
        { id: "south", label: qsTr("A / 南") },
        { id: "east", label: qsTr("B / 东") },
        { id: "west", label: qsTr("X / 西") },
        { id: "north", label: qsTr("Y / 北") },
        { id: "leftshoulder", label: qsTr("LB") },
        { id: "rightshoulder", label: qsTr("RB") },
        { id: "leftstick", label: qsTr("左摇杆按下") },
        { id: "rightstick", label: qsTr("右摇杆按下") },
        { id: "back", label: qsTr("Back / Share 键") },
        { id: "start", label: qsTr("Start / Options 键") },
        { id: "guide", label: qsTr("Xbox 键") },
        { id: "touchpad", label: qsTr("触控板键") },
        { id: "left_paddle1", label: qsTr("左背键 1") },
        { id: "left_paddle2", label: qsTr("左背键 2") },
        { id: "right_paddle1", label: qsTr("右背键 1") },
        { id: "right_paddle2", label: qsTr("右背键 2") },
        { id: "dpad_up", label: qsTr("十字键 ↑") },
        { id: "dpad_down", label: qsTr("十字键 ↓") },
        { id: "dpad_left", label: qsTr("十字键 ←") },
        { id: "dpad_right", label: qsTr("十字键 →") },
        { id: "none", label: qsTr("不映射") }
    ]
    // For the gesture rows the choice is a PS5 button, not a physical one.
    readonly property var ps5Options: [
        { id: "cross", label: "✕ Cross" },
        { id: "circle", label: "○ Circle" },
        { id: "square", label: "□ Square" },
        { id: "triangle", label: "△ Triangle" },
        { id: "l1", label: "L1" },
        { id: "r1", label: "R1" },
        { id: "l3", label: "L3" },
        { id: "r3", label: "R3" },
        { id: "options", label: qsTr("Options") },
        { id: "share", label: qsTr("Share") },
        { id: "touchpad", label: qsTr("触控板点击") },
        { id: "ps", label: qsTr("PS 键") }
    ]
    function sourceLabel(id) {
        for (const o of sourceOptions) if (o.id === id) return o.label
        return qsTr("不映射")
    }
    function ps5Label(id) {
        for (const o of ps5Options) if (o.id === id) return o.label
        return id || ""
    }
    function mapValue(action) {
        const m = padDialog.pad.map
        return m && m[action] ? m[action] : ""
    }
    function gestureValue(key, fallback) {
        return (padDialog.pad && padDialog.pad[key]) || fallback
    }

    DialogSection { text: qsTr("PS5 按键 ← Xbox 手柄") }
    DialogGroup {
        Repeater {
            model: padDialog.bindings
            delegate: VRow {
                required property var modelData
                label: modelData.label
                VSelect {
                    implicitWidth: 200
                    value: padDialog.sourceLabel(padDialog.mapValue(modelData.key))
                    options: padDialog.sourceOptions
                    onPicked: id => veyra.ps5PadSet(modelData.key, id)
                }
            }
        }
    }

    DialogSection { text: qsTr("触摸板模拟（Xbox 手柄）") }
    DialogGroup {
        VRow {
            label: qsTr("触摸键（键盘）")
            hint: qsTr("推荐：掌机把背键在 Armoury Crate 里映射成键盘按键，点右侧后按一下背键。键盘经 Moonlight 转发，不占手柄按键，GTA6 的 L3 不会冲突；只在 Veyra 窗口有焦点时生效")
            hintBelow: true
            RowLayout {
                spacing: 6
                Text {
                    objectName: "padmap-touch-key"
                    text: padDialog.pad.touchKeyName || qsTr("未设置")
                    color: Theme.t1
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.fsBody
                    elide: Text.ElideRight
                    Layout.maximumWidth: 150
                }
                VButton {
                    objectName: "padmap-touch-key-capture"
                    text: padDialog.pad.touchKeyCapturing ? qsTr("请按键…") : qsTr("按下按键…")
                    ghost: true
                    onClicked: veyra.ps5PadSet("touchKey", "capture")
                }
                VButton {
                    text: qsTr("清除")
                    ghost: true
                    visible: (padDialog.pad.touchKeyName || "").length > 0
                    onClicked: veyra.ps5PadSet("touchKey", "")
                }
            }
        }
        VRow {
            label: qsTr("进入触摸模式的键")
            hint: qsTr("按住它时摇杆变成触摸点。掌机上先把背键映射成一个标准按键（如 LS 按下），再在这里选它")
            VSelect {
                implicitWidth: 170
                value: padDialog.ps5Label(padDialog.gestureValue("touchModifier", "share"))
                options: padDialog.ps5Options
                onPicked: id => veyra.ps5PadSet("touchModifier", id)
            }
        }
        VRow {
            label: qsTr("轻按＝点击")
            hint: qsTr("开启后：按住触摸键、不推摇杆直接松开＝触控板点击。绑到背键时，一个键就能同时完成点击和滑动")
            VSwitch {
                checked: padDialog.pad.tapClick !== false
                onToggled: v => veyra.ps5PadSet("touchTapClick", v ? "true" : "false")
            }
        }
        VRow {
            label: qsTr("触摸点击键")
            hint: qsTr("在触摸模式下按下＝触控板点击（默认 Options）")
            VSelect {
                implicitWidth: 170
                value: padDialog.ps5Label(padDialog.gestureValue("touchClick", "options"))
                options: padDialog.ps5Options
                onPicked: id => veyra.ps5PadSet("touchClick", id)
            }
        }
        VRow {
            label: qsTr("PS 组合键")
            hint: qsTr("在触摸模式下按它＝PS 键（默认 △，避开会唤醒 Game Bar 的 Xbox 键）")
            VSelect {
                implicitWidth: 170
                value: padDialog.ps5Label(padDialog.gestureValue("touchPS", "triangle"))
                options: padDialog.ps5Options
                onPicked: id => veyra.ps5PadSet("touchPS", id)
            }
        }
        VRow {
            label: qsTr("滑动用摇杆")
            hint: qsTr("触摸模式下哪根摇杆控制触摸点；这根摇杆不会传给游戏")
            VSeg {
                options: [{ id: "right", label: qsTr("右摇杆") }, { id: "left", label: qsTr("左摇杆") }]
                current: padDialog.gestureValue("touchStick", "right")
                onPicked: id => veyra.ps5PadSet("touchStick", id)
            }
        }
    }

    DialogSection { text: qsTr("PS5 识别到的控制器") }
    DialogGroup {
        VRow {
            label: qsTr("控制器类型")
            hint: qsTr("远程游玩协议支持 DualShock 4 和 DualSense；Xbox 手柄只作为本机输入。修改后下次连接生效")
            hintBelow: true
            VSeg {
                options: [{ id: "auto", label: qsTr("自动") }, { id: "dualsense", label: "DualSense" }, { id: "ds4", label: qsTr("DualShock 4") }]
                current: padDialog.gestureValue("controllerType", "auto")
                onPicked: id => veyra.ps5PadSet("controllerType", id)
            }
        }
    }
    DialogNote { text: qsTr("默认映射：A→✕、B→○、X→□、Y→△、LB/RB→L1/R1、Back→Share、Start→Options、Xbox 键→PS。接真 DualSense 时触控板、陀螺仪、震动走原生通道，不受这里影响。") }
}
