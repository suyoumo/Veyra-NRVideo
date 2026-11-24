// The application shell, rebuilt from the approved design.
//
// Structure comes from the prototype (index.html + app.css + board.js):
//   * a frameless window with the small 8px radius;
//   * a hidden top dock that drops in when the pointer reaches the top edge;
//   * one page visible at a time: home / min / pro / node / exp / set;
//   * in cinema mode the WINDOW ITSELF snaps to the film's aspect ratio so the
//     picture has no letterbox bars. The prototype's own technical note says the
//     window must resize to the film when a file opens (capture cards and games
//     are 16:9). The separate control window straddles its lower edge.
//
// The video is not in this scene graph. It stays the native D3D12 child window
// that apps/veyra-qml/main.cpp places over the item named "videoHost"; Qt hosts
// it and never composites it, which is what preserved present cost in S3.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "ExportLayout.js" as ExportLayout

Window {
    id: root
    width: 1280
    height: 800
    // 视频直出: the native video window is a separate top-level window placed over
    // this one instead of a child. A child window can never reach the scanout
    // directly, so DWM composes every frame (~10 ms measured on a 4K60 capture);
    // an unowned top-level flip window costs ~0.4 ms. Cost of the switch: QML
    // content that used to be drawn over the picture (HUD, cinema controls) ends
    // up behind it, because nothing may cover a flipping surface.
    readonly property bool directPresent: veyra.directPresent
    // 极简 follows the film, and a portrait film needs a narrow window (the dock is
    // about 330px wide).
    minimumWidth: cinema ? 360 : 720
    minimumHeight: 260
    visible: !test.portableUpdateProbe
    // The software scene graph cannot draw the Shape background. Keep the native
    // window opaque as well, including while its first frame is being produced.
    color: typeof vySoftwareUi !== "undefined" && vySoftwareUi ? (cinema ? Theme.videoBlack : Theme.bgOuter) : "transparent"
    // 游戏模式 click-through (ui/GamePassthrough.h): Qt's own flags, so Qt keeps the
    // window's layered/transparent/no-activate styles in both interface renderers.
    flags: Qt.Window | Qt.FramelessWindowHint
           | (veyra.passthroughActive ? Qt.WindowTransparentForInput : 0)
           | (veyra.gameInputActive ? Qt.WindowDoesNotAcceptFocus : 0)
    title: "Veyra " + veyra.version

    // Page ids match the prototype's PAGES keys so a design screenshot and an
    // app screenshot can be compared under the same name.
    property string page: "home"
    readonly property bool onboardingActive: onboarding.visible
    // The node page splitter: height of its video card (picture + 40px bar).
    property real nodeVideoHeight: 330
    // Every navigation goes through here: "专业" means the node page while the
    // node configuration is active, so the dock never drops into the list UI.
    function goPageTarget(p) { return p === "pro" && veyra.nodeMode === 1 ? "node" : p }
    function goPage(p) { page = goPageTarget(p) }
    // 设置 → 赞助作者, from the home page's coffee line.
    function showSponsor() {
        settingsPage.section = "sponsor"
        goPage("set")
    }
    // Test switches from the command line (main.cpp, G0.3); empty in normal use.
    readonly property var test: typeof vyTest !== "undefined" ? vyTest : ({})
    readonly property real testAspect: test.aspect !== undefined ? test.aspect : 0
    readonly property bool cinema: page === "min"
    // Maximized behaves like fullscreen for geometry: the screen owns the size,
    // so the cinema aspect rule must not fight the window manager.
    readonly property bool maximized: visibility === Window.Maximized
    // Use visibility directly: leaving fullscreen clears fullTarget before the
    // Maximized binding settles. The cinema animation must not resize a screen
    // sized window during that transition.
    readonly property bool screenSized: fullTarget || fullRestorePending || visibility === Window.FullScreen || visibility === Window.Maximized
    function toggleMaximized() {
        // The dock's restore button must also leave fullscreen, using the same
        // saved window state and cleanup as F11 / the transport button.
        if (fullscreen) { toggleFullscreen(); return }
        visibility = maximized ? Window.Windowed : Window.Maximized
        veyra.logUi("ui-window", "maximized=" + (visibility === Window.Maximized))
    }
    Binding { target: Theme; property: "reduced"; value: veyra.reducedMotion || root.test.reducedMotion === true }
    Binding { target: Theme; property: "accentName"; value: veyra.preferences.accent || "orange" }
    Binding { target: Theme; property: "paletteName"; value: veyra.preferences.palette || "dusk" }
    Binding { target: Theme; property: "backdropLevel"; value: veyra.preferences.backdrop !== undefined ? veyra.preferences.backdrop : 1 }

    // Cinema geometry: the main window ends at the picture. The independent pill
    // can extend below it without leaving an opaque strip in this window.
    // 16:9 until a source reports its own shape: a PS5 / capture session has no
    // aspect while it connects, and 2.39 then left a thin black strip.
    property real filmAspect: 16 / 9
    readonly property bool gameAligned: fullscreen && veyra.gameMode && !veyra.gameScaledPreview
    readonly property real reportedAspect: veyra.previewAspect
    onReportedAspectChanged: if (reportedAspect < 0.2) fitToFilm(16 / 9)
    property string lastPage: "home"
    property real heightBeforeCinema: 0
    // The tallest picture that still fits on the screen with the pill (46px) under
    // it. A portrait video or a captured portrait window used to make the window
    // taller than the screen (field report 2026-10-01).
    // The screen under the window's top centre (Screen.desktopAvailable* is the whole
    // virtual desktop, wrong on multi-monitor setups).
    readonly property rect screenArea: veyra.screenAvailableAt(root.x + root.width / 2, root.y + 20)
    readonly property real maxPictureHeight: Math.max(200, root.screenArea.height - 46 - 16)
    readonly property real pictureHeight: Math.min(Math.round(width / filmAspect), maxPictureHeight)
    // The width a tall film took away, given back when a wider film fits again.
    property real widthBeforeNarrow: 0

    function fitToFilm(aspect) {
        if (aspect > 0.2 && aspect < 5.0) filmAspect = aspect
        // The page, not `cinema`: onPageChanged calls this before that binding updates.
        if (page !== "min" || screenSized) return
        // Narrow the window for a film too tall for the screen, keeping its centre.
        let w = widthBeforeNarrow > 0 ? widthBeforeNarrow : width
        if (Math.round(w / filmAspect) > maxPictureHeight)
            w = Math.max(minimumWidth, Math.round(maxPictureHeight * filmAspect))
        if (Math.abs(w - width) >= 1) {
            if (widthBeforeNarrow <= 0) widthBeforeNarrow = width
            x += Math.round((width - w) / 2)
            width = w
        }
        if (widthBeforeNarrow > 0 && w >= widthBeforeNarrow) widthBeforeNarrow = 0
        height = Math.round(pictureHeight)
        keepPillOnScreen()
    }
    // The pill hangs 46px below the picture: lift the window if that leaves the screen.
    function keepPillOnScreen() {
        const a = root.screenArea
        const bottom = a.y + a.height
        if (y + pictureHeight + 46 + 8 > bottom) y = Math.max(a.y, bottom - pictureHeight - 46 - 8)
    }
    // .vy.cine transition: height .7s var(--spring-soft)
    Behavior on height {
        enabled: root.cinema && !root.screenSized
        NumberAnimation { duration: Theme.d(700); easing.bezierCurve: Theme.springSoft }
    }
    onWidthChanged: {
        if (cinema && !screenSized) height = Math.round(pictureHeight)
        videoHost.syncRect()
    }
    // The window height animates towards the film's aspect, so the picture area has
    // to follow every step of that animation. Without this the host kept its
    // previous height and the picture sat inside black bars - exactly what cinema
    // mode exists to avoid.
    onHeightChanged: videoHost.syncRect()
    onMaximizedChanged: {
        if (visibility === Window.Windowed && cinema && !fullTarget && !fullRestorePending) height = Math.round(pictureHeight)
        videoHost.syncRect()
    }
    onPageChanged: {
        if (onboarding.visible) onboarding.close()
        if (page === "node" && veyra.nodeMode !== 1) {
            veyra.nodeMode = 1
            if (veyra.nodeMode !== 1) { page = "pro"; return }
        }
        // Tell the bridge where the user is, so a command like "open a file" can
        // behave differently from home than from the professional page.
        if (veyra.currentPage !== page) veyra.currentPage = page
        // A page the user picks while the Home panel is up ends it there: closing
        // the panel later does not snap back to the page it was opened from.
        if (quickPanel && !instantPage) {
            quickReturnPage = ""
            if (page !== "pro" && page !== "node") quickPanel = false
        }
        // Test the page itself: the cinema binding may not have caught up with
        // this change yet, and a stale value shrank the page after min instead.
        // Leaving 极简 gives the window back the height it had before (core.js
        // clears the cinema height on any other page).
        const wasCinema = lastPage === "min"
        lastPage = page
        if (!screenSized && wasCinema && page !== "min" && widthBeforeNarrow > 0) {
            // A tall film narrowed the window; the other pages get their width back.
            x -= Math.round((widthBeforeNarrow - width) / 2)
            width = widthBeforeNarrow
            widthBeforeNarrow = 0
        }
        if (!screenSized && page !== "min" && width < 720) { x -= Math.round((720 - width) / 2); width = 720 }
        if (screenSized) { /* the screen is the window */ }
        else if (page === "min") { if (!wasCinema) heightBeforeCinema = height; fitToFilm(filmAspect) }
        else if (wasCinema && heightBeforeCinema >= 400) height = heightBeforeCinema
        else if (height < 600) height = 800
        // core.js app.go: the old page sinks (.22s) and the new one shows 150 ms
        // later with its [data-in] items rising. Reduced motion shows it at once.
        const prev = shownPage
        if (prev === page) return
        leavingPage = Theme.reduced || prev === "" || instantPage ? "" : prev
        if (prev === "home" || prev === "set") videoHost.syncRect()
        if (leavingPage !== "") { leaveAnim.restart(); showTimer.restart() }
        else showPage(false)
    }
    // The page on screen (entering or settled) and the one sinking out.
    property string shownPage: "home"
    readonly property var pageItems: [homePage, minPage, proPage, nodePage, exportPage, settingsPage]
    property string leavingPage: ""
    function showPage(animated) {
        shownPage = page
        if (animated)
            // By list, not pages.children: hidden pages are out of the scene (VPage.home).
            for (const p of root.pageItems) if (p.pageId === page) p.enter()
        // The video rect follows the page, and it must be settled before the
        // engine opens anything: it samples the window's client size once. It moves
        // here, once, not at the click: the native window cannot fade with the pages.
        videoHost.syncRect()
    }
    Timer { id: showTimer; interval: 150; onTriggered: root.showPage(true) }
    // @keyframes sink { to { opacity: 0; transform: scale(.985) } }, .22s --out.
    property real leaveT: 0
    NumberAnimation {
        id: leaveAnim
        target: root; property: "leaveT"; from: 0; to: 1
        duration: 220; easing.bezierCurve: Theme.easeOut
        onFinished: root.leavingPage = ""
    }

    // Background: pure black in cinema mode (the picture is the window), the
    // three-stop gradient otherwise - the prototype's .vy / .vy.cine split.
    VBackdrop {
        id: backdrop
        anchors.fill: parent
        visible: !root.cinema
        strokeColor: root.cinema ? "transparent" : Theme.stroke
    }

    // The one host item for the native video window; main.cpp finds it by name.
    // Exactly one must exist in the tree: an earlier version had a copy in each
    // page and the engine bound its swapchain to a hidden, zero-sized one.
    Item {
        id: videoHost
        objectName: "videoHost"
        visible: false          // geometry proxy; the native window draws pixels
        function syncRect() {
            const r = root.videoRect()
            x = r.x; y = r.y; width = r.width; height = r.height
        }
    }

    // Where the picture sits, per page, computed from the design's own layout
    // numbers rather than read from a laid-out child. A child reports 0x0 on the
    // frame the user opens a file, and that once produced a 1x1 swapchain.
    function videoRect() {
        if (fullscreen && veyra.gameMode &&
            veyra.gameVideoRect.width > 0 && veyra.gameVideoRect.height > 0)
            return veyra.gameVideoRect
        // Fullscreen is a video-only viewport (AppShell): the picture is the screen
        // and the controls float above it in their own window (FullscreenBar).
        if ((fullscreen || (maximized && page === "min")) && page !== "home" && page !== "set")
            return { x: 0, y: 0, width: width, height: height }
        switch (page) {
        case "min":
            // .min .stage: full width, height = the picture height.
            return { x: 0, y: 0, width: width, height: Math.round(pictureHeight) }
        case "pro":
            // Share the page's responsive header, transport and status dimensions.
            // A second fixed-height formula let the HWND cover the playback bar.
            return proPage.videoViewport
        case "node":
            return nodePage.videoViewport
        case "exp":
            return ExportLayout.calculate(width, height, exportPage.compactTab).video
        default:
            return { x: 0, y: 0, width: 0, height: 0 }
        }
    }

    // Pages live for the whole session so switching never loses a half-edited
    // chain; only the visible page owns the video window.
    Item {
        id: pages
        anchors.fill: parent
        // In fullscreen the picture covers the page, and the native video window
        // passes the pointer through: a hidden page control must not take a click
        // meant for the picture.
        visible: !root.fullscreen || root.page === "home" || root.page === "set" || root.quickPanelShown
        // Each page is shown while current or while sinking out; the sinking one
        // takes no input (.page.leave { pointer-events: none }).
        HomePage {
            id: homePage
            pageId: "home"
            home: pages
            onRequestPage: p => root.goPage(p)
            onRequestSponsor: root.showSponsor()
            onRequestOnboarding: if (root.page === "home" && dialogs.dialog === "") onboarding.start()
        }
        MinimalPage {
            id: minPage
            pageId: "min"
            dragStripHeight: 0
            home: pages
            onRequestPage: p => root.goPage(p)
            onRequestAspect: aspect => { if (root.testAspect <= 0) root.fitToFilm(aspect) }
            onRequestFullscreen: root.toggleFullscreen()
        }
        ProPage {
            id: proPage
            pageId: "pro"
            liveMode: root.liveMode
            onRequestLiveMode: root.toggleLiveMode()
            onRequestAudioTest: {
                root.audioTestPanel = !root.audioTestPanel
                if (root.audioTestPanel) veyra.startAudioTest()
                else veyra.stopAudioTest()
            }
            // Also follows wrapping status text and the compact header/transport.
            onVideoViewportChanged: if (root.page === "pro" && !root.fullscreen) videoHost.syncRect()
            home: pages
            onboardingActive: root.onboardingActive
            overlay: root.quickPanelShown && root.page === "pro"
            onRequestFullscreen: root.toggleFullscreen()
            onRequestPage: p => root.goPage(p)
            onRequestDialog: key => dialogs.open(key)
        }
        NodePage {
            id: nodePage
            pageId: "node"
            dragStripHeight: 14
            onVideoViewportChanged: if (root.page === "node" && !root.fullscreen) videoHost.syncRect()
            home: pages
            videoHeight: root.nodeVideoHeight
            overlay: root.quickPanelShown && root.page === "node"
            onVideoHeightEdited: h => { root.nodeVideoHeight = h; videoHost.syncRect() }
            onRequestFullscreen: root.toggleFullscreen()
            onRequestPage: p => root.goPage(p)
            onRequestDialog: key => dialogs.open(key)
            onRequestInspector: (index, type) => {
                proPage.selectedEffect = index
                veyra.selectedNrLayer = index
                veyra.selectedColourLayer = index
                proPage.tab = type === "color" ? "color" : "quality"
                root.page = "pro"
            }
        }
        ExportPage { id: exportPage; pageId: "exp"; home: pages; onRequestPage: p => root.goPage(p) }
        SettingsPage {
            id: settingsPage
            pageId: "set"
            home: pages
            onRequestPage: p => {
                if (p === "translation") dialogs.open(p)
                else root.goPage(p)
            }
        }
    }

    // The five dialogs sit above the pages and below the dock's own tooltips.
    DialogHost {
        id: dialogs
        anchors.fill: parent
        onboardingActive: root.onboardingActive
        // The dialogs open the source through the bridge; show the picture.
        onStartPs5: if (root.page === "home") root.goPage("min")
        onStartMoonlight: if (root.page === "home") root.goPage("min")
        onStartXbox: if (root.page === "home") root.goPage("min")
        onStartScreen: {
            if (root.page === "home") root.goPage("min")
            if (veyra.screenOptions.passthrough === true) root.enterGameFullscreen()
        }
        onStartScreenCrop: {
            root.goPage("min")
            screenCrop.begin(veyra.screenOptions)
        }
    }

    OnboardingGuide {
        id: onboarding
        parent: root.contentItem
        z: 1100
        property var savedProView
        property real savedCaptureScroll
        property real savedHomeScroll
        targetFor: key => {
            if (key === "capture") return { item: homePage.captureCard(), viewport: homePage.sourceViewport }
            if (dialogs.dialog === "capture") return dialogs.captureSettings.onboardingTarget(key)
            return proPage.onboardingTarget(key)
        }
        onAboutToShow: {
            savedProView = proPage.saveOnboardingView()
            savedCaptureScroll = dialogs.captureSettings.bodyViewport.contentY
            savedHomeScroll = homePage.sourceViewport.contentY
        }
        onStepRequested: step => {
            // Only the displayed page changes. The player, remembered page and
            // native video window stay in the same state as on the home page.
            if (step.tab) {
                dialogs.close()
                proPage.showOnboardingStep(step)
                root.shownPage = "pro"
            } else {
                root.shownPage = "home"
                dialogs.dialog = step.target === "capture" ? "" : "capture"
            }
        }
        onAboutToHide: {
            dialogs.close()
            dialogs.captureSettings.bodyViewport.contentY = savedCaptureScroll
            homePage.sourceViewport.contentY = savedHomeScroll
            root.shownPage = "home"
            proPage.restoreOnboardingView(savedProView)
        }
        onClosed: if (root.page === "home" && dialogs.dialog === "") homePage.onboardingEntry.forceActiveFocus()
    }

    // The dock floats above every page and retracts on its own.
    TopDock {
        id: dock
        anchors.horizontalCenter: parent.horizontalCenter
        // The node page is the professional mode's node view.
        currentPage: root.page === "node" ? "pro" : root.page
        maximized: root.maximized || root.fullscreen
        pinned: root.test.dockPinned === true || veyra.preferences.dockPinned === true
        locked: root.fullLocked
        forcedTip: root.test.tip !== undefined ? root.test.tip : ""
        opened: pinned
        // In fullscreen only the picture is on screen, so another page (专业 above all)
        // would change nothing visible: leave fullscreen first, as the pill's menu does.
        onRequestPage: p => {
            if (root.fullscreen && root.goPageTarget(p) !== root.page) root.toggleFullscreen()
            root.goPage(p)
        }
        onRequestMinimize: root.showMinimized()
        onRequestMaximize: root.toggleMaximized()
        onRequestClose: root.close()
        onRequestMove: if (!root.fullscreen) root.startSystemMove()
        pillToggleVisible: veyra.hasSource && (root.page === "min" || root.fullscreen)
        pillHidden: root.pillHidden
        onRequestTogglePill: root.setPillHidden(!root.pillHidden)
    }

    // Frameless window: the system resizes from 6px borders and 12px corners.
    Repeater {
        model: [
            { e: Qt.LeftEdge, c: Qt.SizeHorCursor }, { e: Qt.RightEdge, c: Qt.SizeHorCursor },
            { e: Qt.TopEdge, c: Qt.SizeVerCursor }, { e: Qt.BottomEdge, c: Qt.SizeVerCursor },
            { e: Qt.LeftEdge | Qt.TopEdge, c: Qt.SizeFDiagCursor }, { e: Qt.RightEdge | Qt.BottomEdge, c: Qt.SizeFDiagCursor },
            { e: Qt.RightEdge | Qt.TopEdge, c: Qt.SizeBDiagCursor }, { e: Qt.LeftEdge | Qt.BottomEdge, c: Qt.SizeBDiagCursor }
        ]
        delegate: MouseArea {
            required property var modelData
            readonly property bool l: (modelData.e & Qt.LeftEdge) !== 0
            readonly property bool r: (modelData.e & Qt.RightEdge) !== 0
            readonly property bool t: (modelData.e & Qt.TopEdge) !== 0
            readonly property bool b: (modelData.e & Qt.BottomEdge) !== 0
            readonly property bool corner: (l || r) && (t || b)
            objectName: "window-resize-" + modelData.e
            // Above the dialog layer (z 100): an open dialog must not stop the
            // window from being resized.
            z: 130
            enabled: !root.fullscreen && !root.maximized
            visible: enabled
            width: corner ? 12 : (l || r) ? 6 : root.width - 24
            height: corner ? 12 : (t || b) ? 6 : root.height - 24
            x: l ? 0 : r ? root.width - width : 12
            y: t ? 0 : b ? root.height - height : 12
            cursorShape: modelData.c
            onPressed: root.startSystemResize(modelData.e)
        }
    }

    // Drag and drop opens the file (AppShell WM_DROPFILES).
    DropArea {
        anchors.fill: parent
        onDropped: drop => {
            if (!drop.hasUrls || drop.urls.length === 0) return
            drop.accept(Qt.CopyAction)
            veyra.openDroppedUrls(drop.urls)
        }
    }

    // Keyboard, aligned with AppShell's message loop. Text fields keep their own
    // keys (Qt's shortcut override), so typing in a field never seeks. V (hold for
    // the original picture) needs its release as well; main.cpp handles it.
    readonly property bool fullscreen: visibility === Window.FullScreen
    // Set before the visibility changes: Windows resizes the window first, and the
    // cinema rule would otherwise snap that fullscreen size back to the film.
    property bool fullTarget: false
    // 直播模式: fullscreen with nothing drawn over the picture - no HUD, no
    // playback pill, no toasts - so a screen capture (OBS 显示器采集) gets a
    // clean, unchanging frame. Esc or F leaves it like any fullscreen.
    property bool liveMode: false
    function toggleLiveMode() { setLiveMode(!liveMode) }
    function setLiveMode(on) {
        if (on === liveMode) return
        liveMode = on
        veyra.logUi("live-mode", on ? "on: fullscreen clean picture" : "off")
        if (on) {
            if (typeof toast !== "undefined") toast.on = false
            if (!fullscreen) toggleFullscreen()
        }
    }
    property int fullRestoreVisibility: Window.Windowed
    // Windows may briefly report Windowed while restoring a maximized window.
    // Keep cinema sizing out of that native transition, without a polling timer.
    property bool fullRestorePending: false
    property bool fullLocked: false
    // Fullscreen quick-adjust panel (Home): the professional mode's own controls
    // over the picture - the list inspector (with the performance orbs) in list
    // mode, the node canvas along the bottom in node mode (veyra.nodeMode). From
    // 极简 the professional page becomes current underneath the fullscreen picture
    // while the panel is open, and 极简 returns when it closes.
    property bool quickPanel: false
    property string quickReturnPage: ""
    // A page change the user cannot see (under the fullscreen picture) skips the
    // sink animation, so the old page never flashes over the picture.
    property bool instantPage: false
    readonly property bool quickPanelShown: fullscreen && quickPanel && (page === "pro" || page === "node") && !fullLocked
    function switchPageInstantly(p) { instantPage = true; page = p; instantPage = false }
    function toggleQuickPanel() {
        if (quickPanel) { closeQuickPanel(); return }
        const target = veyra.nodeMode === 1 ? "node" : "pro"
        if (page !== target) {
            if (page !== "pro" && page !== "node") quickReturnPage = page
            switchPageInstantly(target)
        }
        quickPanel = true
    }
    function closeQuickPanel() {
        quickPanel = false
        if (quickReturnPage !== "") { const p = quickReturnPage; quickReturnPage = ""; switchPageInstantly(p) }
    }
    onQuickPanelShownChanged: {
        veyra.logUi("ui-fullscreen", "quick panel shown=" + quickPanelShown)
        veyra.setGamePanelOpen(quickPanelShown)
    }
    FullscreenTimingPanel {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 16
        width: Math.min(330, Math.max(180, parent.width - 424))
        maximumHeight: Math.max(120, root.height - (root.page === "node" ? Math.min(root.height * 0.6, 460) : 90))
        visible: root.quickPanelShown && veyra.hasSource
        onCloseRequested: root.closeQuickPanel()
        z: 75
    }
    // 游戏模式 (屏幕捕获 → 游戏模式): fullscreen on the captured monitor (or the
    // monitor of the captured window); the bridge then makes the picture click-through
    // so the program underneath keeps the keyboard, mouse and gamepad.
    function enterGameFullscreen() {
        const i = veyra.gameModeScreenIndex()
        const screens = Qt.application.screens
        if (i >= 0 && i < screens.length) {
            const s = screens[i]
            const inside = x + width / 2 >= s.virtualX && x + width / 2 < s.virtualX + s.width &&
                           y + height / 2 >= s.virtualY && y + height / 2 < s.virtualY + s.height
            if (!inside) {
                if (fullscreen) toggleFullscreen()
                x = s.virtualX + Math.max(0, Math.round((s.width - width) / 2))
                y = s.virtualY + Math.max(0, Math.round((s.height - height) / 2))
            }
        }
        if (!fullscreen) toggleFullscreen()
        veyra.logUi("game-mode", "fullscreen for game mode on screen " + i)
    }
    Connections {
        target: veyra
        // Home: the adjust panel takes the input back / gives it to the program again.
        // Ctrl+Alt+Home: leave the fullscreen picture (and with it the click-through).
        function onGameHotkey(which) {
            if (which === 1) {
                root.closeQuickPanel()
                if (root.fullscreen) root.toggleFullscreen()
                return
            }
            if (!root.fullscreen) return
            root.fullLocked = false
            root.toggleQuickPanel()
        }
        function onGameModeStopped(reason) {
            if (root.fullscreen) root.toggleFullscreen()
            toast.show(reason, true)
        }
        function onGameModeChanged() {
            videoHost.syncRect()
            if (veyra.gameInputActive && playlistWindow.visible) playlistWindow.hide()
        }
    }
    onFullscreenChanged: {
        veyra.setPresentationFullscreen(fullscreen)
        fullTarget = fullscreen
        if (!fullscreen) {
            fullLocked = false
            // Leaving the screen the stream captures ends 直播模式 with it.
            if (liveMode) liveMode = false
            closeQuickPanel()
            if (cinema && visibility === Window.Windowed && !fullRestorePending) height = Math.round(pictureHeight)
        }
        fullControls = true
        fullHide.restart()
        videoHost.syncRect()
    }
    function toggleFullscreen() {
        if (!fullscreen && veyra.fullscreenMemorySafe === false) {
            toast.show(qsTr("本次播放检测到全屏显存持续增长，请使用窗口播放；重新打开片源后可再次测试全屏。"), true)
            veyra.logUi("ui-fullscreen", "blocked after sustained video-memory growth")
            return
        }
        fullLocked = false
        if (!fullscreen) {
            fullRestoreVisibility = maximized ? Window.Maximized : Window.Windowed
            fullTarget = true
            visibility = Window.FullScreen
        } else {
            fullRestorePending = fullRestoreVisibility === Window.Maximized
            fullTarget = false
            visibility = fullRestoreVisibility
            Qt.callLater(function() { root.fullRestorePending = false })
        }
        veyra.logUi("ui-fullscreen", "enabled=" + fullscreen)
    }

    // G2.5 fullscreen controls. AppShell: any pointer movement shows the transport,
    // and 1.6 s without movement hides it (and the cursor) unless the pointer rests
    // on the bar or a menu is open; Ctrl+L locks it away. The bar is its own
    // top-level window (FullscreenBar.qml), because the native video window draws
    // above everything in this one.
    property bool fullControls: true
    // Also consumed by the native video HWND: a QML cursor alone cannot hide
    // the class cursor of the foreign child window under a stationary pointer.
    readonly property bool fullscreenCursorHidden: fullscreen && (!fullControls || fullLocked)
        && !quickPanelShown && dialogs.dialog === ""
    function pointerActivity() {
        if (!fullscreen || fullLocked || root.test.fullBar === "hidden") return
        if (root.test.fullDebug === true) veyra.logUi("ui-fullscreen-debug", "pointer activity")
        if (!fullControls) { fullControls = true; veyra.logUi("ui-fullscreen", "controls shown by pointer") }
        fullHide.restart()
    }
    Timer {
        id: fullHide
        interval: 1600
        onTriggered: {
            if (!root.fullscreen || !root.fullControls) return
            if (fullBar.hovered || fullBar.menuOpen || fullBar.interacting || root.test.fullBar === "shown") { restart(); return }
            root.fullControls = false
            veyra.logUi("ui-fullscreen", "controls hidden; video and subtitles only")
        }
    }
    // Every pointer move over this window (a HoverHandler sees them all, whatever
    // item is under the pointer; the video window passes them through). The cursor
    // hides with the controls, as in AppShell.
    HoverHandler {
        id: pointerWatch
        cursorShape: root.fullscreenCursorHidden ? Qt.BlankCursor : Qt.ArrowCursor
        // Qt re-sends hover while anything animates under a still pointer; only a
        // real change of position counts as movement.
        property point last: Qt.point(-1, -1)
        onPointChanged: {
            const p = point.scenePosition
            if (Math.abs(p.x - last.x) < 1 && Math.abs(p.y - last.y) < 1) return
            last = p
            root.pointerActivity()
            // Fullscreen: the top edge brings the dock down (极简 / 专业 switch in
            // place). Its own 12px hot zone sits under the native picture, where
            // hover is not always delivered, so the window-wide handler helps it.
            if (root.fullscreen && !root.fullLocked) {
                if (p.y <= 14) dock.open()
                else if (p.y > 90 && dock.opened && !dock.pinned) dock.opened = false
            }
        }
    }
    // 简要画面 HUD：帧率 / 1% low / 采集 / 引擎 / 上屏 / 总延迟 / 处理耗时。
    // 位置、大小与显示哪些行由「帧率 HUD」对话框设置：hudCorner 0 关 1 左上 2 右上
    // 3 左下 4 右下；hudScale 80–200%；hudItems 位掩码逐行开关。
    Rectangle {
        id: videoHud
        objectName: "video-hud"
        // 视频是原生子窗口，带 videoCover 的 QML 项会从它里面挖洞显示（main.cpp）。
        readonly property bool videoCover: true
        readonly property int corner: Number(veyra.preferences.hudCorner !== undefined ? veyra.preferences.hudCorner : 0)
        readonly property real hudScale: Math.max(0.8, Math.min(2.0,
            Number(veyra.preferences.hudScale !== undefined ? veyra.preferences.hudScale : 100) / 100))
        readonly property int items: Number(veyra.preferences.hudItems !== undefined ? veyra.preferences.hudItems : 127)
        readonly property bool rightSide: corner === 2 || corner === 4 || corner === 6
        readonly property bool bottomSide: corner === 3 || corner === 4
        // 置顶两项贴住画面上边缘，不再加下移量。
        readonly property bool topFlush: corner === 5 || corner === 6
        readonly property real coverRadius: 8 * hudScale
        // PresentMon 的 ETW 数字优先（VRR 下唯一可信）；没有它才用内部读数。
        readonly property real scanoutMs: veyra.presentMonKnown ? veyra.presentMonP50Ms : veyra.displayScanoutP50Ms
        readonly property real scanoutP95Ms: veyra.presentMonKnown ? veyra.presentMonP95Ms : veyra.displayScanoutP95Ms
        // 上屏那一段不可测时（还没有样本），总延迟按已知的两段给出下限。
        readonly property bool scanoutKnown: scanoutMs > 0
        readonly property real totalMs: (veyra.captureDriverAgeP50Ms > 0 && veyra.captureToPresentP50Ms > 0)
            ? veyra.captureDriverAgeP50Ms + veyra.captureToPresentP50Ms + (scanoutKnown ? scanoutMs : 0) : 0
        // 直播模式 keeps the HUD: it is the overlay the user wants in the
        // recording/stream; only the pill, toasts and the rest go away.
        // 视频直出: the present blit draws this same readout into the video buffer
        // (refreshHudOverlay, same settings), so this copy hides - two of them
        // would sit next to each other and only this one would follow the dialog.
        visible: corner > 0 && items !== 0 && veyra.hasSource && videoHost.width > 0 && !root.directPresent
        x: videoHost.x + (rightSide ? videoHost.width - width - 12 : 12)
        // 上边距完全由下移量决定：0 = 贴住画面上边缘（右上不再额外留 38px）。
        y: bottomSide ? videoHost.y + videoHost.height - height - 12
                      : topFlush ? videoHost.y
                      : videoHost.y + Number(veyra.preferences.hudOffsetY !== undefined ? veyra.preferences.hudOffsetY : 96)
        width: hudColumn.implicitWidth + 20 * hudScale
        height: hudColumn.implicitHeight + 14 * hudScale
        radius: 8 * hudScale
        color: Qt.rgba(0, 0, 0, 0.55)
        border.width: 1; border.color: Qt.rgba(1, 1, 1, 0.10)
        z: 75
        Column {
            id: hudColumn
            x: 10 * videoHud.hudScale; y: 7 * videoHud.hudScale; spacing: 1 * videoHud.hudScale
            Text {
                visible: (videoHud.items & 1) !== 0
                text: qsTr("帧率") + "  " + (veyra.submitFpsKnown ? veyra.submitFps.toFixed(1) : "—") + " fps"
                color: veyra.submitFpsKnown ? Theme.ok : Theme.t3
                font.family: Theme.fontMono; font.pixelSize: 13 * videoHud.hudScale; font.weight: Font.DemiBold
            }
            // 1% low：最近 10 秒提交间隔的 99 分位，卡顿感受的主要指标。
            Text {
                visible: (videoHud.items & 2) !== 0
                text: qsTr("1% low") + "  " + (veyra.submitLowFpsKnown ? veyra.submitLowFps.toFixed(1) : "—") + " fps"
                color: Theme.t2; font.family: Theme.fontMono; font.pixelSize: 12 * videoHud.hudScale
            }
            Text {
                visible: (videoHud.items & 4) !== 0
                text: qsTr("采集延迟") + "  " + (veyra.captureDriverAgeP50Ms > 0 ? veyra.captureDriverAgeP50Ms.toFixed(1) : "—") + " ms"
                color: Theme.t1; font.family: Theme.fontMono; font.pixelSize: 12 * videoHud.hudScale
            }
            // 引擎段：采集回调 → Present（含补帧保持与图提交）。
            Text {
                visible: (videoHud.items & 8) !== 0
                text: qsTr("引擎延迟") + "  " + (veyra.captureToPresentP50Ms > 0 ? veyra.captureToPresentP50Ms.toFixed(1) : "—") + " ms"
                color: Theme.t1; font.family: Theme.fontMono; font.pixelSize: 12 * videoHud.hudScale
            }
            // 上屏段：Present → 屏幕真正显示的那次刷新（PresentMon ETW 优先）。
            Text {
                visible: (videoHud.items & 16) !== 0
                text: qsTr("上屏等待") + (veyra.presentMonKnown ? " (PM)" : "") + "  "
                      + (videoHud.scanoutMs > 0 ? videoHud.scanoutMs.toFixed(1) : "—") + " ms"
                color: Theme.t1; font.family: Theme.fontMono; font.pixelSize: 12 * videoHud.hudScale
            }
            Text {
                visible: (videoHud.items & 32) !== 0
                text: qsTr("上屏总延迟") + "  "
                      + (videoHud.totalMs > 0 ? (videoHud.scanoutKnown ? "" : "≥") + videoHud.totalMs.toFixed(1) : "—") + " ms"
                color: Theme.t1; font.family: Theme.fontMono; font.pixelSize: 12 * videoHud.hudScale
            }
            Text {
                visible: (videoHud.items & 64) !== 0
                text: qsTr("处理耗时") + "  " + (veyra.chainTotalMsKnown ? veyra.chainTotalMs.toFixed(1) : "—") + " ms"
                color: Theme.t3; font.family: Theme.fontMono; font.pixelSize: 12 * videoHud.hudScale
            }
        }
    }
    // HUD 的位置开关放在播放条的胶囊里（CineBar 的 FPS 按钮），画面上不加按钮。
    // Shared mask input follows the exact native host across list/node/cinema/fullscreen.
    ScreenCropEditor {
        id: screenCrop
        z: 94
        hostRect: Qt.rect(videoHost.x, videoHost.y, videoHost.width, videoHost.height)
        onCancelled: {
            veyra.stopPlayback()
            dialogs.open("screen")
        }
    }
    Connections {
        target: veyra
        function onMediaOpenRequested() { screenCrop.editing = false }
    }
    MouseArea {
        id: maskInput
        objectName: "mask-edit-input"
        x: videoHost.x; y: videoHost.y; width: videoHost.width; height: videoHost.height
        z: 80
        visible: veyra.hasSource && videoHost.width > 0 && videoHost.height > 0
                 && (veyra.protectionDrawShape.length > 0 || veyra.exclusionOverlayVisible)
        enabled: visible && !screenCrop.editing && !veyra.gameInputActive && dialogs.dialog === ""
        preventStealing: true
        cursorShape: veyra.protectionDrawShape.length > 0 ? Qt.CrossCursor : pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
        property point start
        property bool drawing: false
        property bool moving: false
        onPressed: mouse => {
            const p = mapToItem(null, mouse.x, mouse.y)
            if (root.overVideoCover(p) || (root.quickPanelShown && (proPage.overlayContains(p.x,p.y) || nodePage.overlayContains(p.x,p.y)))) { mouse.accepted = false; return }
            start = Qt.point(mouse.x / width, mouse.y / height)
            drawing = veyra.protectionDrawShape.length > 0
            moving = !drawing && veyra.beginExclusionMove(start.x,start.y)
            if (!drawing && !moving) mouse.accepted = false
        }
        onPositionChanged: mouse => {
            if (!pressed) return
            if (drawing) veyra.updateProtectionDraw(start.x,start.y,mouse.x/width,mouse.y/height)
            else if (moving) veyra.updateExclusionMove(mouse.x/width,mouse.y/height)
        }
        onReleased: mouse => {
            if (drawing) veyra.commitProtectionDraw(start.x,start.y,mouse.x/width,mouse.y/height)
            else if (moving) { veyra.updateExclusionMove(mouse.x/width,mouse.y/height); veyra.endExclusionMove(true) }
            drawing = false; moving = false
        }
        onCanceled: { veyra.endExclusionMove(false); if (drawing) veyra.beginProtectionDraw(""); drawing=false; moving=false }
        Shortcut {
            sequence: "Escape"
            enabled: maskInput.moving
            onActivated: { veyra.endExclusionMove(false); maskInput.moving=false }
        }
    }
    // AppShell VideoSurface: a double click on the picture toggles fullscreen
    // (1.4.4 did both directions), unless locked. Windowed, only a double click
    // inside the picture counts; with the quick panel up, not one on the panel.
    // A click on the picture of a playing file pauses or resumes it (field request
    // 2026-10-02), and takes the keyboard back from a slider so the arrows seek again.
    // Live sources ignore it: a click to focus the window mid-game must not stop the
    // capture. The second click of a double click undoes the first; the double click
    // itself toggles fullscreen below.
    function pictureContains(p) {
        return p.x >= videoHost.x && p.y >= videoHost.y && p.x <= videoHost.x + videoHost.width
            && p.y <= videoHost.y + videoHost.height
    }
    // Anything floating over the picture is cut out of the video window (videoCover:
    // the dock, popovers, tips). A click on it belongs to it, never to the picture:
    // the window-wide handlers below used to see the dock's 专业模式 click too, so
    // leaving 极简 also paused or resumed the film (field report 2026-10-05).
    function overVideoCover(scenePoint) {
        const visit = item => {
            if (!item || !item.visible || item.opacity <= 0) return false
            if (item.objectName === "videoCover" || item.videoCover === true) {
                const q = item.mapFromItem(null, scenePoint.x, scenePoint.y)
                if (q.x >= 0 && q.y >= 0 && q.x <= item.width && q.y <= item.height) return true
            }
            for (let i = 0; i < item.children.length; ++i) if (visit(item.children[i])) return true
            return false
        }
        return visit(root.contentItem.parent ? root.contentItem.parent : root.contentItem)
    }
    TapHandler {
        id: pictureTap
        // Scaled game input deliberately catches mouse events instead of
        // forwarding wrong coordinates; they must not control the player either.
        enabled: !root.fullLocked && !screenCrop.editing && !veyra.gameInputActive
        // Judged where the press landed: by the release the page may already have changed.
        property bool pressOnCover: false
        property bool pressOnPicture: false
        onPressedChanged: if (pressed) {
            pressOnCover = root.overVideoCover(point.scenePosition)
            pressOnPicture = root.pictureContains(point.scenePosition)
        }
        onTapped: (eventPoint, button) => {
            const p = eventPoint.position
            // A transport button can enter fullscreen before this handler sees
            // release. Its press still belongs to the transport, not the newly
            // enlarged picture underneath it.
            if (pressOnCover || !pressOnPicture) return
            if (root.page !== "min" && root.page !== "pro" && root.page !== "node" && !root.fullscreen) return
            if (dialogs.dialog !== "" || !veyra.hasSource) return
            if (root.fullscreen && root.quickPanelShown && (proPage.overlayContains(p.x, p.y) || nodePage.overlayContains(p.x, p.y))) return
            // 游戏模式: a click on the picture beside the panel returns to the game.
            if (root.fullscreen && root.quickPanelShown && veyra.gameMode) { root.closeQuickPanel(); return }
            // Do not re-test geometry on release: a picture double click may
            // already have left fullscreen before this second tap restores play.
            root.contentItem.forceActiveFocus()
            if (veyra.isCapture || veyra.duration <= 0) return
            if (veyra.protectionDrawShape.length > 0 || veyra.exclusionOverlayVisible || veyra.compareMode === 2) return
            if (pictureTap.tapCount <= 2) veyra.togglePlayPause()
        }
    }
    TapHandler {
        enabled: !root.fullLocked && !veyra.gameInputActive
        property bool pressOnCover: false
        property bool pressOnPicture: false
        onPressedChanged: if (pressed) {
            pressOnCover = root.overVideoCover(point.scenePosition)
            pressOnPicture = root.pictureContains(point.scenePosition)
        }
        onDoubleTapped: (eventPoint, button) => {
            const p = eventPoint.position
            if (pressOnCover || !pressOnPicture) return
            if (root.fullscreen) {
                if (root.quickPanelShown && (proPage.overlayContains(p.x, p.y) || nodePage.overlayContains(p.x, p.y))) return
                root.toggleFullscreen()
                return
            }
            if (root.page !== "min" && root.page !== "pro" && root.page !== "node") return
            if (dialogs.dialog !== "" || !veyra.hasSource) return
            if (veyra.protectionDrawShape.length > 0 || veyra.exclusionOverlayVisible || veyra.compareMode === 2) return
            if (p.x < videoHost.x || p.y < videoHost.y || p.x > videoHost.x + videoHost.width
                || p.y > videoHost.y + videoHost.height) return
            root.toggleFullscreen()
        }
    }
    // A click on the picture of a PC stream takes the keyboard and mouse back after a release
    // (Ctrl+Alt+Shift+Z). While captured the clicks belong to the host and never get here.
    TapHandler {
        enabled: veyra.moonlight && veyra.moonlight.state.streaming === true && !veyra.moonlightCaptured && dialogs.dialog === ""
        property bool pressOnCover: false
        property bool pressOnPicture: false
        onPressedChanged: if (pressed) {
            pressOnCover = root.overVideoCover(point.scenePosition)
            pressOnPicture = root.pictureContains(point.scenePosition)
        }
        onTapped: eventPoint => {
            const p = eventPoint.position
            if (pressOnCover || !pressOnPicture) return
            if (root.page !== "min" && root.page !== "pro" && root.page !== "node") return
            if (p.x < videoHost.x || p.y < videoHost.y || p.x > videoHost.x + videoHost.width
                || p.y > videoHost.y + videoHost.height) return
            veyra.moonlightCapture(true)
        }
    }
    // Stream statistics on and off. While a PC stream has the keyboard, the capture filter sees the keys
    // first and toggles the same switch.
    Shortcut {
        sequence: "Ctrl+Alt+Shift+S"
        enabled: (veyra.moonlight && veyra.moonlight.state.streaming === true) || (veyra.xbox && veyra.xbox.state.streaming === true)
        onActivated: veyra.moonlightStatsVisible = !veyra.moonlightStatsVisible
    }
    StreamHud {
        id: streamHud
        x: 16
        y: root.fullscreen ? 16 : 52
        z: 90
    }
    PlaylistWindow {
        id: playlistWindow
        owner: root
        playlist: veyra.playlist
        onAddFiles: veyra.addPlaylistFilesDialog()
        onAddFolder: veyra.addPlaylistFolderDialog()
    }
    FullscreenBar {
        id: fullBar
        owner: root
        // Windowed cinema: the pill straddles the picture edge (D1). It hides under
        // an open dialog, which lives in the main window below it.
        cinema: root.cinema && !root.fullscreen
        pillTop: (root.maximized ? root.height : root.pictureHeight) - 46
        shown: !root.onboardingActive && veyra.hasSource && !screenCrop.editing && !root.pillHidden && !veyra.gameInputActive && (root.fullscreen ? root.fullControls && !root.fullLocked && !root.liveMode
                                                   : root.cinema && root.shownPage === "min"
                                                     && root.leavingPage === "" && dialogs.dialog === ""
                                                     && root.visibility !== Window.Minimized)
        onRequestPage: p => { if (root.fullscreen) root.toggleFullscreen(); root.goPage(p) }
        onRequestDialog: k => dialogs.open(k)
        onRequestFullscreen: root.toggleFullscreen()
        onRequestLock: root.toggleLock()
        onRequestHide: root.setPillHidden(true)
        onRequestPlaylist: playlistWindow.toggle()
        onRequestMove: if (!root.fullscreen && !root.maximized) root.startSystemMove()
        onActivity: root.pointerActivity()
    }
    // The cinema / fullscreen pill can be hidden for games (capture card, streams); the
    // dock's eye brings it back. Remembered across sessions.
    readonly property bool pillHidden: veyra.preferences.cinePillHidden === true
    function setPillHidden(hidden) {
        veyra.setPreference("cinePillHidden", hidden)
        toast.show(hidden ? qsTr("播放条已隐藏 · 鼠标移到顶部，点胶囊里的眼睛可重新打开") : qsTr("播放条已显示"), false)
    }
    function toggleLock() {
        fullLocked = !fullLocked
        if (fullLocked) dock.opened = false
        else { fullControls = true; fullHide.restart() }
        toast.show(fullLocked ? qsTr("已锁定全屏 · Ctrl+L 解锁，Esc 退出全屏") : qsTr("已解锁全屏"), false)
        veyra.logUi("ui-fullscreen", "locked=" + fullLocked + " shortcut=Ctrl+L")
    }
    // Subtitles keep clear of the control pill: fullscreen bar 24px margin + pill,
    // windowed cinema pill straddling the picture's bottom edge.
    Binding { target: veyra; property: "subtitleBottomInset"; value: fullBar.shown ? (root.fullscreen ? 84 : 56) : 0 }
    // Rebindable (设置 → 快捷键); the hold-to-compare key is handled by the bridge's
    // application event filter because it needs the key release too.
    Shortcut { sequence: veyra.shortcuts.playPause; enabled: !screenCrop.editing; onActivated: veyra.togglePlayPause() }
    // 直播模式 toggles from the keyboard too, so it works while the screen is
    // already fullscreen and the top bar is out of reach.
    Shortcut { sequence: "Ctrl+Shift+L"; autoRepeat: false; onActivated: root.toggleLiveMode() }
    Shortcut { sequence: veyra.shortcuts.fullscreen; onActivated: root.toggleFullscreen() }
    Shortcut { sequence: veyra.shortcuts.screenshot; onActivated: veyra.takeScreenshot() }
    Shortcut { sequence: veyra.shortcuts.record; autoRepeat: false; onActivated: veyra.toggleRecording("shortcut") } // a held key must not toggle at the repeat rate
    Shortcut { sequence: veyra.shortcuts.replay; autoRepeat: false; onActivated: veyra.saveReplay("shortcut") }
    Shortcut {
        sequence: veyra.shortcuts.toggleMode
        enabled: dialogs.dialog === ""
        onActivated: root.goPage(root.page === "min" ? "pro" : "min")
    }
    Shortcut { sequences: ["F11", "Alt+Return", "Alt+Enter"]; onActivated: root.toggleFullscreen() }
    // In 游戏模式 Esc closes the adjust panel and gives the input back to the game.
    Shortcut {
        sequence: "Esc"
        enabled: root.fullscreen && !screenCrop.editing && !maskInput.moving && !dialogs.captureGuideOpen
        onActivated: {
            // Visible editing operations own the first Escape. Hidden pages and
            // the hidden playlist must never compete with fullscreen's shortcut.
            if (root.quickPanelShown && veyra.protectionDrawShape.length > 0)
                veyra.beginProtectionDraw("")
            else if (root.quickPanelShown && root.page === "node" &&
                     (nodePage.wireFrom >= 0 || nodePage.marked.length > 0))
                nodePage.clearSelection()
            else if (veyra.gameMode && root.quickPanelShown) root.closeQuickPanel()
            else root.toggleFullscreen()
        }
    }
    // Home: the fullscreen quick-adjust panel, from 极简 or 专业 (list or node view).
    Shortcut {
        sequence: "Home"
        enabled: root.fullscreen && !root.fullLocked && dialogs.dialog === "" &&
                 !veyra.gameMode && (root.page === "min" || root.page === "pro" || root.page === "node")
        onActivated: root.toggleQuickPanel()
    }
    // 5 s steps (field request 2026-10-02).
    Shortcut { sequence: "Left"; onActivated: veyra.seekBy(-5) }
    Shortcut { sequence: "Right"; onActivated: veyra.seekBy(5) }
    Shortcut { sequence: "Up"; enabled: !dialogs.captureGuideOpen; onActivated: veyra.volume = Math.min(veyra.maximumVolume, veyra.volume + 0.05) }
    Shortcut { sequence: "Down"; enabled: !dialogs.captureGuideOpen; onActivated: veyra.volume = Math.max(0, veyra.volume - 0.05) }
    Shortcut { sequence: "Ctrl+O"; onActivated: veyra.openFileDialog() }
    Shortcut { sequence: "Ctrl+U"; onActivated: veyra.openNetworkDialog() }
    Shortcut { sequence: "Ctrl+E"; onActivated: root.page = "exp" }
    Shortcut {
        sequence: veyra.shortcuts.lock
        enabled: root.fullscreen
        onActivated: root.toggleLock()
    }
    // Subtitle keys, as in 1.4.4: B on/off, Z/X -/+50 ms (Shift: 1 s), T/Y cycle
    // the primary/secondary track.
    Shortcut { sequence: "B"; onActivated: veyra.toggleSubtitles() }
    Shortcut { sequence: "Z"; onActivated: veyra.nudgeSubtitle(-50) }
    Shortcut { sequence: "Shift+Z"; onActivated: veyra.nudgeSubtitle(-1000) }
    Shortcut { sequence: "X"; onActivated: veyra.nudgeSubtitle(50) }
    Shortcut { sequence: "Shift+X"; onActivated: veyra.nudgeSubtitle(1000) }
    Shortcut { sequence: "T"; onActivated: veyra.cycleSubtitle(false) }
    Shortcut { sequence: "Y"; onActivated: veyra.cycleSubtitle(true) }

    // 声音自检：游戏声音与麦克风的电平，以及「录 5 秒 → 播放」的试听。
    // 视频是原生子窗口，带 videoCover 的项才会从它里面挖洞显示（main.cpp）。
    property bool audioTestPanel: false
    Rectangle {
        id: audioTestBox
        objectName: "audio-test-panel"
        readonly property bool videoCover: true
        readonly property real coverRadius: 8
        visible: root.audioTestPanel
        width: 440
        height: audioTestCol.implicitHeight + 32
        x: Math.round((root.width - width) / 2)
        y: Math.round((root.height - height) / 2)
        radius: Theme.rCard
        color: Theme.popover
        border.width: 1
        border.color: Theme.stroke2
        z: 60
        // The probe follows the button, not this item: the initial visible
        // binding (true -> false) would otherwise stop it right after start.
        MouseArea { anchors.fill: parent; onClicked: {} }
        ColumnLayout {
            id: audioTestCol
            anchors.fill: parent
            anchors.margins: 16
            spacing: 12
            RowLayout {
                spacing: 8
                Text {
                    text: qsTr("声音自检")
                    color: Theme.t1
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.fsH2
                    font.weight: Font.DemiBold
                }
                Item { Layout.fillWidth: true }
                Text {
                    text: veyra.audioTestRunning ? qsTr("检测中") : qsTr("已停止")
                    color: Theme.t3
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.fsSmall
                }
            }
            RowLayout {
                spacing: 8
                Text {
                    text: qsTr("游戏声音")
                    color: Theme.t2
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.fsBody
                    Layout.preferredWidth: 62
                }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 8
                    radius: 4
                    color: Theme.card3
                    Rectangle {
                        height: parent.height
                        radius: 4
                        color: veyra.audioTestGameDb > -45 ? Theme.ok : (veyra.audioTestGameDb > -70 ? Theme.warn : Theme.err)
                        width: parent.width * Math.max(0, Math.min(1, (veyra.audioTestGameDb + 70) / 70))
                        Behavior on width { NumberAnimation { duration: 90; easing.type: Easing.OutQuad } }
                    }
                }
                Text {
                    text: veyra.audioTestGameDb > -119 ? Math.round(veyra.audioTestGameDb) + " dB" : "—"
                    color: Theme.t3
                    font.family: Theme.fontMono
                    font.pixelSize: Theme.fsSmall
                    Layout.preferredWidth: 52
                    horizontalAlignment: Text.AlignRight
                }
            }
            RowLayout {
                spacing: 8
                Text {
                    text: qsTr("麦克风")
                    color: Theme.t2
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.fsBody
                    Layout.preferredWidth: 62
                }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 8
                    radius: 4
                    color: Theme.card3
                    Rectangle {
                        height: parent.height
                        radius: 4
                        color: veyra.audioTestMicDb > -45 ? Theme.ok : (veyra.audioTestMicDb > -70 ? Theme.warn : Theme.err)
                        width: parent.width * Math.max(0, Math.min(1, (veyra.audioTestMicDb + 70) / 70))
                        Behavior on width { NumberAnimation { duration: 90; easing.type: Easing.OutQuad } }
                    }
                }
                Text {
                    text: veyra.audioTestMicDb > -119 ? Math.round(veyra.audioTestMicDb) + " dB" : "—"
                    color: Theme.t3
                    font.family: Theme.fontMono
                    font.pixelSize: Theme.fsSmall
                    Layout.preferredWidth: 52
                    horizontalAlignment: Text.AlignRight
                }
            }
            Text {
                text: veyra.audioTestMicStatus
                color: veyra.audioTestMicOpen ? Theme.t2 : Theme.err
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsSmall
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            RowLayout {
                spacing: 8
                VButton {
                    objectName: "audio-test-record"
                    text: veyra.audioTestRecording ? qsTr("录制中…") : qsTr("录 5 秒")
                    primary: !veyra.audioTestRecording
                    enabled: !veyra.audioTestRecording
                    onClicked: veyra.recordAudioSample()
                }
                VButton {
                    objectName: "audio-test-play"
                    text: qsTr("播放录音")
                    ghost: true
                    enabled: veyra.audioTestSamplePath.length > 0
                    onClicked: veyra.playAudioSample()
                }
                Item { Layout.fillWidth: true }
                VButton {
                    text: qsTr("关闭")
                    ghost: true
                    onClicked: { root.audioTestPanel = false; veyra.stopAudioTest() }
                }
            }
            Text {
                text: qsTr("播放游戏声音并说话，点「录 5 秒」；录完点「播放录音」听到的就是实际录制的内容。麦克风打不开时上面会写原因。")
                color: Theme.t3
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsSmall
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
        }
    }

    // 区域录制: drag the rectangle on a dim full-screen window; the take starts
    // a moment after it hides so the overlay is not in the file.
    RegionSelect {
        id: regionSelect
        onSelected: function(x, y, w, h) {
            veyra.setRecordRegion(x, y, w, h)
            regionStart.restart()
        }
    }
    Timer {
        id: regionStart
        interval: 400
        repeat: false
        onTriggered: veyra.startRecording("region")
    }
    Connections {
        target: veyra
        function onRequestRegionSelection() { regionSelect.begin(root.screen) }
    }

    // Toasts: a refused edit, a finished export, a failed open.
    UpdateBanner {
        id: updateBanner
        z: 35
        anchors.top: parent.top
        anchors.topMargin: 12
        anchors.horizontalCenter: parent.horizontalCenter
        width: Math.min(500, root.width - 32)
        onOpenRequested: {
            settingsPage.section = "about"
            root.goPage("set")
        }
    }
    Connections {
        target: veyra.updates
        function onUpdateAvailable(version) { updateBanner.show(version) }
    }
    // pages.css .canvas-toast: top 12px, padding 7px 12px, from translate -8px and
    // opacity 0; .on: opacity .2s, translate .45s --spring; hidden after 2600 ms.
    Rectangle {
        id: toast
        // Under the dock (z 40): an opened dock stays usable over a toast.
        z: 30
        property string message: ""
        property bool isError: false
        property bool on: false
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: 12
        // Over the picture: cut out of the video window.
        objectName: "videoCover"
        property real coverRadius: 9
        width: Math.min(toastText.implicitWidth + 24 + 2, root.width - 48)
        height: toastText.implicitHeight + 14 + 2
        radius: 9
        color: toast.isError ? "#2A1414" : "#132218"
        border.width: 1
        border.color: toast.isError ? Qt.rgba(1, 0.365, 0.365, 0.4) : Qt.rgba(0.239, 0.863, 0.518, 0.35)
        opacity: on ? 1 : 0
        visible: opacity > 0.01
        transform: Translate { id: toastShift; y: toast.on ? 0 : -8
            Behavior on y { NumberAnimation { duration: Theme.d(450); easing.bezierCurve: Theme.spring } } }
        Behavior on opacity { NumberAnimation { duration: Theme.d(200) } }
        function show(m, bad) { toast.message = m; toast.isError = bad; toast.on = true; toastTimer.restart() }
        Timer { id: toastTimer; interval: 2600; onTriggered: toast.on = false }
        Text {
            id: toastText
            anchors.centerIn: parent
            width: Math.min(implicitWidth, root.width - 72)
            text: toast.message
            color: toast.isError ? "#FFC9C9" : "#BDF3D2"
            font.family: Theme.fontUi
            font.pixelSize: 12
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignHCenter
        }
    }

    // RivaTuner, NVIDIA's overlay or GamePP started after Veyra (QmlPlayerBridge): RTSS's
    // OSD is kept out until a restart; the other two cannot be, and froze 2.0 in the field.
    VConfirm {
        id: overlayRestart
        objectName: "overlay-restart-confirm"
        property string kind: "rtss"
        readonly property string product: kind === "nvidia" ? qsTr("NVIDIA 信息浮窗") : qsTr("游戏加加（GamePP）")
        title: kind === "rtss" ? qsTr("检测到 RTSS 屏显服务") : qsTr("检测到 %1").arg(product)
        text: kind === "rtss"
              ? qsTr("检测到 RTSS 屏显服务正在运行。开启兼容绘制需重启 Veyra，重启后 OSD 仅显示在视频上。现在重启吗？当前播放或串流将停止。")
              : qsTr("%1 正在运行。它会同时挂接界面和视频两个画面，可能让 Veyra 卡死；开启兼容绘制需重启 Veyra，重启后界面改用软件绘制，它只作用在视频上。现在重启吗？当前播放或串流将停止。").arg(product)
        acceptText: qsTr("重启")
        rejectText: qsTr("稍后")
        onAccepted: veyra.restartForOverlayCompatibility()
    }

    Connections {
        target: veyra
        function onPlaylistRequested() { onboarding.close(); playlistWindow.open() }
        // 直播模式 keeps the captured screen free of messages; errors still show,
        // because a hidden failure is worse than one frame with a message in it.
        function onNotice(text, isError) { if (!root.liveMode || isError) toast.show(text, isError) }
        function onOverlayRestartSuggested(kind) { onboarding.close(); overlayRestart.kind = kind; overlayRestart.open() }
        function onNavigate(p) {
            onboarding.close()
            // A dialog key opens the dialog; anything else is a page.
            if (p === "network" || p === "capture" || p === "ps5" || p === "moonlight" || p === "xbox" || p === "screen"
                || p === "subtitle" || p === "translation" || p === "audio" || p === "save" || p === "manage"
                || p === "hud" || p === "padmap") {
                dialogs.open(p)
                return
            }
            root.goPage(p === "export" ? "exp" : p)
        }
    }

    // The film's aspect drives the window in cinema mode. Reported only: the
    // engine's own values, never an invented ratio.
    Connections {
        target: veyra
        function onSnapshotChanged() {
            if (root.fullscreen && veyra.fullscreenMemorySafe === false) root.toggleFullscreen()
            if (root.testAspect > 0) return
            if (veyra.previewAspect > 0.2 && Math.abs(veyra.previewAspect - root.filmAspect) > 0.02)
                root.fitToFilm(veyra.previewAspect)
        }
        function onCompareChanged() { if(root.testAspect <= 0) root.fitToFilm(veyra.previewAspect) }
    }

    // Motion probe (G0.5, --motion-probe): starts one motion 800 ms after load and logs
    // "motion-probe,<name>,<ms>,<value>" every frame for a second, to line up against the
    // design's curve from shotpage.js ?probe=. Sampled per rendered frame, so the spacing
    // follows the display rate rather than a fixed 10 ms step.
    Loader {
        id: probeSwitch
        active: root.test.motionProbe === "switch"
        x: 40; y: 120; z: 100
        sourceComponent: VSwitch {}
    }
    Loader {
        id: probeSeg
        active: root.test.motionProbe === "seg"
        x: 40; y: 160; z: 100
        sourceComponent: VSeg {
            options: [{ id: "a", label: qsTr("自动") }, { id: "b", label: qsTr("有限 / Limited") }, { id: "c", label: qsTr("完整 / Full") }]
            current: "a"
        }
    }
    Timer {
        running: root.test.motionProbe !== undefined
        interval: 800
        onTriggered: {
            const name = root.test.motionProbe
            if (name === "dock") dock.opened = true
            else if (name === "page") { root.page = "pro"; probe.t0 = Date.now() + 150; probe.running = true; return }
            else if (name === "switch") probeSwitch.item.checked = true
            else if (name === "menu") proPage.openTestMenu("source")
            else if (name === "dialog") dialogs.open("capture")
            else if (name === "seg") { probe.x0 = probeSeg.item.indicatorX; probeSeg.item.current = "c"; probe.x1 = probeSeg.item.targetX }
            probe.t0 = Date.now()
            probe.running = true
        }
    }
    FrameAnimation {
        id: probe
        property real t0: 0
        property real x0: 0
        property real x1: 1
        running: false
        onTriggered: {
            const ms = Date.now() - t0
            const name = root.test.motionProbe
            let v
            if (name === "dock") v = dock.barY
            else if (name === "page") { if (ms < 0) return; v = proPage.probeRise.target.opacity.toFixed(4) + "," + proPage.probeRise.ty.toFixed(3) }
            else if (name === "menu") v = proPage.testMenuScale.toFixed(4)
            else if (name === "dialog") v = dialogs.motionScale.toFixed(4)
            else if (name === "seg") v = ((probeSeg.item.indicatorX - x0) / (x1 - x0)).toFixed(4)
            else if (probeSwitch.item) v = probeSwitch.item.children[0].x - 3
            else return
            console.log("motion-probe," + name + "," + ms + "," + v)
            if (ms > 1000) running = false
        }
    }

    // 设置 → 启动窗口大小: a fixed size or the last one, clamped to the screen
    // and centred. A --size test switch resizes after this, so tests keep theirs.
    function applyStartupSize() {
        const choice = veyra.preferences.windowSize || "1280x800"
        const text = choice === "last" ? (veyra.preferences.lastWindow || "1280x800") : choice
        const wh = text.split("x").map(Number)
        if (wh.length !== 2 || !(wh[0] > 0) || !(wh[1] > 0)) return
        // Centred on the monitor the pointer is on when Veyra starts (the one it was
        // launched from), not across the whole desktop.
        const a = veyra.launchScreenAvailable()
        root.width = Math.max(root.minimumWidth, Math.min(wh[0], a.width - 40))
        root.height = Math.max(root.minimumHeight, Math.min(wh[1], a.height - 40))
        root.x = a.x + Math.round((a.width - root.width) / 2)
        root.y = a.y + Math.round((a.height - root.height) / 2)
    }
    onClosing: if (!root.fullscreen && !root.maximized && !root.cinema) veyra.rememberWindowSize(root.width, root.height)
    // 日落 startup intro (DuskIntro.qml, 2026-11-15): on a plain start to the home page in the
    // dusk palette, unless 设置 → 外观 → 启动动画 is off, animations are reduced or a test
    // switch is set. Its clock starts with the first frame on screen; a page change (a file
    // from the command line, a resumed source) fades it out at once.
    Loader {
        id: introLoader
        anchors.fill: parent
        z: 1000
        active: false
        sourceComponent: DuskIntro {
            skipText: qsTr("单击或按任意键跳过")
            musicClock: () => { const s = vyMusic.position(); return s >= 0 ? s * 1000 : -1 }
            onFrame: target = root.introTarget()
            onRevealHome: homePage.introHold = false
            onFinished: root.endIntro()
        }
        onLoaded: item.target = root.introTarget()
    }
    function introTarget() {
        const w = homePage.wordmarkItem
        return w ? w.mapToItem(introLoader, 0, 0, w.width, w.height) : Qt.rect(0, 0, 0, 0)
    }
    function endIntro() {
        const i = introLoader.item
        if (i) veyra.logUi("ui-intro", (i.dismissing ? "dismissed" : "finished") + " ms=" + Math.round(i.ms) + " frames=" + i.frames
                           + " worstFrameMs=" + Math.round(i.worstFrameMs) + " size=" + root.width + "x" + root.height)
        homePage.introHold = false
        introLoader.active = false
        root.contentItem.forceActiveFocus()
    }
    Connections {
        target: root
        enabled: introLoader.item !== null
        function onFrameSwapped() {
            const i = introLoader.item
            if (!i || i.running || i.dismissing || i.ms !== 0) return
            // The music and the intro start together; the intro then follows the music's clock.
            i.followMusic = root.musicOn && root.musicSounding
            i.start()
            veyra.logUi("ui-intro", "started music=" + i.followMusic)
        }
        function onPageChanged() { if (introLoader.item && root.page !== "home") introLoader.item.dismiss() }
    }
    Component.onCompleted: {
        veyra.updates.startAutomaticCheck()
        applyStartupSize()
        // Open on the configured page: the user's saved preference, or the
        // override a test passes on the command line.
        if (veyra.initialPage.length > 0)
            root.page = veyra.initialPage === "pro" && veyra.nodeMode === 1 ? "node" : veyra.initialPage
        if (root.test.tab !== undefined) proPage.tab = root.test.tab
        if (root.testAspect > 0) root.fitToFilm(root.testAspect)
        if (root.test.dialog !== undefined) dialogs.open(root.test.dialog)
        videoHost.syncRect()
        // Independent of animation, palette, reduced motion and initial page. Never restart.
        if (root.musicOn && vyMusic.available && !veyra.hasSource && !veyra.openingSource) {
            vyMusic.prepare(veyra.volume)
            vyMusic.play()
        }
        if (root.test.menu !== undefined) testMenuTimer.start()
        if (root.test.fullBar !== undefined) testFullTimer.start()
        if ((veyra.preferences.palette || "dusk") !== "classic" && veyra.preferences.introAnimation !== false
                && !veyra.reducedMotion && root.page === "home" && Object.keys(root.test).length === 0) {
            homePage.introHold = true
            introLoader.active = true
        }
    }

    // Startup music plays once, continuing after the intro or without it. Media-open
    // intent or the shared setting/button permanently stops it for this launch.
    readonly property bool musicOn: typeof vyMusic !== "undefined" && veyra.preferences.introMusic !== false
    readonly property bool musicSounding: typeof vyMusic !== "undefined" && (vyMusic.state === "loading" || vyMusic.state === "playing")
    function stopStartupMusicForSession() {
        if (typeof vyMusic === "undefined") return
        vyMusic.pause()
        veyra.logUi("ui-music", "stopped for this launch (button)")
    }
    readonly property bool sourceOpen: veyra.hasSource
    onSourceOpenChanged: if (sourceOpen && typeof vyMusic !== "undefined") vyMusic.pause()
    // Bottom right of the home page, above the intro too, while the music plays.
    Rectangle {
        id: musicButton
        objectName: "music-toggle"
        z: 1001
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 16
        width: 34
        height: 34
        radius: 17
        visible: root.musicSounding && !veyra.hasSource
        color: musicHover.hovered ? Theme.card3 : Qt.rgba(0, 0, 0, 0.28)
        border.width: 1
        border.color: Theme.stroke2
        VIcon {
            anchors.centerIn: parent
            name: "music"
            size: 16
            color: vyMusic.muted ? Theme.t3 : Theme.t1
        }
        // Muted: the note crossed out.
        Rectangle {
            visible: vyMusic.muted
            anchors.centerIn: parent
            width: 22
            height: 2
            radius: 1
            rotation: -45
            color: Theme.t2
        }
        HoverHandler { id: musicHover; cursorShape: Qt.PointingHandCursor }
        TapHandler {
            onTapped: root.stopStartupMusicForSession()
        }
        ToolTip.visible: musicHover.hovered
        ToolTip.text: qsTr("停止本次启动音乐")
    }
    // The menu anchors on laid-out buttons, so it opens once the page has settled.
    // --full-bar shown|hidden: enter fullscreen with the control window held shown
    // or held hidden, for the present-timing comparison (G2.5).
    Timer { id: testFullTimer; interval: 1500; onTriggered: { root.toggleFullscreen(); if (root.test.fullBar === "hidden") root.fullControls = false } }
    Timer {
        id: testMenuTimer
        interval: 600
        onTriggered: root.test.menu === "min-source" ? minPage.openSourceMenuForTest() : proPage.openTestMenu(root.test.menu)
    }
}
