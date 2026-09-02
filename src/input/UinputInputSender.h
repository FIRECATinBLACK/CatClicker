#pragma once

#include "InputSenderBackend.h"
#include "UinputIo.h"

#include <QtCore/QMutex>
#include <QtCore/QSet>
#include <QtCore/QStringList>

#include <memory>

namespace CatClicker {

class UinputInputSender final : public InputSenderBackend {
    Q_OBJECT

public:
    struct AvailabilityProbe {
        bool deviceNodeExists = false;
        bool openable = false;
        int openErrorCode = 0;
        QString openErrorText;
    };

    explicit UinputInputSender(std::unique_ptr<UinputIo> io = createPosixUinputIo(), QObject *parent = nullptr);
    ~UinputInputSender() override;

    QString backendId() const override;
    QString backendName() const override;
    bool initialize(const MacroDisplayInfo &display) override;
    bool isAvailable() const override;
    AvailabilityProbe availabilityProbe() const;
    QString statusText() const override;
    QStringList diagnosticLines() const override;
    int virtualHeldKeyCount() const override;
    int virtualHeldButtonCount() const override;

    bool sendKey(uint32_t keyCode, bool pressed) override;
    bool movePointerAbsolute(double x, double y) override;
    bool sendButton(int button, bool pressed) override;
    bool sendScroll(double dx, double dy) override;
    void releaseEverything() override;

private:
    struct DeviceState {
        int fd = -1;
        bool ready = false;
    };

    std::unique_ptr<UinputIo> m_io;
    mutable QMutex m_mutex;
    DeviceState m_keyboard;
    DeviceState m_pointer;
    QString m_statusText = QStringLiteral("uinput backend not initialized.");
    QString m_uinputPath = QStringLiteral("/dev/uinput");
    MacroDisplayInfo m_display;
    int m_displayCount = 0;
    bool m_hasMappingWarning = false;
    bool m_keyboardSettled = false;
    bool m_pointerSettled = false;
    QSet<uint32_t> m_backendHeldKeys;
    QSet<int> m_backendHeldButtons;
    int m_lastPointerX = 0;
    int m_lastPointerY = 0;
    bool m_hasLastPointerPosition = false;

    bool createKeyboardDevice();
    bool createPointerDevice();
    void destroyDevicesLocked();
    void destroyDeviceLocked(DeviceState &device);
    bool ensurePointerReadyForDisplayLocked(const MacroDisplayInfo &display);
    bool settleDeviceLocked(DeviceState &device, bool *settledFlag, const QString &deviceRole);
    bool emitEventLocked(int fd, quint16 type, quint16 code, qint32 value);
    bool emitSynLocked(int fd);
    bool emitWithSynLocked(int fd, quint16 type, quint16 code, qint32 value);
    bool emitAbsolutePositionLocked(int x, int y);
    bool sendKeyLocked(uint32_t keyCode, bool pressed, bool trackState);
    bool sendButtonLocked(int button, bool pressed, bool trackState);
    void rememberKeyStateLocked(uint32_t keyCode, bool pressed);
    void rememberButtonStateLocked(int button, bool pressed);
    bool setStatusLocked(const QString &status);
    int mapButtonCode(int button) const;
    int clampAxis(double value, int maxValue) const;
};

}
