// 键鼠映射: GTA6 preset (default/driving), mouse feel and stick/trigger tuning.
import QtQuick
import QtQuick.Layouts

DialogLayer {
    id: kbmDialog
    objectName: "kbm-dialog"
    key: "kbm"
    glyph: "gamepad"
    title: qsTr("键鼠映射")
    sub: qsTr("GTA6 预设 · 手柄仍是主输入，键鼠叠加")
    dialogWidth: 720
    actions: [
        { label: qsTr("恢复默认") },
        { label: qsTr("完成"), primary: true }
    ]
    onActionTriggered: label => {
        if (label === qsTr("恢复默认")) {
            const defaults = { sensitivity: 1.8, curveType: 0, deadzone: 0.0, antiDeadzone: 0.15,
                               smoothing: 0.5, leftValue: 1.0, leftRamp: 4.0, r2Depth: 1.0, l2Depth: 1.0,
                               flick: true, invertY: false }
            for (const k in defaults) veyra.ps5KbmSet(k, defaults[k])
            return
        }
        dialogHost.close()
    }

    readonly property var kbm: veyra.ps5Kbm
    function set(key, value) { veyra.ps5KbmSet(key, value) }

    DialogSection { text: qsTr("开关") }
    DialogGroup {
        VRow {
            label: qsTr("启用键鼠映射")
            hint: qsTr("开启后鼠标被锁在画面内、光标隐藏；按 End 释放。只在 Veyra 前台且手柄控制已连接时生效")
            VSwitch {
                objectName: "kbm-enabled"
                checked: kbmDialog.kbm.enabled === true
                onToggled: v => kbmDialog.set("enabled", v)
            }
        }
        VRow {
            label: qsTr("当前模式")
            hint: qsTr("鼠标侧键 2 循环切换「默认 / 驾驶」；驾驶时 W/S 变成油门/刹车")
            Text {
                objectName: "kbm-mode"
                text: kbmDialog.kbm.mode === 1 ? qsTr("驾驶") : qsTr("默认")
                color: Theme.t1
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsBody
            }
        }
    }

    DialogSection { text: qsTr("鼠标手感") }
    DialogGroup {
        VRow {
            label: qsTr("灵敏度")
            hint: qsTr("鼠标速度达到基准时摇杆满偏的倍率；1.8 是 GTA6 预设值")
            VSlider {
                objectName: "kbm-sensitivity"
                valueFromModel: true
                implicitWidth: 150
                from: 0.5; to: 4.0; stepSize: 0.1; live: false
                value: kbmDialog.kbm.sensitivity ?? 1.8
                onMoved: v => kbmDialog.set("sensitivity", Math.round(v * 10) / 10)
            }
        }
        VRow {
            label: qsTr("响应曲线")
            hint: qsTr("线性最直接；二次/三次让慢速移动更精细")
            VSeg {
                objectName: "kbm-curve"
                options: [{ id: "0", label: qsTr("线性") }, { id: "1", label: qsTr("二次") }, { id: "2", label: qsTr("三次") }]
                current: String(kbmDialog.kbm.curveType ?? 0)
                onPicked: id => kbmDialog.set("curveType", Number(id))
            }
        }
        VRow {
            label: qsTr("死区")
            hint: qsTr("小于该值的摇杆输出归零，抵消手抖")
            VSlider {
                objectName: "kbm-deadzone"
                valueFromModel: true
                implicitWidth: 150
                from: 0.0; to: 0.2; stepSize: 0.005; live: false
                value: kbmDialog.kbm.deadzone ?? 0.0
                onMoved: v => kbmDialog.set("deadzone", Math.round(v * 200) / 200)
            }
        }
        VRow {
            label: qsTr("反死区")
            hint: qsTr("把非零输出抬到游戏死区之上，解决「小幅移动拖不动」")
            VSlider {
                objectName: "kbm-anti-deadzone"
                valueFromModel: true
                implicitWidth: 150
                from: 0.0; to: 0.4; stepSize: 0.01; live: false
                value: kbmDialog.kbm.antiDeadzone ?? 0.15
                onMoved: v => kbmDialog.set("antiDeadzone", Math.round(v * 100) / 100)
            }
        }
        VRow {
            label: qsTr("平滑")
            hint: qsTr("越大越稳但越滞后；0.5 是预设值")
            VSlider {
                objectName: "kbm-smoothing"
                valueFromModel: true
                implicitWidth: 150
                from: 0.0; to: 0.9; stepSize: 0.05; live: false
                value: kbmDialog.kbm.smoothing ?? 0.5
                onMoved: v => kbmDialog.set("smoothing", Math.round(v * 20) / 20)
            }
        }
        VRow {
            label: qsTr("Flick Stick")
            hint: qsTr("水平快速甩动时摇杆瞬间满偏一小段，模拟瞬间转身")
            VSwitch {
                objectName: "kbm-flick"
                checked: kbmDialog.kbm.flick !== false
                onToggled: v => kbmDialog.set("flick", v)
            }
        }
        VRow {
            label: qsTr("Y 轴反转")
            VSwitch {
                objectName: "kbm-invert-y"
                checked: kbmDialog.kbm.invertY === true
                onToggled: v => kbmDialog.set("invertY", v)
            }
        }
    }

    DialogSection { text: qsTr("摇杆与扳机") }
    DialogGroup {
        VRow {
            label: qsTr("左摇杆幅度")
            hint: qsTr("WASD 推到底时的摇杆幅度；左 Alt 可在 0.3 / 1.0 之间循环")
            VSlider {
                objectName: "kbm-left-value"
                valueFromModel: true
                implicitWidth: 150
                from: 0.2; to: 1.0; stepSize: 0.05; live: false
                value: kbmDialog.kbm.leftValue ?? 1.0
                onMoved: v => kbmDialog.set("leftValue", Math.round(v * 20) / 20)
            }
        }
        VRow {
            label: qsTr("左摇杆爬升")
            hint: qsTr("每秒推到目标的速度；0 为瞬时到位，4 为预设")
            VSlider {
                objectName: "kbm-left-ramp"
                valueFromModel: true
                implicitWidth: 150
                from: 0.0; to: 12.0; stepSize: 0.5; live: false
                value: kbmDialog.kbm.leftRamp ?? 4.0
                onMoved: v => kbmDialog.set("leftRamp", Math.round(v * 2) / 2)
            }
        }
        VRow {
            label: qsTr("R2 输出深度")
            hint: qsTr("键盘/鼠标映射到 R2 时按下的行程；驾驶油门可调浅")
            VSlider {
                objectName: "kbm-r2-depth"
                valueFromModel: true
                implicitWidth: 150
                from: 0.1; to: 1.0; stepSize: 0.05; live: false
                value: kbmDialog.kbm.r2Depth ?? 1.0
                onMoved: v => kbmDialog.set("r2Depth", Math.round(v * 20) / 20)
            }
        }
        VRow {
            label: qsTr("L2 输出深度")
            VSlider {
                objectName: "kbm-l2-depth"
                valueFromModel: true
                implicitWidth: 150
                from: 0.1; to: 1.0; stepSize: 0.05; live: false
                value: kbmDialog.kbm.l2Depth ?? 1.0
                onMoved: v => kbmDialog.set("l2Depth", Math.round(v * 20) / 20)
            }
        }
    }

    DialogNote {
        text: qsTr("默认模式：WASD=左摇杆，空格=✕，R=□，F=△，C=○，Q=R1，Tab=L1，Shift=L3，B=←，Ctrl=→，M=触控板，方向键=十字键；鼠标左键=R2、右键=L2、中键=R3、侧键1=↑、侧键2=切模式。\n驾驶模式：W/S=R2/L2，鼠标左键=R1。手柄本身的按键、摇杆、扳机始终叠加，不受影响。")
    }
}
