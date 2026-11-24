import QtQuick
import QtQuick.Window

// 区域录制: a dim window over the whole monitor to drag the recording
// rectangle on. It reports monitor-relative physical pixels, the unit the
// capture feeder crops in.
Window {
    id: root
    signal selected(int x, int y, int w, int h)
    signal cancelled()

    flags: Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.Tool
    color: Qt.rgba(0, 0, 0, 0.45)
    modality: Qt.NonModal
    title: qsTr("拖动鼠标选择录制区域（Esc 取消）")
    visible: false

    // Window.screen is a QQuickScreenInfo: it has width/height/devicePixelRatio
    // but no geometry, so its size is what this window takes (it opens on the
    // screen the caller is on) and what the logical-to-physical ratio comes from.
    property var targetScreen: null
    readonly property real dpr: targetScreen && targetScreen.devicePixelRatio > 0
                                ? targetScreen.devicePixelRatio : 1

    property real startX: 0
    property real startY: 0
    property real endX: 0
    property real endY: 0
    readonly property rect picked: Qt.rect(Math.min(startX, endX), Math.min(startY, endY),
                                           Math.abs(endX - startX), Math.abs(endY - startY))

    function begin(target) {
        targetScreen = target
        startX = startY = endX = endY = 0
        if (target && target.width > 0 && target.height > 0) {
            width = target.width
            height = target.height
        }
        // Full screen instead of a placed window: QQuickScreenInfo has no
        // geometry to position by, and fullscreen is exactly the monitor.
        // Order matters: show() maps the window back to Windowed, so the
        // fullscreen state goes on afterwards.
        show()
        visibility = Window.FullScreen
        requestActivate()
    }

    Rectangle {
        id: frame
        x: root.picked.x
        y: root.picked.y
        width: root.picked.width
        height: root.picked.height
        color: "transparent"
        border.width: 2
        border.color: Theme.accent
        visible: width > 0 && height > 0
    }

    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.CrossCursor
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        onPressed: (mouse) => {
            if (mouse.button === Qt.RightButton) { root.cancelled(); root.hide(); return }
            root.startX = mouse.x; root.startY = mouse.y
            root.endX = mouse.x; root.endY = mouse.y
        }
        onPositionChanged: (mouse) => {
            if (!pressed) return
            root.endX = Math.max(0, Math.min(width, mouse.x))
            root.endY = Math.max(0, Math.min(height, mouse.y))
        }
        onReleased: {
            const r = root.picked
            let px = Math.round(r.x * root.dpr), py = Math.round(r.y * root.dpr)
            let pw = Math.round(r.width * root.dpr), ph = Math.round(r.height * root.dpr)
            // Even extents: the encoder needs them and the crop must stay inside.
            pw = pw & ~1; ph = ph & ~1
            if (pw < 32 || ph < 32) { root.cancelled(); root.hide(); return }
            root.hide()
            root.selected(px, py, pw, ph)
        }
    }

    Shortcut {
        sequence: "Escape"
        onActivated: { root.cancelled(); root.hide() }
    }
}
