// QML bridge: Recording responsibilities.
#include "../QmlPlayerBridge_p.h"
#include "../AudioProbe.h"
#include "veyra/engine/MicrophoneCapture.h"
#include <QDesktopServices>
#include <QDir>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

namespace veyra::ui {
// --- live recording of the processed picture --------------------------------
// The engine owns the encoder and the file; this side only starts, stops and
// shows the state. The 3 second countdown is an interface animation and runs
// before the request is sent.
QString QmlPlayerBridge::recordingDirectory() const {
    const QString chosen = impl_->prefString("recordDir");
    if (!chosen.isEmpty()) return chosen;
    const QString videos = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    return QDir(videos.isEmpty() ? QDir::homePath() : videos).filePath(QStringLiteral("Veyra"));
}
// 0 = automatic from the picture size and frame rate (LiveRecorder).
int QmlPlayerBridge::recordingBitrateMbps() const { return std::clamp(impl_->prefInt("recordBitrateMbps", 0), 0, 400); }
int QmlPlayerBridge::recordingFpsLimit() const { return std::clamp(impl_->prefInt("recordFps", 0), 0, 240); }
bool QmlPlayerBridge::recordingHevc() const { return impl_->prefBool("recordHevc", false); }
bool QmlPlayerBridge::recordingAudio() const { return impl_->prefBool("recordAudio", true); }
// 录制界面: the window as shown (picture plus panels) rather than the clean
// picture. On by default: clicking 录制 records what is on screen - the panels
// while the window is open, the picture plus HUD when it is fullscreen.
bool QmlPlayerBridge::recordingUi() const { return impl_->prefBool("recordUi", true); }
// 全屏录制: the screen and the system sound, with no Veyra source - for a PC
// game Veyra does not play itself.
bool QmlPlayerBridge::recordingFullscreen() const { return impl_->prefBool("recordFullscreen", false); }
// 区域录制: only the dragged rectangle of the screen.
bool QmlPlayerBridge::recordingRegion() const { return impl_->prefBool("recordRegion", false); }
int QmlPlayerBridge::recordRegionW() const { return impl_->prefInt("recordRegionW", 0); }
bool QmlPlayerBridge::recording() const { return impl_->recordingState.value("active").toBool(); }
QVariantMap QmlPlayerBridge::recordingState() const { return impl_->recordingState; }

bool QmlPlayerBridge::recordingMicrophone() const { return impl_->prefBool("recordMic", false); }
QString QmlPlayerBridge::recordingMicrophoneId() const { return impl_->prefString("recordMicDevice"); }
bool QmlPlayerBridge::recordingMicrophoneTrack() const { return impl_->prefBool("recordMicTrack", false); }
QVariantList QmlPlayerBridge::microphoneDevices() const { return impl_->microphones; }
bool QmlPlayerBridge::replayEnabled() const { return impl_->prefBool("replayEnabled", false); }
int QmlPlayerBridge::replaySeconds() const { return std::clamp(impl_->prefInt("replaySeconds", 60), 15, 600); }
QVariantMap QmlPlayerBridge::replayState() const { return impl_->replayState; }

// 设置 → 录制: what a recording and the replay buffer both follow.
engine::RecordingSettings QmlPlayerBridge::recordingOptions() const {
    engine::RecordingSettings options;
    options.directory = wideOf(recordingDirectory());
    options.bitrateMbps = uint32_t(recordingBitrateMbps());
    options.hevc = recordingHevc();
    options.fpsLimit = uint32_t(std::max(0, recordingFpsLimit()));
    options.audio = recordingAudio();
    options.ui = recordingUi() || recordingFullscreen() || recordingRegion();
    // 全屏/区域录制 take the sound from the system loopback: Veyra is not the
    // one playing, so its renderer tap would stay silent.
    options.systemAudio = recordingFullscreen() || recordingRegion();
    if (recordingRegion()) {
        options.regionX = impl_->prefInt("recordRegionX", 0);
        options.regionY = impl_->prefInt("recordRegionY", 0);
        options.regionW = uint32_t(std::max(0, impl_->prefInt("recordRegionW", 0)));
        options.regionH = uint32_t(std::max(0, impl_->prefInt("recordRegionH", 0)));
    }
    if (options.ui && impl_->gameWindow) options.window = uint64_t(impl_->gameWindow->winId());
    options.microphone = recordingMicrophone();
    options.microphoneId = recordingMicrophoneId().toStdWString();
    options.microphoneTrack = recordingMicrophoneTrack();
    options.splitSeconds = uint32_t(std::clamp(impl_->prefInt("recordSplitSeconds", 0), 0, 86400));
    options.splitBytes = uint64_t(std::clamp(impl_->prefInt("recordSplitMiB", 0), 0, 1048576)) << 20;
    // NVENC tuning (设置 → 录制): two-pass and psycho-visual AQ, both off by default.
    options.multipass = uint32_t(std::clamp(impl_->prefInt("recordMultipass", 0), 0, 2));
    options.psychoAq = impl_->prefBool("recordPsychoAq", false);
    options.replaySeconds = uint32_t(replaySeconds());
    // Tests only: a small memory cap shows what the cap does.
    if (const int mb = qEnvironmentVariableIntValue("VEYRA_TEST_REPLAY_MEMORY_MB"); mb > 0) options.replayMemoryBytes = uint64_t(mb) << 20;
    return options;
}

// Every start and stop names its trigger, and a shortcut also the key the
// shortcut map was offered just before (field report 2026-11-08: recording
// toggled on and off around clicks and window activation, and the log could not
// tell a key from the 专业 button).
// (A template so the private Impl's key record is passed without naming it.)
template <class Key>
static void logRecordingRequest(const char* what, const QString& source, const QString& binding, const Key& k, const char* outcome) {
    std::string detail;
    if (source.startsWith(QStringLiteral("shortcut"))) {
        const qint64 age = k.atMs ? QDateTime::currentMSecsSinceEpoch() - k.atMs : -1;
        const QKeySequence pressed(QKeyCombination(Qt::KeyboardModifiers(k.modifiers & ~Qt::KeypadModifier), Qt::Key(k.key)));
        detail = std::format(" binding={}",binding.toStdString());
        if(age>=0&&age<=1000&&pressed==QKeySequence(binding,QKeySequence::PortableText)){
            detail += std::format(" key={} vk=0x{:X} scan=0x{:X} repeat={} receiver={} ageMs={}",
                                  binding.toStdString(),k.virtualKey,k.scanCode,k.autoRepeat,k.window.toStdString(),age);
        }else detail += " keyEvent=unobserved";
    }
    veyra::log::info("ui-record", std::format("{} source={}{} {}", what, source.isEmpty() ? std::string("unnamed") : source.toStdString(), detail, outcome));
}
void QmlPlayerBridge::startRecording(const QString& source) {
    const QString binding=source==QLatin1String("shortcut")?shortcuts().value("record").toString():QString();
    if (recording()) { logRecordingRequest("start", source, binding, impl_->lastShortcutKey, "refused=already-recording"); emit notice(tr("录制已在进行中"), true); return; }
    // 全屏/区域录制 need no Veyra source: the game behind is the picture.
    if (!hasSource() && !recordingFullscreen() && !recordingRegion()) { logRecordingRequest("start", source, binding, impl_->lastShortcutKey, "refused=no-source"); emit notice(tr("没有正在播放的画面，无法录制"), true); return; }
    // 区域录制: ask for the rectangle first; the overlay calls back with it and
    // starts the take.
    if (recordingRegion() && recordRegionW() <= 0) {
        logRecordingRequest("start", source, binding, impl_->lastShortcutKey, "requesting-region");
        emit requestRegionSelection();
        return;
    }
    logRecordingRequest("start", source, binding, impl_->lastShortcutKey, "requested");
    std::wstring error;
    if (!impl_->engine.requestRecording(recordingOptions(), error)) { emit notice(uiText(error), true); return; }
    // Shown at once; the poll fills in the file and the counters.
    impl_->recordingState["active"] = true;
    impl_->recordingState["failed"] = false;
    emit recordingChanged();
}
void QmlPlayerBridge::stopRecording(const QString& source) {
    const QString binding=source==QLatin1String("shortcut")?shortcuts().value("record").toString():QString();
    if (!recording()) { logRecordingRequest("stop", source, binding, impl_->lastShortcutKey, "refused=not-recording"); return; }
    logRecordingRequest("stop", source, binding, impl_->lastShortcutKey, "requested");
    impl_->engine.stopRecording();
    impl_->recordingState["active"] = false;
    emit recordingChanged();
}
void QmlPlayerBridge::toggleRecording(const QString& source) { if (recording()) stopRecording(source); else startRecording(source); }
// --- instant replay (#103) -----------------------------------------------------
// The buffer follows 设置 → 即时回放 and the recording settings; armed from
// startup, it takes frames only while something is presented.
void QmlPlayerBridge::syncReplay() {
    std::wstring error;
    const bool enabled = replayEnabled();
    if (!impl_->engine.setReplay(enabled, recordingOptions(), error)) emit notice(uiText(error), true);
    veyra::log::info("ui-replay", std::format("replay {} seconds={} microphone={}", enabled ? "on" : "off", replaySeconds(),
                                              recordingMicrophone() ? (recordingMicrophoneTrack() ? "track" : "mixed") : "off"));
    pollReplay();
}
void QmlPlayerBridge::saveReplay(const QString& source) {
    const QString binding = source.startsWith(QStringLiteral("shortcut")) ? shortcuts().value("replay").toString() : QString();
    if (!replayEnabled()) {
        logRecordingRequest("replay-save", source, binding, impl_->lastShortcutKey, "refused=off");
        emit notice(tr("即时回放未开启：设置 → 播放 → 即时回放"), true);
        return;
    }
    std::wstring error;
    if (!impl_->engine.saveReplay(wideOf(recordingDirectory()), error)) {
        logRecordingRequest("replay-save", source, binding, impl_->lastShortcutKey, "refused");
        emit notice(uiText(error), true);
        return;
    }
    logRecordingRequest("replay-save", source, binding, impl_->lastShortcutKey, "requested");
    pollReplay();
}
// Whole seconds and 10 MB steps: the state changes about once a second, not
// at the poll rate.
void QmlPlayerBridge::pollReplay() {
    const auto live = impl_->engine.replaySnapshot();
    QVariantMap state;
    state["active"] = live.active;
    state["seconds"] = int(std::floor(live.bufferedSeconds));
    state["megabytes"] = int(live.bufferedBytes / (uint64_t(10) << 20) * 10);
    state["memoryLimited"] = live.memoryLimited;
    state["saving"] = live.saving;
    state["failed"] = live.failed;
    state["error"] = live.error.empty() ? QString() : uiText(live.error);
    state["path"] = QString::fromStdWString(live.path);
    state["encoder"] = uiText(live.encoder);
    state["audio"] = live.audio;
    state["microphone"] = live.microphone;
    state["microphoneError"] = live.microphoneError.empty() ? QString() : uiText(live.microphoneError);
    state["saved"] = QVariant::fromValue(qulonglong(live.saved));
    if (state == impl_->replayState) return;
    const auto previous = impl_->replayState;
    impl_->replayState = state;
    emit replayChanged();
    if (live.saved != impl_->replaySaved) {
        impl_->replaySaved = live.saved;
        emit notice(tr("即时回放已保存：%1").arg(state.value("path").toString()), false);
    }
    const QString error = state.value("error").toString();
    if (live.failed && !error.isEmpty() && error != previous.value("error").toString()) emit notice(error, true);
}
void QmlPlayerBridge::refreshMicrophones() {    QVariantList list;
    for (const auto& device : engine::microphoneDevices())
        list.push_back(QVariantMap{{"id", QString::fromStdWString(device.id)}, {"label", QString::fromStdWString(device.name)}});
    impl_->microphones = list;
    veyra::log::info("ui-record", std::format("microphones listed={}", list.size()));
    emit microphoneDevicesChanged();
}

// --- 声音自检 (AudioProbe.h) -------------------------------------------------
// The meters listen through the same taps a recording uses, so a moving level
// here means the file would have sound; a device that cannot be opened reports
// its reason instead of leaving a silent file to be discovered later.
void QmlPlayerBridge::startAudioTest() {
    const QString endpoint = recordingMicrophoneId();
    if (!startAudioProbe(endpoint.toStdWString(), recordingFullscreen() || recordingRegion())) return;
    impl_->audioTest = QVariantMap{{QStringLiteral("running"), true},
                                   {QStringLiteral("gameDb"), -120.0},
                                   {QStringLiteral("micDb"), -120.0},
                                   {QStringLiteral("micOpen"), false},
                                   {QStringLiteral("micStatus"), tr("正在打开麦克风…")}};
    veyra::log::info("ui-audio-test", std::format("probe started microphone={}", endpoint.isEmpty() ? std::string("default") : endpoint.toStdString()));
    emit audioTestChanged();
}
void QmlPlayerBridge::stopAudioTest() {
    if (!audioProbeRunning()) return;
    stopAudioProbe();
    impl_->audioTest.clear();
    veyra::log::info("ui-audio-test", "probe stopped");
    emit audioTestChanged();
}
bool QmlPlayerBridge::audioTestRunning() const { return impl_->audioTest.value(QStringLiteral("running")).toBool(); }
double QmlPlayerBridge::audioTestGameDb() const { return impl_->audioTest.value(QStringLiteral("gameDb"), -120.0).toDouble(); }
double QmlPlayerBridge::audioTestMicDb() const { return impl_->audioTest.value(QStringLiteral("micDb"), -120.0).toDouble(); }
bool QmlPlayerBridge::audioTestMicOpen() const { return impl_->audioTest.value(QStringLiteral("micOpen")).toBool(); }
QString QmlPlayerBridge::audioTestMicStatus() const { return impl_->audioTest.value(QStringLiteral("micStatus")).toString(); }
bool QmlPlayerBridge::audioTestRecording() const { return impl_->audioTest.value(QStringLiteral("recording")).toBool(); }
QString QmlPlayerBridge::audioTestSamplePath() const { return impl_->audioTest.value(QStringLiteral("samplePath")).toString(); }

// 录制试听: five seconds of the mix a recording would write, then the system
// player - hearing the result is faster than reading a meter.
void QmlPlayerBridge::recordAudioSample() {
    if (!audioTestRunning()) startAudioTest();
    if (!audioTestRunning()) return;
    if (audioTestRecording()) return;
    startSampleCapture();
    impl_->audioTest[QStringLiteral("recording")] = true;
    emit audioTestChanged();
    veyra::log::info("ui-audio-test", "sample capture started (5 s)");
    QTimer::singleShot(5000, this, [this] {
        QString folder = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
        if (folder.isEmpty()) folder = QDir::tempPath();
        QDir().mkpath(folder);
        const QString path = QDir(folder).filePath(QStringLiteral("veyra-audio-test.wav"));
        const bool saved = finishSampleCapture(path.toStdWString());
        impl_->audioTest[QStringLiteral("recording")] = false;
        impl_->audioTest[QStringLiteral("samplePath")] = saved ? path : QString();
        emit audioTestChanged();
        veyra::log::info("ui-audio-test", std::format("sample {} path={}", saved ? "saved" : "failed", path.toStdString()));
        if (!saved) emit notice(tr("没有录到声音：请确认正在播放并且麦克风已打开"), true);
    });
}
void QmlPlayerBridge::playAudioSample() {
    const QString path = audioTestSamplePath();
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        emit notice(tr("还没有录音：先点「录 5 秒」"), true);
        return;
    }
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(path))) emit notice(tr("无法播放录音：%1").arg(path), true);
}

// 区域录制: the overlay passes the dragged rectangle in monitor pixels; the
// next start uses it.
void QmlPlayerBridge::setRecordRegion(int x,int y,int w,int h) {
    setPreference(QStringLiteral("recordRegionX"), x);
    setPreference(QStringLiteral("recordRegionY"), y);
    setPreference(QStringLiteral("recordRegionW"), w);
    setPreference(QStringLiteral("recordRegionH"), h);
    veyra::log::info("ui-record", std::format("region x={} y={} {}x{}", x, y, w, h));
}

void QmlPlayerBridge::chooseRecordingDirectory() {
    const QString dir = QFileDialog::getExistingDirectory(nullptr, tr("选择录制保存位置"), recordingDirectory());
    if (!dir.isEmpty()) setPreference(QStringLiteral("recordDir"), dir);
}
void QmlPlayerBridge::openRecordingDirectory() {
    const QString dir = recordingDirectory();
    QDir().mkpath(dir);
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(dir))) emit notice(tr("无法打开目录：%1").arg(dir), true);
}

void QmlPlayerBridge::chooseExportPath() {
    if (impl_->chain.mode == engine::ChainMode::Node) {
        emit notice(tr("节点链尚未接入导出执行器，请切换列表模式后导出"), true);
        return;
    }
    const QString path = QFileDialog::getExistingDirectory(nullptr,tr("选择导出目录"),exportTarget());
    setExportPath(path);
}

void QmlPlayerBridge::setExportPath(const QString& path) {
    if (path.isEmpty()) return;
    impl_->exportOutput = wideOf(path);
    impl_->exportStatus = QFileInfo(path).fileName();
    emit exportChanged();
}

void QmlPlayerBridge::startExport() {
    QString error;auto settings=impl_->exportSettings(error);
    if(!settings){emit notice(error,true);return;}
    if(impl_->exportQueue.count()==0){
        if(!QFileInfo(utf8Of(impl_->sourceLabel)).isFile()){emit notice(tr("请先添加要导出的文件"),true);return;}
        const QString target=exportTarget();
        const bool fileTarget=!target.isEmpty()&&!QFileInfo(target).isDir();
        const auto id=impl_->exportQueue.addFile(utf8Of(impl_->sourceLabel),fileTarget?target:QString());
        impl_->exportQueue.queue().setTrim(id,impl_->exportTrimStartSeconds,impl_->exportTrimEndSeconds);
        impl_->exportQueue.setSelectedId(id);
    }
    if(impl_->exportOutput.empty())chooseExportPath();
    if(impl_->exportOutput.empty())return;
    const QFileInfo target(exportTarget());
    const QString folder=target.isDir()?target.absoluteFilePath():target.absolutePath();
    std::wstring reason;
    if(!impl_->exportQueue.queue().start(wideOf(folder),*settings,impl_->exportHevc,impl_->exportRateControl,reason)){
        emit notice(uiText(reason),true);emit exportChanged();return;
    }
    veyra::log::info("qml-export",std::format("export frozen bitrateMbps={} rateControl={} hevc={} srTarget={} srPrescale={} srOutput={}",
        settings->exportBitrateMbps,int(impl_->exportRateControl),impl_->exportHevc,int(settings->srTarget),uint32_t(settings->srPrescale),uint32_t(settings->srOutput)));
    impl_->exportQueue.refresh();pollExport();emit navigate(QStringLiteral("exp"));
    // Leave the GPU to the export (field request 2026-10-02): whatever plays is closed,
    // unless 导出页 → 导出时关闭正在播放的内容 is off.
    if(impl_->prefBool("exportStopsPlayback",true)&&(hasSource()||impl_->openingSource)){
        veyra::log::info("qml-export","export started: closing the playing source");
        stopPlayback();emit notice(tr("已关闭正在播放的内容，显卡全部留给导出"),false);
    }
}
void QmlPlayerBridge::cancelExport() {
    impl_->exportQueue.queue().cancel();impl_->exportQueue.refresh();pollExport();emit exportChanged();
}

void QmlPlayerBridge::restartApplication() {
    requestApplicationRestart(false);
}
void QmlPlayerBridge::restartForOverlayCompatibility() {
    requestApplicationRestart(true);
}
void QmlPlayerBridge::requestApplicationRestart(bool overlayCompatibility) {
    if (exportRunning()) {
        emit notice(tr("设置已保存，请等待导出结束后重启软件"), true);
        return;
    }
    rememberPosition(true);
    qApp->setProperty("veyraOverlayCompatRestart", overlayCompatibility);
    // main launches the replacement only after UI/engine owners unwind.
    QCoreApplication::exit(42);
}

void QmlPlayerBridge::quit() {
    rememberPosition(true);
    if (auto* app = qobject_cast<QGuiApplication*>(QCoreApplication::instance())) app->quit();
}

void QmlPlayerBridge::openCaptureDialog() { emit navigate(QStringLiteral("capture")); }
void QmlPlayerBridge::openPs5Dialog() { emit navigate(QStringLiteral("ps5")); }
void QmlPlayerBridge::openScreenCaptureDialog() { emit navigate(QStringLiteral("screen")); }
void QmlPlayerBridge::openAudioDialog() { emit navigate(QStringLiteral("audio")); }
void QmlPlayerBridge::openSubtitleDialog() { emit navigate(QStringLiteral("subtitle")); }
void QmlPlayerBridge::openHudDialog() { emit navigate(QStringLiteral("hud")); }


} // namespace veyra::ui
