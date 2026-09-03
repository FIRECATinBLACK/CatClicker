#include "SingleInstanceCoordinator.h"

#include <QtNetwork/QLocalSocket>

#include <unistd.h>

#include <utility>

namespace CatClicker {

SingleInstanceCoordinator::SingleInstanceCoordinator(QString serverName, QObject *parent)
    : QObject(parent)
    , m_serverName(serverName.isEmpty() ? defaultServerName() : std::move(serverName))
{
    m_server.setSocketOptions(QLocalServer::UserAccessOption);
    connect(&m_server, &QLocalServer::newConnection, this, [this]() {
        while (QLocalSocket *socket = m_server.nextPendingConnection()) {
            const auto handleMessage = [this, socket]() {
                if (socket->readAll().contains("activate")) emit activationRequested();
                socket->disconnectFromServer();
            };
            connect(socket, &QLocalSocket::readyRead, this, handleMessage);
            connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
            if (socket->bytesAvailable() > 0) handleMessage();
        }
    });
}

SingleInstanceCoordinator::StartResult SingleInstanceCoordinator::start()
{
    if (notifyExistingInstance()) return StartResult::Secondary;

    QLocalServer::removeServer(m_serverName);
    if (m_server.listen(m_serverName)) return StartResult::Primary;

    if (notifyExistingInstance()) return StartResult::Secondary;
    m_error = m_server.errorString();
    return StartResult::Error;
}

QString SingleInstanceCoordinator::errorString() const
{
    return m_error;
}

QString SingleInstanceCoordinator::defaultServerName()
{
    return QStringLiteral("catclicker-%1").arg(static_cast<qulonglong>(::geteuid()));
}

bool SingleInstanceCoordinator::notifyExistingInstance()
{
    QLocalSocket socket;
    socket.connectToServer(m_serverName, QIODevice::WriteOnly);
    if (!socket.waitForConnected(150)) return false;
    socket.write("activate");
    socket.flush();
    socket.waitForBytesWritten(150);
    socket.disconnectFromServer();
    return true;
}

}
