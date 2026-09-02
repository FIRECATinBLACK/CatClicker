#pragma once

#include "PortalController.h"

#include <QtCore/QMutex>
#include <QtCore/QObject>
#include <QtCore/QPointF>
#include <QtCore/QString>
#include <QtCore/QVariantMap>

#include <memory>

namespace CatClicker {

struct CursorSnapshot {
    bool valid = false;
    QPointF position;
};

class CursorPositionProvider {
public:
    virtual ~CursorPositionProvider() = default;
    virtual CursorSnapshot cursorSnapshot() const = 0;
    bool hasCursorPosition() const { return cursorSnapshot().valid; }
    QPointF lastPosition() const { return cursorSnapshot().position; }
};

class CursorTracker : public QObject, public CursorPositionProvider {
    Q_OBJECT

public:
    struct PipeWireRuntime;

    explicit CursorTracker(QObject *parent = nullptr);
    ~CursorTracker() override;

    bool hasBuildSupport() const;
    bool isAvailable() const;
    bool metadataCursorModeAvailable(uint availableCursorModes) const;
    QString statusText() const;
    QString statusTextForPortal(uint availableCursorModes) const;
    bool startTracking(const PortalCapabilities &capabilities);
    void stopTracking();
    bool isTracking() const;
    CursorSnapshot cursorSnapshot() const override;
    QString lastError() const;

signals:
    void trackingChanged();
    void cursorPositionChanged(const QPointF &position);

private slots:
    void handlePortalResponse(uint response, const QVariantMap &results);
    void handleSessionClosed();
    void applyPipeWireCursorPosition(const QPointF &position);

private:
    enum class RequestStage {
        None,
        CreateSession,
        SelectSources,
        Start,
    };

    struct StreamSelection {
        uint nodeId = 0;
    };

    PortalCapabilities m_capabilities;
    mutable QMutex m_mutex;
    QPointF m_lastPosition;
    QString m_statusText;
    QString m_lastError;
    QString m_activeRequestPath;
    QString m_sessionHandlePath;
    QString m_sessionToken;
    RequestStage m_requestStage = RequestStage::None;
    bool m_tracking = false;
    bool m_hasCursorPosition = false;
    StreamSelection m_streamSelection;
    std::unique_ptr<PipeWireRuntime> m_pipeWireRuntime;

    bool beginPortalSession();
    bool sendCreateSessionRequest();
    bool sendSelectSourcesRequest();
    bool sendStartRequest();
    bool startPipeWireCapture(int pipeWireFd, uint nodeId);
    void stopPipeWireCapture();
    void resetState();
    void updateStatus(const QString &status, const QString &error = QString());
    void setCursorPosition(const QPointF &position);
    bool subscribeToRequestPath(const QString &handlePath);
    void unsubscribeFromRequestPath(const QString &handlePath);
    bool subscribeToSessionClosed();
    void unsubscribeFromSessionClosed();
    QString nextHandleToken() const;
    QString predictedHandlePath(const QString &token) const;
    QString predictedSessionHandlePath(const QString &token) const;
    static QString portalSenderToPathElement(QString sender);
    static StreamSelection parseStartResponse(const QVariantMap &results, QString *error);
};

}
