#include "CursorTracker.h"

#include "BuildConfig.h"

#include <QtCore/QDateTime>
#include <QtCore/QMetaType>
#include <QtCore/QUuid>
#include <QtCore/QVariantList>
#include <QtDBus/QDBusArgument>
#include <QtDBus/QDBusConnection>
#include <QtDBus/QDBusError>
#include <QtDBus/QDBusInterface>
#include <QtDBus/QDBusMessage>
#include <QtDBus/QDBusObjectPath>
#include <QtDBus/QDBusPendingCallWatcher>
#include <QtDBus/QDBusPendingReply>
#include <QtDBus/QDBusReply>
#include <QtDBus/QDBusUnixFileDescriptor>

#if CATCLICKER_HAS_PIPEWIRE && CATCLICKER_HAS_SPA
#include <pipewire/pipewire.h>
#include <spa/buffer/buffer.h>
#include <spa/buffer/meta.h>
#include <spa/param/video/format-utils.h>
#include <spa/pod/builder.h>
#include <spa/utils/defs.h>
#endif

#include <algorithm>
#include <cmath>

namespace CatClicker {

struct CursorTracker::PipeWireRuntime {
#if CATCLICKER_HAS_PIPEWIRE && CATCLICKER_HAS_SPA
    CursorTracker *owner = nullptr;
    pw_thread_loop *threadLoop = nullptr;
    pw_context *context = nullptr;
    pw_core *core = nullptr;
    pw_stream *stream = nullptr;
    spa_hook streamListener{};
#endif
};

namespace {

constexpr auto kPortalService = "org.freedesktop.portal.Desktop";
constexpr auto kPortalDesktopPath = "/org/freedesktop/portal/desktop";
constexpr auto kScreenCastInterface = "org.freedesktop.portal.ScreenCast";
constexpr auto kRequestInterface = "org.freedesktop.portal.Request";
constexpr auto kSessionInterface = "org.freedesktop.portal.Session";
constexpr uint kScreenCastSourceTypeMonitor = 1U;
constexpr uint kScreenCastCursorModeMetadata = 4U;

bool readObjectPathValue(const QVariant &value, QString *path)
{
    if (!path) {
        return false;
    }

    if (value.canConvert<QDBusObjectPath>()) {
        const QDBusObjectPath objectPath = qvariant_cast<QDBusObjectPath>(value);
        if (!objectPath.path().isEmpty()) {
            *path = objectPath.path();
            return true;
        }
    }

    if (value.canConvert<QString>()) {
        const QString stringValue = value.toString();
        if (!stringValue.isEmpty()) {
            *path = stringValue;
            return true;
        }
    }

    return false;
}

#if CATCLICKER_HAS_PIPEWIRE && CATCLICKER_HAS_SPA

const spa_meta_cursor *findCursorMeta(const spa_buffer *buffer)
{
    if (!buffer) {
        return nullptr;
    }

    for (uint32_t i = 0; i < buffer->n_metas; ++i) {
        const spa_meta &meta = buffer->metas[i];
        if (meta.type == SPA_META_Cursor && meta.data && meta.size >= sizeof(spa_meta_cursor)) {
            return static_cast<const spa_meta_cursor *>(meta.data);
        }
    }

    return nullptr;
}

void handlePipeWireProcess(void *data)
{
    auto *runtime = static_cast<CursorTracker::PipeWireRuntime *>(data);
    if (!runtime || !runtime->owner || !runtime->stream) {
        return;
    }

    while (pw_buffer *buffer = pw_stream_dequeue_buffer(runtime->stream)) {
        if (const spa_meta_cursor *cursor = findCursorMeta(buffer->buffer)) {
            const QPointF position(cursor->position.x, cursor->position.y);
            QMetaObject::invokeMethod(runtime->owner,
                                      "applyPipeWireCursorPosition",
                                      Qt::QueuedConnection,
                                      Q_ARG(QPointF, position));
        }
        pw_stream_queue_buffer(runtime->stream, buffer);
    }
}

const pw_stream_events kCursorStreamEvents = {
    .version = PW_VERSION_STREAM_EVENTS,
    .process = handlePipeWireProcess,
};

void ensurePipeWireInitialized()
{
    static const bool initialized = []() {
        pw_init(nullptr, nullptr);
        return true;
    }();
    Q_UNUSED(initialized)
}

#endif

}

CursorTracker::CursorTracker(QObject *parent)
    : QObject(parent)
{
}

CursorTracker::~CursorTracker()
{
    stopTracking();
}

bool CursorTracker::hasBuildSupport() const
{
    return CATCLICKER_HAS_PIPEWIRE && CATCLICKER_HAS_SPA;
}

bool CursorTracker::isAvailable() const
{
    return hasBuildSupport()
        && m_capabilities.screenCastInterfaceAvailable
        && metadataCursorModeAvailable(m_capabilities.availableCursorModes);
}

bool CursorTracker::metadataCursorModeAvailable(uint availableCursorModes) const
{
    return (availableCursorModes & kScreenCastCursorModeMetadata) != 0;
}

QString CursorTracker::statusText() const
{
    QMutexLocker locker(&m_mutex);
    if (!m_statusText.isEmpty()) {
        return m_statusText;
    }

    if (!hasBuildSupport()) {
        return QStringLiteral("PipeWire/SPA support not detected at build time.");
    }

    if (!m_capabilities.screenCastInterfaceAvailable) {
        return QStringLiteral("ScreenCast portal interface is unavailable for cursor tracking.");
    }

    if (!metadataCursorModeAvailable(m_capabilities.availableCursorModes)) {
        return QStringLiteral("ScreenCast metadata cursor mode is not advertised on this compositor, so CatClicker has no trustworthy global absolute cursor source.");
    }

    return QStringLiteral("ScreenCast metadata cursor tracking is idle.");
}

QString CursorTracker::statusTextForPortal(uint availableCursorModes) const
{
    if (!hasBuildSupport()) {
        return QStringLiteral("PipeWire/SPA support not detected at build time.");
    }

    if (!metadataCursorModeAvailable(availableCursorModes)) {
        return QStringLiteral("ScreenCast metadata cursor mode is not advertised on this compositor. CatClicker will not infer absolute cursor positions from relative motion or captured pixels.");
    }

    return QStringLiteral("ScreenCast metadata cursor mode is available for global absolute cursor tracking.");
}

bool CursorTracker::startTracking(const PortalCapabilities &capabilities)
{
    stopTracking();

    {
        QMutexLocker locker(&m_mutex);
        m_capabilities = capabilities;
    }

    if (!hasBuildSupport()) {
        updateStatus(QStringLiteral("Global cursor tracking unavailable."),
                     QStringLiteral("PipeWire/SPA support not detected at build time."));
        return false;
    }
    if (!capabilities.screenCastInterfaceAvailable) {
        updateStatus(QStringLiteral("Global cursor tracking unavailable."),
                     QStringLiteral("ScreenCast portal interface is unavailable."));
        return false;
    }
    if (!metadataCursorModeAvailable(capabilities.availableCursorModes)) {
        updateStatus(QStringLiteral("Global cursor tracking unavailable."),
                     QStringLiteral("ScreenCast metadata cursor mode is not advertised on this compositor. No other trustworthy compositor-native absolute cursor source is implemented yet."));
        return false;
    }

    if (!beginPortalSession()) {
        return false;
    }

    updateStatus(QStringLiteral("Requesting ScreenCast cursor metadata session."));
    emit trackingChanged();
    return true;
}

void CursorTracker::stopTracking()
{
    QString requestPath;
    QString sessionHandlePath;
    {
        QMutexLocker locker(&m_mutex);
        requestPath = m_activeRequestPath;
        sessionHandlePath = m_sessionHandlePath;
    }

    stopPipeWireCapture();

    if (!requestPath.isEmpty()) {
        unsubscribeFromRequestPath(requestPath);
    }
    if (!sessionHandlePath.isEmpty()) {
        unsubscribeFromSessionClosed();
        QDBusMessage closeMessage = QDBusMessage::createMethodCall(QString::fromLatin1(kPortalService),
                                                                   sessionHandlePath,
                                                                   QString::fromLatin1(kSessionInterface),
                                                                   QStringLiteral("Close"));
        QDBusConnection::sessionBus().asyncCall(closeMessage);
    }

    {
        QMutexLocker locker(&m_mutex);
        resetState();
        m_statusText = QStringLiteral("Global cursor tracking idle.");
    }
    emit trackingChanged();
}

bool CursorTracker::isTracking() const
{
    QMutexLocker locker(&m_mutex);
    return m_tracking;
}

CursorSnapshot CursorTracker::cursorSnapshot() const
{
    QMutexLocker locker(&m_mutex);
    return {m_hasCursorPosition, m_lastPosition};
}

QString CursorTracker::lastError() const
{
    QMutexLocker locker(&m_mutex);
    return m_lastError;
}

void CursorTracker::handlePortalResponse(uint response, const QVariantMap &results)
{
    RequestStage stage = RequestStage::None;
    QString requestPath;
    {
        QMutexLocker locker(&m_mutex);
        stage = m_requestStage;
        requestPath = m_activeRequestPath;
        m_activeRequestPath.clear();
    }

    if (!requestPath.isEmpty()) {
        unsubscribeFromRequestPath(requestPath);
    }

    if (response != 0U) {
        stopTracking();
        updateStatus(QStringLiteral("Global cursor tracking unavailable."),
                     QStringLiteral("ScreenCast request failed with portal response code %1.").arg(response));
        return;
    }

    switch (stage) {
    case RequestStage::CreateSession: {
        QString sessionHandlePath;
        if (!readObjectPathValue(results.value(QStringLiteral("session_handle")), &sessionHandlePath)) {
            stopTracking();
            updateStatus(QStringLiteral("Global cursor tracking unavailable."),
                         QStringLiteral("ScreenCast CreateSession response did not include a session handle."));
            return;
        }

        {
            QMutexLocker locker(&m_mutex);
            m_sessionHandlePath = sessionHandlePath;
            m_requestStage = RequestStage::SelectSources;
        }

        if (!subscribeToSessionClosed() || !sendSelectSourcesRequest()) {
            stopTracking();
        }
        return;
    }
    case RequestStage::SelectSources:
        {
            QMutexLocker locker(&m_mutex);
            m_requestStage = RequestStage::Start;
        }
        if (!sendStartRequest()) {
            stopTracking();
        }
        return;
    case RequestStage::Start: {
        QString parseError;
        const StreamSelection selection = parseStartResponse(results, &parseError);
        if (selection.nodeId == 0U) {
            stopTracking();
            updateStatus(QStringLiteral("Global cursor tracking unavailable."),
                         parseError.isEmpty()
                             ? QStringLiteral("ScreenCast Start response did not contain a usable stream.")
                             : parseError);
            return;
        }

        const QDBusReply<QDBusUnixFileDescriptor> reply =
            QDBusInterface(QString::fromLatin1(kPortalService),
                           QString::fromLatin1(kPortalDesktopPath),
                           QString::fromLatin1(kScreenCastInterface),
                           QDBusConnection::sessionBus())
                .call(QStringLiteral("OpenPipeWireRemote"), QDBusObjectPath(m_sessionHandlePath), QVariantMap{});
        if (!reply.isValid() || !reply.value().isValid()) {
            stopTracking();
            updateStatus(QStringLiteral("Global cursor tracking unavailable."),
                         QStringLiteral("Failed to open the ScreenCast PipeWire remote: %1")
                             .arg(reply.isValid() ? QStringLiteral("invalid file descriptor") : reply.error().message()));
            return;
        }

        if (!startPipeWireCapture(reply.value().fileDescriptor(), selection.nodeId)) {
            stopTracking();
            return;
        }

        {
            QMutexLocker locker(&m_mutex);
            m_requestStage = RequestStage::None;
            m_streamSelection = selection;
            m_tracking = true;
            m_statusText = QStringLiteral("Global absolute cursor tracking active through ScreenCast metadata.");
        }
        emit trackingChanged();
        return;
    }
    case RequestStage::None:
        return;
    }
}

void CursorTracker::handleSessionClosed()
{
    stopTracking();
    updateStatus(QStringLiteral("Global cursor tracking unavailable."),
                 QStringLiteral("The ScreenCast session was closed by the portal or compositor."));
}

void CursorTracker::applyPipeWireCursorPosition(const QPointF &position)
{
    setCursorPosition(position);
}

bool CursorTracker::beginPortalSession()
{
    if (!QDBusConnection::sessionBus().isConnected()) {
        updateStatus(QStringLiteral("Global cursor tracking unavailable."),
                     QStringLiteral("Session D-Bus is unavailable."));
        return false;
    }

    return sendCreateSessionRequest();
}

bool CursorTracker::sendCreateSessionRequest()
{
    const QString requestToken = nextHandleToken();
    const QString handlePath = predictedHandlePath(requestToken);
    if (!subscribeToRequestPath(handlePath)) {
        updateStatus(QStringLiteral("Global cursor tracking unavailable."),
                     QStringLiteral("Failed to subscribe to the ScreenCast CreateSession response."));
        return false;
    }

    const QString sessionToken = nextHandleToken();
    QVariantMap options;
    options.insert(QStringLiteral("handle_token"), requestToken);
    options.insert(QStringLiteral("session_handle_token"), sessionToken);

    {
        QMutexLocker locker(&m_mutex);
        m_activeRequestPath = handlePath;
        m_sessionToken = sessionToken;
        m_requestStage = RequestStage::CreateSession;
    }

    QDBusMessage message = QDBusMessage::createMethodCall(QString::fromLatin1(kPortalService),
                                                          QString::fromLatin1(kPortalDesktopPath),
                                                          QString::fromLatin1(kScreenCastInterface),
                                                          QStringLiteral("CreateSession"));
    message << options;

    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, handlePath](QDBusPendingCallWatcher *self) {
        QDBusPendingReply<QDBusObjectPath> reply = *self;
        watcher->deleteLater();
        if (reply.isError()) {
            stopTracking();
            updateStatus(QStringLiteral("Global cursor tracking unavailable."),
                         QStringLiteral("Failed to start the ScreenCast session: %1").arg(reply.error().message()));
            return;
        }

        const QString actualHandlePath = reply.value().path();
        if (actualHandlePath.isEmpty() || actualHandlePath == handlePath) {
            return;
        }
        if (!subscribeToRequestPath(actualHandlePath)) {
            stopTracking();
            updateStatus(QStringLiteral("Global cursor tracking unavailable."),
                         QStringLiteral("Portal returned ScreenCast request handle %1, but CatClicker could not subscribe to it.")
                             .arg(actualHandlePath));
            return;
        }
        unsubscribeFromRequestPath(handlePath);
        QMutexLocker locker(&m_mutex);
        m_activeRequestPath = actualHandlePath;
    });

    return true;
}

bool CursorTracker::sendSelectSourcesRequest()
{
    const QString requestToken = nextHandleToken();
    const QString handlePath = predictedHandlePath(requestToken);
    if (!subscribeToRequestPath(handlePath)) {
        updateStatus(QStringLiteral("Global cursor tracking unavailable."),
                     QStringLiteral("Failed to subscribe to the ScreenCast SelectSources response."));
        return false;
    }

    QVariantMap options;
    options.insert(QStringLiteral("handle_token"), requestToken);
    options.insert(QStringLiteral("types"), kScreenCastSourceTypeMonitor);
    options.insert(QStringLiteral("multiple"), false);
    options.insert(QStringLiteral("cursor_mode"), kScreenCastCursorModeMetadata);

    {
        QMutexLocker locker(&m_mutex);
        m_activeRequestPath = handlePath;
    }

    QDBusMessage message = QDBusMessage::createMethodCall(QString::fromLatin1(kPortalService),
                                                          QString::fromLatin1(kPortalDesktopPath),
                                                          QString::fromLatin1(kScreenCastInterface),
                                                          QStringLiteral("SelectSources"));
    message << QDBusObjectPath(m_sessionHandlePath) << options;

    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, handlePath](QDBusPendingCallWatcher *self) {
        QDBusPendingReply<QDBusObjectPath> reply = *self;
        watcher->deleteLater();
        if (reply.isError()) {
            stopTracking();
            updateStatus(QStringLiteral("Global cursor tracking unavailable."),
                         QStringLiteral("Failed to request ScreenCast sources: %1").arg(reply.error().message()));
            return;
        }

        const QString actualHandlePath = reply.value().path();
        if (actualHandlePath.isEmpty() || actualHandlePath == handlePath) {
            return;
        }
        if (!subscribeToRequestPath(actualHandlePath)) {
            stopTracking();
            updateStatus(QStringLiteral("Global cursor tracking unavailable."),
                         QStringLiteral("Portal returned ScreenCast request handle %1, but CatClicker could not subscribe to it.")
                             .arg(actualHandlePath));
            return;
        }
        unsubscribeFromRequestPath(handlePath);
        QMutexLocker locker(&m_mutex);
        m_activeRequestPath = actualHandlePath;
    });

    return true;
}

bool CursorTracker::sendStartRequest()
{
    const QString requestToken = nextHandleToken();
    const QString handlePath = predictedHandlePath(requestToken);
    if (!subscribeToRequestPath(handlePath)) {
        updateStatus(QStringLiteral("Global cursor tracking unavailable."),
                     QStringLiteral("Failed to subscribe to the ScreenCast Start response."));
        return false;
    }

    {
        QMutexLocker locker(&m_mutex);
        m_activeRequestPath = handlePath;
    }

    QVariantMap options;
    options.insert(QStringLiteral("handle_token"), requestToken);

    QDBusMessage message = QDBusMessage::createMethodCall(QString::fromLatin1(kPortalService),
                                                          QString::fromLatin1(kPortalDesktopPath),
                                                          QString::fromLatin1(kScreenCastInterface),
                                                          QStringLiteral("Start"));
    message << QDBusObjectPath(m_sessionHandlePath) << QString() << options;

    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, handlePath](QDBusPendingCallWatcher *self) {
        QDBusPendingReply<QDBusObjectPath> reply = *self;
        watcher->deleteLater();
        if (reply.isError()) {
            stopTracking();
            updateStatus(QStringLiteral("Global cursor tracking unavailable."),
                         QStringLiteral("Failed to start the ScreenCast stream: %1").arg(reply.error().message()));
            return;
        }

        const QString actualHandlePath = reply.value().path();
        if (actualHandlePath.isEmpty() || actualHandlePath == handlePath) {
            return;
        }
        if (!subscribeToRequestPath(actualHandlePath)) {
            stopTracking();
            updateStatus(QStringLiteral("Global cursor tracking unavailable."),
                         QStringLiteral("Portal returned ScreenCast request handle %1, but CatClicker could not subscribe to it.")
                             .arg(actualHandlePath));
            return;
        }
        unsubscribeFromRequestPath(handlePath);
        QMutexLocker locker(&m_mutex);
        m_activeRequestPath = actualHandlePath;
    });

    return true;
}

bool CursorTracker::startPipeWireCapture(int pipeWireFd, uint nodeId)
{
#if !(CATCLICKER_HAS_PIPEWIRE && CATCLICKER_HAS_SPA)
    Q_UNUSED(pipeWireFd)
    Q_UNUSED(nodeId)
    updateStatus(QStringLiteral("Global cursor tracking unavailable."),
                 QStringLiteral("PipeWire/SPA support not detected at build time."));
    return false;
#else
    ensurePipeWireInitialized();

    auto runtime = std::make_unique<PipeWireRuntime>();
    runtime->owner = this;
    runtime->threadLoop = pw_thread_loop_new("catclicker-cursor", nullptr);
    if (!runtime->threadLoop) {
        updateStatus(QStringLiteral("Global cursor tracking unavailable."),
                     QStringLiteral("Failed to create a PipeWire thread loop."));
        return false;
    }

    pw_thread_loop_lock(runtime->threadLoop);
    runtime->context = pw_context_new(pw_thread_loop_get_loop(runtime->threadLoop), nullptr, 0);
    if (!runtime->context) {
        pw_thread_loop_unlock(runtime->threadLoop);
        pw_thread_loop_destroy(runtime->threadLoop);
        updateStatus(QStringLiteral("Global cursor tracking unavailable."),
                     QStringLiteral("Failed to create a PipeWire context."));
        return false;
    }

    runtime->core = pw_context_connect_fd(runtime->context, pipeWireFd, nullptr, 0);
    if (!runtime->core) {
        pw_context_destroy(runtime->context);
        pw_thread_loop_unlock(runtime->threadLoop);
        pw_thread_loop_destroy(runtime->threadLoop);
        updateStatus(QStringLiteral("Global cursor tracking unavailable."),
                     QStringLiteral("Failed to connect to the ScreenCast PipeWire remote."));
        return false;
    }

    pw_properties *properties = pw_properties_new(
        PW_KEY_MEDIA_TYPE, "Video",
        PW_KEY_MEDIA_CATEGORY, "Capture",
        PW_KEY_MEDIA_ROLE, "Screen",
        nullptr);
    runtime->stream = pw_stream_new(runtime->core, "CatClicker Cursor Tracker", properties);
    if (!runtime->stream) {
        pw_core_disconnect(runtime->core);
        pw_context_destroy(runtime->context);
        pw_thread_loop_unlock(runtime->threadLoop);
        pw_thread_loop_destroy(runtime->threadLoop);
        updateStatus(QStringLiteral("Global cursor tracking unavailable."),
                     QStringLiteral("Failed to create the PipeWire capture stream."));
        return false;
    }

    pw_stream_add_listener(runtime->stream, &runtime->streamListener, &kCursorStreamEvents, runtime.get());

    uint8_t buffer[1024];
    spa_pod_builder builder = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
    const spa_pod *params[1];
    params[0] = reinterpret_cast<const spa_pod *>(
        spa_pod_builder_add_object(&builder,
                                   SPA_TYPE_OBJECT_Format, SPA_PARAM_EnumFormat,
                                   SPA_FORMAT_mediaType, SPA_POD_Id(SPA_MEDIA_TYPE_video),
                                   SPA_FORMAT_mediaSubtype, SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw),
                                   SPA_FORMAT_VIDEO_format, SPA_POD_CHOICE_ENUM_Id(5,
                                                                                   SPA_VIDEO_FORMAT_BGRx,
                                                                                   SPA_VIDEO_FORMAT_BGRx,
                                                                                   SPA_VIDEO_FORMAT_BGRA,
                                                                                   SPA_VIDEO_FORMAT_RGBx,
                                                                                   SPA_VIDEO_FORMAT_RGBA)));

    const int connectResult = pw_stream_connect(runtime->stream,
                                                PW_DIRECTION_INPUT,
                                                nodeId,
                                                static_cast<pw_stream_flags>(PW_STREAM_FLAG_AUTOCONNECT
                                                                             | PW_STREAM_FLAG_MAP_BUFFERS
                                                                             | PW_STREAM_FLAG_RT_PROCESS),
                                                params,
                                                1);
    pw_thread_loop_unlock(runtime->threadLoop);
    if (connectResult != 0) {
        pw_stream_destroy(runtime->stream);
        pw_core_disconnect(runtime->core);
        pw_context_destroy(runtime->context);
        pw_thread_loop_destroy(runtime->threadLoop);
        updateStatus(QStringLiteral("Global cursor tracking unavailable."),
                     QStringLiteral("Failed to connect the PipeWire stream to the ScreenCast node."));
        return false;
    }

    if (pw_thread_loop_start(runtime->threadLoop) != 0) {
        pw_stream_disconnect(runtime->stream);
        pw_stream_destroy(runtime->stream);
        pw_core_disconnect(runtime->core);
        pw_context_destroy(runtime->context);
        pw_thread_loop_destroy(runtime->threadLoop);
        updateStatus(QStringLiteral("Global cursor tracking unavailable."),
                     QStringLiteral("Failed to start the PipeWire thread loop."));
        return false;
    }

    QMutexLocker locker(&m_mutex);
    m_pipeWireRuntime = std::move(runtime);
    m_lastError.clear();
    return true;
#endif
}

void CursorTracker::stopPipeWireCapture()
{
#if CATCLICKER_HAS_PIPEWIRE && CATCLICKER_HAS_SPA
    std::unique_ptr<PipeWireRuntime> runtime;
    {
        QMutexLocker locker(&m_mutex);
        runtime = std::move(m_pipeWireRuntime);
    }

    if (!runtime) {
        return;
    }

    pw_thread_loop_stop(runtime->threadLoop);
    pw_thread_loop_lock(runtime->threadLoop);
    if (runtime->stream) {
        pw_stream_disconnect(runtime->stream);
        pw_stream_destroy(runtime->stream);
        runtime->stream = nullptr;
    }
    if (runtime->core) {
        pw_core_disconnect(runtime->core);
        runtime->core = nullptr;
    }
    if (runtime->context) {
        pw_context_destroy(runtime->context);
        runtime->context = nullptr;
    }
    pw_thread_loop_unlock(runtime->threadLoop);
    pw_thread_loop_destroy(runtime->threadLoop);
#endif
}

void CursorTracker::resetState()
{
    m_activeRequestPath.clear();
    m_sessionHandlePath.clear();
    m_sessionToken.clear();
    m_requestStage = RequestStage::None;
    m_tracking = false;
    m_hasCursorPosition = false;
    m_lastPosition = {};
    m_streamSelection = {};
    m_lastError.clear();
}

void CursorTracker::updateStatus(const QString &status, const QString &error)
{
    QMutexLocker locker(&m_mutex);
    m_statusText = status;
    m_lastError = error;
}

void CursorTracker::setCursorPosition(const QPointF &position)
{
    const QPointF clamped(std::max(0.0, std::round(position.x())),
                          std::max(0.0, std::round(position.y())));
    {
        QMutexLocker locker(&m_mutex);
        m_lastPosition = clamped;
        m_hasCursorPosition = true;
    }
    emit cursorPositionChanged(clamped);
}

bool CursorTracker::subscribeToRequestPath(const QString &handlePath)
{
    return QDBusConnection::sessionBus().connect(QString::fromLatin1(kPortalService),
                                                 handlePath,
                                                 QString::fromLatin1(kRequestInterface),
                                                 QStringLiteral("Response"),
                                                 this,
                                                 SLOT(handlePortalResponse(uint,QVariantMap)));
}

void CursorTracker::unsubscribeFromRequestPath(const QString &handlePath)
{
    QDBusConnection::sessionBus().disconnect(QString::fromLatin1(kPortalService),
                                             handlePath,
                                             QString::fromLatin1(kRequestInterface),
                                             QStringLiteral("Response"),
                                             this,
                                             SLOT(handlePortalResponse(uint,QVariantMap)));
}

bool CursorTracker::subscribeToSessionClosed()
{
    return QDBusConnection::sessionBus().connect(QString::fromLatin1(kPortalService),
                                                 m_sessionHandlePath,
                                                 QString::fromLatin1(kSessionInterface),
                                                 QStringLiteral("Closed"),
                                                 this,
                                                 SLOT(handleSessionClosed()));
}

void CursorTracker::unsubscribeFromSessionClosed()
{
    if (m_sessionHandlePath.isEmpty()) {
        return;
    }

    QDBusConnection::sessionBus().disconnect(QString::fromLatin1(kPortalService),
                                             m_sessionHandlePath,
                                             QString::fromLatin1(kSessionInterface),
                                             QStringLiteral("Closed"),
                                             this,
                                             SLOT(handleSessionClosed()));
}

QString CursorTracker::nextHandleToken() const
{
    return QStringLiteral("catclicker_%1_%2")
        .arg(QDateTime::currentMSecsSinceEpoch())
        .arg(QUuid::createUuid().toString(QUuid::Id128));
}

QString CursorTracker::predictedHandlePath(const QString &token) const
{
    const QString sender = portalSenderToPathElement(QDBusConnection::sessionBus().baseService());
    return QStringLiteral("/org/freedesktop/portal/desktop/request/%1/%2").arg(sender, token);
}

QString CursorTracker::predictedSessionHandlePath(const QString &token) const
{
    const QString sender = portalSenderToPathElement(QDBusConnection::sessionBus().baseService());
    return QStringLiteral("/org/freedesktop/portal/desktop/session/%1/%2").arg(sender, token);
}

QString CursorTracker::portalSenderToPathElement(QString sender)
{
    if (sender.startsWith(QLatin1Char(':'))) {
        sender.remove(0, 1);
    }

    for (qsizetype i = 0; i < sender.size(); ++i) {
        if (sender[i] == QLatin1Char('.')) {
            sender[i] = QLatin1Char('_');
        }
    }

    return sender;
}

CursorTracker::StreamSelection CursorTracker::parseStartResponse(const QVariantMap &results, QString *error)
{
    const QVariant streamsValue = results.value(QStringLiteral("streams"));
    if (!streamsValue.isValid()) {
        if (error) {
            *error = QStringLiteral("ScreenCast Start response did not contain any streams.");
        }
        return {};
    }

    auto parseVariant = [](const QVariant &variant) -> StreamSelection {
        StreamSelection selection;
        if (variant.metaType().id() == qMetaTypeId<QDBusArgument>()) {
            const QDBusArgument argument = qvariant_cast<QDBusArgument>(variant);
            if (argument.currentType() == QDBusArgument::StructureType) {
                uint nodeId = 0;
                QVariantMap properties;
                argument.beginStructure();
                argument >> nodeId >> properties;
                argument.endStructure();
                selection.nodeId = nodeId;
                return selection;
            }
        }

        const QVariantList list = variant.toList();
        if (!list.isEmpty()) {
            selection.nodeId = list.first().toUInt();
        }
        return selection;
    };

    if (streamsValue.metaType().id() == qMetaTypeId<QDBusArgument>()) {
        QDBusArgument argument = qvariant_cast<QDBusArgument>(streamsValue);
        if (argument.currentType() == QDBusArgument::ArrayType) {
            argument.beginArray();
            while (!argument.atEnd()) {
                QVariant element;
                argument >> element;
                const StreamSelection selection = parseVariant(element);
                if (selection.nodeId != 0U) {
                    argument.endArray();
                    return selection;
                }
            }
            argument.endArray();
        }
    }

    const QVariantList streams = streamsValue.toList();
    for (const QVariant &stream : streams) {
        const StreamSelection selection = parseVariant(stream);
        if (selection.nodeId != 0U) {
            return selection;
        }
    }

    if (error) {
        *error = QStringLiteral("ScreenCast Start response did not contain a usable PipeWire stream node.");
    }
    return {};
}

}
