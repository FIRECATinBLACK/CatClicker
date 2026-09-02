#pragma once

#include "InputSenderBackend.h"
#include "PortalController.h"

namespace CatClicker {

class EiInputSender final : public InputSenderBackend {
    Q_OBJECT

public:
    explicit EiInputSender(QObject *parent = nullptr);

    void setPortalCapabilities(const PortalCapabilities &capabilities);

    QString backendId() const override;
    QString backendName() const override;
    bool initialize(const MacroDisplayInfo &display) override;
    bool isAvailable() const override;
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
    PortalCapabilities m_portalCapabilities;
    QString m_statusText = QStringLiteral("libei session not initialized.");
};

}
