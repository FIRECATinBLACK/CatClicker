#pragma once

#include "../macro/MacroEvent.h"

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QStringList>

namespace CatClicker {

class InputCaptureBackend : public QObject {
    Q_OBJECT

public:
    explicit InputCaptureBackend(QObject *parent = nullptr);
    ~InputCaptureBackend() override;

    virtual bool isAvailable() const = 0;
    virtual QString unavailabilityReason() const = 0;
    virtual bool startCapture() = 0;
    virtual void stopCapture() = 0;

signals:
    void eventCaptured(const CatClicker::MacroEvent &event);
    void backendError(const QString &message);
};

}
