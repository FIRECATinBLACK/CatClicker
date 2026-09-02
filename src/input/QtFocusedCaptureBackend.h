#pragma once

#include "InputCaptureBackend.h"

#include <QtCore/QElapsedTimer>
#include <QtCore/QSet>
#include <QtGui/QKeyEvent>
#include <QtGui/QMouseEvent>
#include <QtGui/QWheelEvent>

namespace CatClicker {

class QtFocusedCaptureBackend : public InputCaptureBackend {
    Q_OBJECT

public:
    explicit QtFocusedCaptureBackend(QObject *parent = nullptr);

    bool isAvailable() const override;
    QString unavailabilityReason() const override;
    bool startCapture() override;
    void stopCapture() override;
    void suppressShortcutUntilRelease(const QString &shortcut);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    qint64 nextTimestampUs();
    bool handleKeyEvent(QKeyEvent *event, bool pressed);
    bool handleMouseEvent(QMouseEvent *event, bool pressed);
    bool handleWheelEvent(QWheelEvent *event);
    void emitReleaseEventsForHeldState();
    static uint32_t mapQtKeyToLinux(int key);
    static int mapQtButtonToLinux(Qt::MouseButton button);
    static QSet<uint32_t> shortcutKeyCodes(const QString &shortcut);

    bool m_capturing = false;
    qint64 m_lastTimestampUs = 0;
    QElapsedTimer m_timer;
    QSet<uint32_t> m_heldKeys;
    QSet<int> m_heldButtons;
    QSet<uint32_t> m_suppressedKeysUntilRelease;
};

}
