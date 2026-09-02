#include "CosmicCursorPositionProvider.h"

#include "BuildConfig.h"

#include <QtCore/QDebug>
#include <QtCore/QMutexLocker>
#include <QtCore/QThread>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

#if CATCLICKER_HAS_COSMIC_CURSOR_PROTOCOL
#include <wayland-client.h>
#include "ext-image-capture-source-v1-client-protocol.h"
#include "ext-image-copy-capture-v1-client-protocol.h"
#endif

namespace CatClicker {

namespace {

bool traceEnabled()
{
    static const bool enabled = qEnvironmentVariableIntValue("CATCLICKER_TRACE_CURSOR") == 1;
    return enabled;
}

void trace(const QString &message)
{
    if (traceEnabled()) {
        qInfo().noquote() << QStringLiteral("[cosmic-cursor] thread=%1 %2")
                                 .arg(QString::number(reinterpret_cast<quintptr>(QThread::currentThreadId()), 16), message);
    }
}

qint64 monotonicTimeUs()
{
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

}

void CosmicCursorState::entered()
{
    m_inside = true;
    m_hasPosition = false;
}

bool CosmicCursorState::updatePosition(const QPointF &position)
{
    if (!m_inside) {
        return false;
    }
    m_position = position;
    m_hasPosition = true;
    return true;
}

void CosmicCursorState::left()
{
    m_inside = false;
    m_hasPosition = false;
}

void CosmicCursorState::stopped()
{
    left();
}

bool CosmicCursorState::hasPosition() const
{
    return m_hasPosition;
}

QPointF CosmicCursorState::position() const
{
    return m_position;
}

bool mapCosmicCursorPosition(const CosmicCursorOutputMapping &mapping,
                             const QPointF &bufferPosition,
                             QPointF *logicalDesktopPosition,
                             QString *error)
{
    if (!logicalDesktopPosition) {
        if (error) *error = QStringLiteral("No coordinate output was supplied.");
        return false;
    }
    if (mapping.transform != 0) {
        if (error) *error = QStringLiteral("Output transform %1 is not supported by the safe single-output mapper.").arg(mapping.transform);
        return false;
    }
    if (mapping.logicalWidth <= 0 || mapping.logicalHeight <= 0
        || mapping.bufferWidth <= 0 || mapping.bufferHeight <= 0) {
        if (error) *error = QStringLiteral("Output logical or buffer dimensions are unavailable.");
        return false;
    }

    const double scaleX = static_cast<double>(mapping.logicalWidth) / mapping.bufferWidth;
    const double scaleY = static_cast<double>(mapping.logicalHeight) / mapping.bufferHeight;
    if (!std::isfinite(scaleX) || !std::isfinite(scaleY)
        || std::abs(scaleX - scaleY) > 0.01 * std::max(scaleX, scaleY)) {
        if (error) *error = QStringLiteral("Output buffer and logical geometry have incompatible aspect ratios.");
        return false;
    }

    *logicalDesktopPosition = QPointF(mapping.logicalX + bufferPosition.x() * scaleX,
                                      mapping.logicalY + bufferPosition.y() * scaleY);
    return true;
}

bool shouldUseDirectCosmicCursorProvider(const QString &currentDesktop,
                                         const QString &sessionType,
                                         const QString &waylandDisplay,
                                         bool buildSupport,
                                         bool explicitlyDisabled)
{
    return buildSupport
        && !explicitlyDisabled
        && currentDesktop.contains(QStringLiteral("COSMIC"), Qt::CaseInsensitive)
        && sessionType.compare(QStringLiteral("wayland"), Qt::CaseInsensitive) == 0
        && !waylandDisplay.isEmpty();
}

#if CATCLICKER_HAS_COSMIC_CURSOR_PROTOCOL

struct CursorSessionListenerData {
    CosmicCursorPositionProvider::Runtime *runtime = nullptr;
    quint64 generation = 0;
};

struct CosmicCursorPositionProvider::Runtime {
    CosmicCursorPositionProvider *owner = nullptr;
    wl_display *display = nullptr;
    wl_registry *registry = nullptr;
    wl_seat *seat = nullptr;
    wl_pointer *pointer = nullptr;
    wl_output *output = nullptr;
    ext_image_copy_capture_manager_v1 *captureManager = nullptr;
    ext_output_image_capture_source_manager_v1 *sourceManager = nullptr;
    ext_image_capture_source_v1 *source = nullptr;
    ext_image_copy_capture_cursor_session_v1 *cursorSession = nullptr;
    std::unique_ptr<CursorSessionListenerData> cursorSessionListenerData;
    wl_callback *syncCallback = nullptr;
    uint32_t seatName = 0;
    uint32_t outputName = 0;
    uint32_t captureManagerName = 0;
    uint32_t sourceManagerName = 0;
    quint64 cursorSessionGeneration = 0;
    int outputCount = 0;
    bool pointerCapable = false;
    CosmicCursorOutputMapping mapping;
};

namespace {

void seatCapabilities(void *data, wl_seat *seat, uint32_t capabilities)
{
    auto *runtime = static_cast<CosmicCursorPositionProvider::Runtime *>(data);
    runtime->pointerCapable = (capabilities & WL_SEAT_CAPABILITY_POINTER) != 0;
    if (runtime->pointerCapable && !runtime->pointer) {
        runtime->pointer = wl_seat_get_pointer(seat);
        trace(QStringLiteral("pointer-capable seat ready"));
    }
}

void seatName(void *, wl_seat *, const char *) {}

const wl_seat_listener kSeatListener = {
    .capabilities = seatCapabilities,
    .name = seatName,
};

void outputGeometry(void *data, wl_output *, int32_t, int32_t, int32_t, int32_t,
                    int32_t, const char *, const char *, int32_t transform)
{
    static_cast<CosmicCursorPositionProvider::Runtime *>(data)->mapping.transform = transform;
}

void outputMode(void *data, wl_output *, uint32_t flags, int32_t width, int32_t height, int32_t)
{
    if ((flags & WL_OUTPUT_MODE_CURRENT) != 0) {
        auto *runtime = static_cast<CosmicCursorPositionProvider::Runtime *>(data);
        runtime->mapping.bufferWidth = width;
        runtime->mapping.bufferHeight = height;
    }
}

void outputDone(void *, wl_output *) {}
void outputScale(void *, wl_output *, int32_t) {}
void outputName(void *, wl_output *, const char *) {}
void outputDescription(void *, wl_output *, const char *) {}

const wl_output_listener kOutputListener = {
    .geometry = outputGeometry,
    .mode = outputMode,
    .done = outputDone,
    .scale = outputScale,
    .name = outputName,
    .description = outputDescription,
};

void cursorEnter(void *data, ext_image_copy_capture_cursor_session_v1 *)
{
    auto *listener = static_cast<CursorSessionListenerData *>(data);
    trace(QStringLiteral("enter"));
    listener->runtime->owner->applyEnterForGeneration(listener->generation);
}

void cursorLeave(void *data, ext_image_copy_capture_cursor_session_v1 *)
{
    auto *listener = static_cast<CursorSessionListenerData *>(data);
    trace(QStringLiteral("leave"));
    listener->runtime->owner->applyLeaveForGeneration(listener->generation);
}

void cursorPosition(void *data, ext_image_copy_capture_cursor_session_v1 *, int32_t x, int32_t y)
{
    auto *listener = static_cast<CursorSessionListenerData *>(data);
    auto *runtime = listener->runtime;
    trace(QStringLiteral("raw position %1,%2").arg(x).arg(y));
    runtime->owner->applyPositionForGeneration(QPointF(x, y), runtime->mapping,
                                                listener->generation);
}

void cursorHotspot(void *data, ext_image_copy_capture_cursor_session_v1 *, int32_t, int32_t)
{
    auto *listener = static_cast<CursorSessionListenerData *>(data);
    listener->runtime->owner->applyHotspot();
}

const ext_image_copy_capture_cursor_session_v1_listener kCursorListener = {
    .enter = cursorEnter,
    .leave = cursorLeave,
    .position = cursorPosition,
    .hotspot = cursorHotspot,
};

void syncDone(void *data, wl_callback *callback, uint32_t)
{
    auto *runtime = static_cast<CosmicCursorPositionProvider::Runtime *>(data);
    if (runtime->syncCallback == callback) {
        runtime->syncCallback = nullptr;
    }
    wl_callback_destroy(callback);
    runtime->owner->applySyncDone();
}

const wl_callback_listener kSyncListener = {
    .done = syncDone,
};

void registryGlobal(void *data, wl_registry *registry, uint32_t name,
                    const char *interface, uint32_t version)
{
    auto *runtime = static_cast<CosmicCursorPositionProvider::Runtime *>(data);
    if (std::strcmp(interface, wl_seat_interface.name) == 0 && !runtime->seat) {
        runtime->seatName = name;
        runtime->seat = static_cast<wl_seat *>(wl_registry_bind(registry, name, &wl_seat_interface, std::min(version, 5U)));
        wl_seat_add_listener(runtime->seat, &kSeatListener, runtime);
        trace(QStringLiteral("bound wl_seat v%1").arg(std::min(version, 5U)));
    } else if (std::strcmp(interface, wl_output_interface.name) == 0) {
        ++runtime->outputCount;
        if (!runtime->output) {
            runtime->outputName = name;
            runtime->output = static_cast<wl_output *>(wl_registry_bind(registry, name, &wl_output_interface, std::min(version, 4U)));
            wl_output_add_listener(runtime->output, &kOutputListener, runtime);
            trace(QStringLiteral("bound wl_output v%1").arg(std::min(version, 4U)));
        }
    } else if (std::strcmp(interface, ext_image_copy_capture_manager_v1_interface.name) == 0) {
        runtime->captureManagerName = name;
        runtime->captureManager = static_cast<ext_image_copy_capture_manager_v1 *>(
            wl_registry_bind(registry, name, &ext_image_copy_capture_manager_v1_interface, std::min(version, 1U)));
        trace(QStringLiteral("bound ext_image_copy_capture_manager_v1 v%1").arg(std::min(version, 1U)));
    } else if (std::strcmp(interface, ext_output_image_capture_source_manager_v1_interface.name) == 0) {
        runtime->sourceManagerName = name;
        runtime->sourceManager = static_cast<ext_output_image_capture_source_manager_v1 *>(
            wl_registry_bind(registry, name, &ext_output_image_capture_source_manager_v1_interface, std::min(version, 1U)));
        trace(QStringLiteral("bound ext_output_image_capture_source_manager_v1 v%1").arg(std::min(version, 1U)));
    }
}

void registryGlobalRemove(void *data, wl_registry *, uint32_t name)
{
    auto *runtime = static_cast<CosmicCursorPositionProvider::Runtime *>(data);
    if (name == runtime->seatName || name == runtime->outputName
        || name == runtime->captureManagerName || name == runtime->sourceManagerName) {
        runtime->owner->applyStopped(QStringLiteral("A required Wayland global was removed."));
    }
}

const wl_registry_listener kRegistryListener = {
    .global = registryGlobal,
    .global_remove = registryGlobalRemove,
};

void destroyRuntime(CosmicCursorPositionProvider::Runtime *runtime)
{
    if (runtime->syncCallback) wl_callback_destroy(runtime->syncCallback);
    if (runtime->cursorSession) ext_image_copy_capture_cursor_session_v1_destroy(runtime->cursorSession);
    runtime->cursorSessionListenerData.reset();
    if (runtime->source) ext_image_capture_source_v1_destroy(runtime->source);
    if (runtime->sourceManager) ext_output_image_capture_source_manager_v1_destroy(runtime->sourceManager);
    if (runtime->captureManager) ext_image_copy_capture_manager_v1_destroy(runtime->captureManager);
    if (runtime->pointer) wl_pointer_destroy(runtime->pointer);
    if (runtime->output) wl_output_destroy(runtime->output);
    if (runtime->seat) wl_seat_destroy(runtime->seat);
    if (runtime->registry) wl_registry_destroy(runtime->registry);
    if (runtime->display) wl_display_disconnect(runtime->display);
}

}

#else

struct CosmicCursorPositionProvider::Runtime {};

#endif

CosmicCursorPositionProvider::CosmicCursorPositionProvider(QObject *parent)
    : QObject(parent)
{
#if !CATCLICKER_HAS_COSMIC_CURSOR_PROTOCOL
    m_status = QStringLiteral("unavailable");
    m_detail = QStringLiteral("Direct COSMIC cursor protocol generation was unavailable at build time.");
#endif
}

CosmicCursorPositionProvider::~CosmicCursorPositionProvider()
{
    stop();
}

bool CosmicCursorPositionProvider::hasBuildSupport() const
{
    return buildSupported();
}

bool CosmicCursorPositionProvider::buildSupported()
{
    return CATCLICKER_HAS_COSMIC_CURSOR_PROTOCOL;
}

bool CosmicCursorPositionProvider::start(const MacroDisplayInfo &display, int outputCount)
{
    stop();
    if (!hasBuildSupport()) {
        setStatus(QStringLiteral("unavailable"), QStringLiteral("Protocol generation unavailable at build time."));
        return false;
    }
    if (outputCount != 1) {
        setStatus(QStringLiteral("failed"), QStringLiteral("Coordinate mapping unsupported: direct COSMIC tracking requires exactly one Qt screen."));
        return false;
    }
    if (display.logicalWidth <= 0 || display.logicalHeight <= 0) {
        setStatus(QStringLiteral("failed"), QStringLiteral("Coordinate mapping unsupported: logical screen geometry is unavailable."));
        return false;
    }

    if (::pipe2(m_wakePipe, O_CLOEXEC | O_NONBLOCK) != 0) {
        setStatus(QStringLiteral("failed"),
                  QStringLiteral("Failed to create cursor worker wake pipe: %1")
                      .arg(QString::fromLocal8Bit(std::strerror(errno))));
        return false;
    }

    m_stopRequested = false;
    m_workerAlive = false;
    m_workerLoopCount = 0;
    m_dispatchCount = 0;
    m_prepareReadSuccessCount = 0;
    m_prepareReadRetryCount = 0;
    m_pollCount = 0;
    m_waylandFdReadableCount = 0;
    m_wakeFdReadableCount = 0;
    m_readEventsSuccessCount = 0;
    m_readEventsFailureCount = 0;
    m_dispatchPendingCount = 0;
    m_flushFailureCount = 0;
    m_wlDisplayError = 0;
    m_enterCount = 0;
    m_leaveCount = 0;
    m_positionCallbackCount = 0;
    m_hotspotCount = 0;
    m_snapshotPublishCount = 0;
    m_syncDoneCount = 0;
    m_cursorSessionGeneration = 1;
    m_cursorSessionRecreateCount = 0;
    m_positionAfterRecreateCount = 0;
    m_cursorSessionRefreshOutstanding = false;
    m_healthProbeRequested = false;
    m_cursorSessionRefreshRequested = false;
    m_captureSourceRefreshRequested = false;
    m_staleHardRefreshOutstanding = false;
    m_staleHardRefreshRequests = 0;
    m_staleHardRefreshCompletions = 0;
    m_staleHardRefreshFailures = 0;
    m_latestPositionCallbackMonotonicUs = 0;
    m_lastStaleHardRefreshMonotonicUs = 0;
    setStatus(QStringLiteral("initializing"), QStringLiteral("Connecting to direct Wayland cursor metadata protocol."));
    m_thread = std::thread(&CosmicCursorPositionProvider::run, this, display);
    return true;
}

void CosmicCursorPositionProvider::stop()
{
    m_stopRequested = true;
    wakeWorker();
    if (m_thread.joinable()) {
        m_thread.join();
    }
    for (int &fd : m_wakePipe) {
        if (fd >= 0) {
            ::close(fd);
            fd = -1;
        }
    }
    m_cursorSessionRefreshOutstanding = false;
    m_cursorInside = false;
    publishSnapshot({});
}

CursorSnapshot CosmicCursorPositionProvider::cursorSnapshot() const
{
    if (m_cursorSessionRefreshOutstanding.load(std::memory_order_acquire)) {
        return {};
    }
    CursorSnapshot snapshot;
    quint64 before = 0;
    quint64 after = 0;
    do {
        before = m_snapshotSequence.load(std::memory_order_acquire);
        if ((before & 1U) != 0) {
            std::this_thread::yield();
            continue;
        }
        snapshot.valid = m_snapshotValid.load(std::memory_order_relaxed);
        snapshot.position.setX(m_snapshotX.load(std::memory_order_relaxed));
        snapshot.position.setY(m_snapshotY.load(std::memory_order_relaxed));
        after = m_snapshotSequence.load(std::memory_order_acquire);
    } while (before != after || (after & 1U) != 0);
    return snapshot;
}

CursorProviderHealth CosmicCursorPositionProvider::healthSnapshot() const
{
    CursorProviderHealth health;
    health.workerAlive = m_workerAlive.load(std::memory_order_acquire);
    health.workerLoopCount = m_workerLoopCount.load(std::memory_order_relaxed);
    health.dispatchCount = m_dispatchCount.load(std::memory_order_relaxed);
    health.prepareReadSuccessCount = m_prepareReadSuccessCount.load(std::memory_order_relaxed);
    health.prepareReadRetryCount = m_prepareReadRetryCount.load(std::memory_order_relaxed);
    health.pollCount = m_pollCount.load(std::memory_order_relaxed);
    health.waylandFdReadableCount = m_waylandFdReadableCount.load(std::memory_order_relaxed);
    health.wakeFdReadableCount = m_wakeFdReadableCount.load(std::memory_order_relaxed);
    health.readEventsSuccessCount = m_readEventsSuccessCount.load(std::memory_order_relaxed);
    health.readEventsFailureCount = m_readEventsFailureCount.load(std::memory_order_relaxed);
    health.dispatchPendingCount = m_dispatchPendingCount.load(std::memory_order_relaxed);
    health.flushFailureCount = m_flushFailureCount.load(std::memory_order_relaxed);
    health.wlDisplayError = m_wlDisplayError.load(std::memory_order_relaxed);
    health.enterCount = m_enterCount.load(std::memory_order_relaxed);
    health.leaveCount = m_leaveCount.load(std::memory_order_relaxed);
    health.positionCallbackCount = m_positionCallbackCount.load(std::memory_order_relaxed);
    health.hotspotCount = m_hotspotCount.load(std::memory_order_relaxed);
    health.snapshotPublishCount = m_snapshotPublishCount.load(std::memory_order_relaxed);
    health.syncDoneCount = m_syncDoneCount.load(std::memory_order_relaxed);
    health.cursorSessionGeneration = m_cursorSessionGeneration.load(std::memory_order_relaxed);
    health.cursorSessionRecreateCount = m_cursorSessionRecreateCount.load(std::memory_order_relaxed);
    health.positionAfterRecreateCount = m_positionAfterRecreateCount.load(std::memory_order_relaxed);
    health.staleHardRefreshRequests = m_staleHardRefreshRequests.load(std::memory_order_relaxed);
    health.staleHardRefreshCompletions = m_staleHardRefreshCompletions.load(std::memory_order_relaxed);
    health.staleHardRefreshFailures = m_staleHardRefreshFailures.load(std::memory_order_relaxed);
    health.cursorSessionRefreshOutstanding =
        m_cursorSessionRefreshOutstanding.load(std::memory_order_relaxed);
    health.staleHardRefreshOutstanding = m_staleHardRefreshOutstanding.load(std::memory_order_relaxed);
    health.latestPositionCallbackMonotonicUs =
        m_latestPositionCallbackMonotonicUs.load(std::memory_order_relaxed);
    health.latestRefreshRequestMonotonicUs =
        m_latestRefreshRequestMonotonicUs.load(std::memory_order_relaxed);
    health.lastStaleHardRefreshMonotonicUs =
        m_lastStaleHardRefreshMonotonicUs.load(std::memory_order_relaxed);
    health.latestPublished = cursorSnapshot();
    return health;
}

void CosmicCursorPositionProvider::requestHealthProbe()
{
    m_healthProbeRequested = true;
    wakeWorker();
}

bool CosmicCursorPositionProvider::requestCursorSessionRefresh()
{
    bool expected = false;
    if (!m_cursorSessionRefreshOutstanding.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel)) {
        return false;
    }
    m_cursorSessionGeneration.fetch_add(1, std::memory_order_acq_rel);
    m_latestRefreshRequestMonotonicUs.store(monotonicTimeUs(), std::memory_order_relaxed);
    m_cursorSessionRefreshRequested = true;
    wakeWorker();
    return true;
}

bool CosmicCursorPositionProvider::supersedeCursorSessionRefresh()
{
    m_cursorSessionGeneration.fetch_add(1, std::memory_order_acq_rel);
    m_latestRefreshRequestMonotonicUs.store(monotonicTimeUs(), std::memory_order_relaxed);
    m_cursorSessionRefreshOutstanding.store(true, std::memory_order_release);
    m_cursorSessionRefreshRequested.store(true, std::memory_order_release);
    wakeWorker();
    return true;
}

bool CosmicCursorPositionProvider::requestCaptureSourceRefresh()
{
    bool expected = false;
    if (!m_staleHardRefreshOutstanding.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel)) {
        return false;
    }
    m_cursorSessionGeneration.fetch_add(1, std::memory_order_acq_rel);
    m_cursorSessionRefreshOutstanding.store(true, std::memory_order_release);
    m_cursorSessionRefreshRequested.store(false, std::memory_order_release);
    m_captureSourceRefreshRequested.store(true, std::memory_order_release);
    m_staleHardRefreshRequests.fetch_add(1, std::memory_order_relaxed);
    m_lastStaleHardRefreshMonotonicUs.store(monotonicTimeUs(), std::memory_order_relaxed);
    m_cursorInside = false;
    publishSnapshot({});
    wakeWorker();
    return true;
}

QString CosmicCursorPositionProvider::statusText() const
{
    QMutexLocker locker(&m_mutex);
    return m_detail;
}

QString CosmicCursorPositionProvider::diagnosticState() const
{
    const CursorSnapshot snapshot = cursorSnapshot();
    QMutexLocker locker(&m_mutex);
    const QString position = snapshot.valid
        ? QStringLiteral("%1,%2").arg(snapshot.position.x()).arg(snapshot.position.y())
        : QStringLiteral("none");
    return QStringLiteral("COSMIC direct cursor protocol: %1\n%2\nLast trusted cursor position: %3")
        .arg(m_status, m_detail, position);
}

void CosmicCursorPositionProvider::setStatus(const QString &state, const QString &detail)
{
    {
        QMutexLocker locker(&m_mutex);
        m_status = state;
        m_detail = detail;
    }
    emit trackingChanged();
}

void CosmicCursorPositionProvider::applyEnter()
{
    applyEnterForGeneration(m_cursorSessionGeneration.load(std::memory_order_acquire));
}

void CosmicCursorPositionProvider::applyEnterForGeneration(quint64 generation)
{
    m_enterCount.fetch_add(1, std::memory_order_relaxed);
    if (generation != m_cursorSessionGeneration.load(std::memory_order_acquire)) {
        return;
    }
    m_cursorInside = true;
    publishSnapshot({});
}

void CosmicCursorPositionProvider::applyLeave()
{
    applyLeaveForGeneration(m_cursorSessionGeneration.load(std::memory_order_acquire));
}

void CosmicCursorPositionProvider::applyLeaveForGeneration(quint64 generation)
{
    m_leaveCount.fetch_add(1, std::memory_order_relaxed);
    if (generation != m_cursorSessionGeneration.load(std::memory_order_acquire)) {
        return;
    }
    m_cursorInside = false;
    publishSnapshot({});
    emit trackingChanged();
}

void CosmicCursorPositionProvider::applyPosition(const QPointF &rawPosition,
                                                  const CosmicCursorOutputMapping &mapping)
{
    applyPositionForGeneration(rawPosition, mapping,
                               m_cursorSessionGeneration.load(std::memory_order_acquire));
}

void CosmicCursorPositionProvider::applyPositionForGeneration(
    const QPointF &rawPosition,
    const CosmicCursorOutputMapping &mapping,
    quint64 generation)
{
    m_positionCallbackCount.fetch_add(1, std::memory_order_relaxed);
    m_latestPositionCallbackMonotonicUs.store(monotonicTimeUs(), std::memory_order_relaxed);
    const quint64 currentGeneration = m_cursorSessionGeneration.load(std::memory_order_acquire);
    if (generation != currentGeneration) {
        return;
    }
    QPointF logical;
    QString error;
    if (!mapCosmicCursorPosition(mapping, rawPosition, &logical, &error)) {
        applyStopped(QStringLiteral("Coordinate mapping unsupported: %1").arg(error));
        m_stopRequested = true;
        return;
    }
    if (!m_cursorInside) {
        return;
    }
    publishSnapshot({true, logical});
    m_snapshotPublishCount.fetch_add(1, std::memory_order_relaxed);
    if (generation > 1) {
        m_positionAfterRecreateCount.fetch_add(1, std::memory_order_relaxed);
    }
    m_cursorSessionRefreshOutstanding.store(false, std::memory_order_release);
    if (m_staleHardRefreshOutstanding.exchange(false, std::memory_order_acq_rel)) {
        m_staleHardRefreshCompletions.fetch_add(1, std::memory_order_relaxed);
    }
    trace(QStringLiteral("mapped logical position %1,%2").arg(logical.x()).arg(logical.y()));
    emit cursorPositionChanged(logical);
}

void CosmicCursorPositionProvider::applyHotspot()
{
    m_hotspotCount.fetch_add(1, std::memory_order_relaxed);
}

void CosmicCursorPositionProvider::applySyncDone()
{
    m_syncDoneCount.fetch_add(1, std::memory_order_relaxed);
}

void CosmicCursorPositionProvider::applyCursorSessionRecreated(quint64 generation)
{
    m_cursorSessionGeneration.store(generation, std::memory_order_release);
    m_cursorInside = false;
    publishSnapshot({});
    m_cursorSessionRecreateCount.fetch_add(1, std::memory_order_relaxed);
}

void CosmicCursorPositionProvider::applyStopped(const QString &reason)
{
    m_stopRequested = true;
    if (m_staleHardRefreshOutstanding.exchange(false, std::memory_order_acq_rel)) {
        m_staleHardRefreshFailures.fetch_add(1, std::memory_order_relaxed);
    }
    m_cursorSessionRefreshOutstanding.store(false, std::memory_order_release);
    m_cursorInside = false;
    publishSnapshot({});
    {
        QMutexLocker locker(&m_mutex);
        m_status = QStringLiteral("failed");
        m_detail = reason;
    }
    emit trackingChanged();
}

void CosmicCursorPositionProvider::publishSnapshot(const CursorSnapshot &snapshot)
{
    // The Wayland dispatch thread is the sole live-session writer. stop() joins it
    // before invalidating, so readers never block the producer and still observe
    // valid/x/y from one completed publication.
    m_snapshotSequence.fetch_add(1, std::memory_order_acq_rel);
    m_snapshotX.store(snapshot.position.x(), std::memory_order_relaxed);
    m_snapshotY.store(snapshot.position.y(), std::memory_order_relaxed);
    m_snapshotValid.store(snapshot.valid, std::memory_order_relaxed);
    m_snapshotSequence.fetch_add(1, std::memory_order_release);
}

void CosmicCursorPositionProvider::wakeWorker()
{
    if (m_wakePipe[1] < 0) {
        return;
    }
    const char byte = 1;
    const ssize_t result = ::write(m_wakePipe[1], &byte, 1);
    Q_UNUSED(result)
}

void CosmicCursorPositionProvider::run(MacroDisplayInfo displayInfo)
{
#if CATCLICKER_HAS_COSMIC_CURSOR_PROTOCOL
    struct WorkerAliveGuard {
        std::atomic_bool &alive;
        explicit WorkerAliveGuard(std::atomic_bool &value) : alive(value) { alive.store(true, std::memory_order_release); }
        ~WorkerAliveGuard() { alive.store(false, std::memory_order_release); }
    } workerAliveGuard(m_workerAlive);

    auto runtime = std::make_unique<Runtime>();
    runtime->owner = this;
    runtime->mapping.logicalX = displayInfo.offsetX;
    runtime->mapping.logicalY = displayInfo.offsetY;
    runtime->mapping.logicalWidth = displayInfo.logicalWidth;
    runtime->mapping.logicalHeight = displayInfo.logicalHeight;
    runtime->display = wl_display_connect(nullptr);
    if (!runtime->display) {
        applyStopped(QStringLiteral("Failed to connect to the Wayland display: %1").arg(QString::fromLocal8Bit(std::strerror(errno))));
        return;
    }
    runtime->registry = wl_display_get_registry(runtime->display);
    wl_registry_add_listener(runtime->registry, &kRegistryListener, runtime.get());
    if (wl_display_roundtrip(runtime->display) < 0 || wl_display_roundtrip(runtime->display) < 0) {
        applyStopped(QStringLiteral("Wayland registry initialization failed."));
        destroyRuntime(runtime.get());
        return;
    }
    if (!runtime->captureManager || !runtime->sourceManager) {
        applyStopped(QStringLiteral("Required ext-image-copy-capture globals are not advertised."));
        destroyRuntime(runtime.get());
        return;
    }
    if (!runtime->seat || !runtime->pointerCapable || !runtime->pointer) {
        applyStopped(QStringLiteral("No pointer-capable wl_seat is available."));
        destroyRuntime(runtime.get());
        return;
    }
    if (!runtime->output || runtime->outputCount != 1) {
        applyStopped(QStringLiteral("Coordinate mapping unsupported: direct tracking requires exactly one advertised wl_output."));
        destroyRuntime(runtime.get());
        return;
    }

    QString mappingError;
    QPointF ignored;
    if (!mapCosmicCursorPosition(runtime->mapping, QPointF(), &ignored, &mappingError)) {
        applyStopped(QStringLiteral("Coordinate mapping unsupported: %1").arg(mappingError));
        destroyRuntime(runtime.get());
        return;
    }

    const auto createSource = [&]() {
        runtime->source = ext_output_image_capture_source_manager_v1_create_source(
            runtime->sourceManager, runtime->output);
        return runtime->source != nullptr;
    };
    if (!createSource()) {
        applyStopped(QStringLiteral("Failed to create cursor capture source."));
        destroyRuntime(runtime.get());
        return;
    }
    const auto createCursorSession = [&]() {
        runtime->cursorSession = ext_image_copy_capture_manager_v1_create_pointer_cursor_session(
            runtime->captureManager, runtime->source, runtime->pointer);
        if (!runtime->cursorSession) {
            return false;
        }
        runtime->cursorSessionGeneration =
            m_cursorSessionGeneration.load(std::memory_order_acquire);
        runtime->cursorSessionListenerData = std::make_unique<CursorSessionListenerData>();
        runtime->cursorSessionListenerData->runtime = runtime.get();
        runtime->cursorSessionListenerData->generation = runtime->cursorSessionGeneration;
        ext_image_copy_capture_cursor_session_v1_add_listener(
            runtime->cursorSession, &kCursorListener, runtime->cursorSessionListenerData.get());
        return true;
    };
    if (!createCursorSession()) {
        applyStopped(QStringLiteral("Failed to create cursor metadata session."));
        destroyRuntime(runtime.get());
        return;
    }
    trace(QStringLiteral("cursor session created (no capture session or frames)"));
    setStatus(QStringLiteral("active"), QStringLiteral("Direct Wayland cursor-position session active; no screen frames are captured."));

    const auto processWorkerRequests = [&]() {
        if (m_healthProbeRequested.exchange(false, std::memory_order_acq_rel)
            && !runtime->syncCallback) {
            runtime->syncCallback = wl_display_sync(runtime->display);
            if (runtime->syncCallback) {
                wl_callback_add_listener(runtime->syncCallback, &kSyncListener, runtime.get());
            }
        }

        if (m_captureSourceRefreshRequested.exchange(false, std::memory_order_acq_rel)) {
            m_cursorSessionRefreshRequested.store(false, std::memory_order_release);
            if (runtime->cursorSession) {
                ext_image_copy_capture_cursor_session_v1_destroy(runtime->cursorSession);
                runtime->cursorSession = nullptr;
            }
            runtime->cursorSessionListenerData.reset();
            if (runtime->source) {
                ext_image_capture_source_v1_destroy(runtime->source);
                runtime->source = nullptr;
            }
            m_cursorInside = false;
            publishSnapshot({});
            if (createSource() && createCursorSession()) {
                applyCursorSessionRecreated(runtime->cursorSessionGeneration);
            } else {
                m_cursorSessionRefreshOutstanding.store(false, std::memory_order_release);
                applyStopped(QStringLiteral("Failed to rebuild the cursor capture source."));
            }
        } else if (m_cursorSessionRefreshRequested.exchange(false, std::memory_order_acq_rel)) {
            if (runtime->cursorSession) {
                ext_image_copy_capture_cursor_session_v1_destroy(runtime->cursorSession);
                runtime->cursorSession = nullptr;
            }
            runtime->cursorSessionListenerData.reset();
            m_cursorInside = false;
            publishSnapshot({});
            if (createCursorSession()) {
                applyCursorSessionRecreated(runtime->cursorSessionGeneration);
            } else {
                m_cursorSessionRefreshOutstanding.store(false, std::memory_order_release);
                applyStopped(QStringLiteral("Failed to recreate cursor metadata session."));
            }
        }
    };

    const int waylandFd = wl_display_get_fd(runtime->display);
    while (!m_stopRequested) {
        m_workerLoopCount.fetch_add(1, std::memory_order_relaxed);
        processWorkerRequests();

        while (wl_display_prepare_read(runtime->display) != 0) {
            m_prepareReadRetryCount.fetch_add(1, std::memory_order_relaxed);
            if (wl_display_dispatch_pending(runtime->display) < 0) {
                applyStopped(QStringLiteral("Wayland pending-event dispatch failed."));
                break;
            }
            m_dispatchPendingCount.fetch_add(1, std::memory_order_relaxed);
            m_dispatchCount.fetch_add(1, std::memory_order_relaxed);
        }
        if (m_stopRequested) {
            break;
        }
        m_prepareReadSuccessCount.fetch_add(1, std::memory_order_relaxed);

        if (wl_display_flush(runtime->display) < 0) {
            m_flushFailureCount.fetch_add(1, std::memory_order_relaxed);
            if (errno != EAGAIN) {
                wl_display_cancel_read(runtime->display);
                applyStopped(QStringLiteral("Wayland display flush failed."));
                break;
            }
        }

        pollfd items[2] = {
            {waylandFd, POLLIN | POLLERR | POLLHUP, 0},
            {m_wakePipe[0], POLLIN | POLLERR | POLLHUP, 0},
        };
        m_pollCount.fetch_add(1, std::memory_order_relaxed);
        const int result = ::poll(items, 2, -1);
        if (result < 0 && errno == EINTR) {
            wl_display_cancel_read(runtime->display);
            continue;
        }
        if (result < 0 || (items[0].revents & (POLLERR | POLLHUP))) {
            wl_display_cancel_read(runtime->display);
            applyStopped(QStringLiteral("Wayland compositor connection stopped."));
            break;
        }
        if (items[0].revents & POLLIN) {
            m_waylandFdReadableCount.fetch_add(1, std::memory_order_relaxed);
            if (wl_display_read_events(runtime->display) < 0) {
                m_readEventsFailureCount.fetch_add(1, std::memory_order_relaxed);
                applyStopped(QStringLiteral("Wayland event read failed."));
                break;
            }
            m_readEventsSuccessCount.fetch_add(1, std::memory_order_relaxed);
            if (wl_display_dispatch_pending(runtime->display) < 0) {
                applyStopped(QStringLiteral("Wayland event dispatch failed."));
                break;
            }
            m_dispatchPendingCount.fetch_add(1, std::memory_order_relaxed);
            m_dispatchCount.fetch_add(1, std::memory_order_relaxed);
        } else {
            wl_display_cancel_read(runtime->display);
        }

        if (items[1].revents & (POLLIN | POLLERR | POLLHUP)) {
            m_wakeFdReadableCount.fetch_add(1, std::memory_order_relaxed);
            char bytes[64];
            while (::read(m_wakePipe[0], bytes, sizeof(bytes)) > 0) {
            }
        }
        m_wlDisplayError.store(wl_display_get_error(runtime->display), std::memory_order_relaxed);
    }
    m_wlDisplayError.store(wl_display_get_error(runtime->display), std::memory_order_relaxed);
    destroyRuntime(runtime.get());
#else
    Q_UNUSED(displayInfo)
#endif
}

}
