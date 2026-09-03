#pragma once

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtNetwork/QLocalServer>

namespace CatClicker {

class SingleInstanceCoordinator final : public QObject {
    Q_OBJECT

public:
    enum class StartResult { Primary, Secondary, Error };

    explicit SingleInstanceCoordinator(QString serverName = {}, QObject *parent = nullptr);
    StartResult start();
    QString errorString() const;

    static QString defaultServerName();

signals:
    void activationRequested();

private:
    bool notifyExistingInstance();

    QString m_serverName;
    QString m_error;
    QLocalServer m_server;
};

}
