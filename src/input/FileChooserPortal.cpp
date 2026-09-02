#include "FileChooserPortal.h"

#include <QtCore/QByteArray>
#include <QtCore/QDateTime>
#include <QtCore/QMetaType>
#include <QtCore/QStringList>
#include <QtCore/QVariantList>
#include <QtCore/QUuid>
#include <QtDBus/QDBusConnection>
#include <QtDBus/QDBusMessage>
#include <QtDBus/QDBusObjectPath>
#include <QtDBus/QDBusPendingCall>
#include <QtDBus/QDBusPendingCallWatcher>
#include <QtDBus/QDBusPendingReply>

namespace CatClicker {

namespace {

constexpr auto kPortalService = "org.freedesktop.portal.Desktop";
constexpr auto kPortalPath = "/org/freedesktop/portal/desktop";
constexpr auto kFileChooserInterface = "org.freedesktop.portal.FileChooser";
constexpr auto kRequestInterface = "org.freedesktop.portal.Request";

QString firstUriString(const QVariant &value)
{
    if (value.canConvert<QStringList>()) {
        const QStringList uris = value.toStringList();
        return uris.size() == 1 ? uris.constFirst() : QString();
    }

    if (value.typeId() == QMetaType::QVariantList) {
        const QVariantList uris = value.toList();
        if (uris.size() != 1 || !uris.constFirst().canConvert<QString>()) {
            return QString();
        }
        return uris.constFirst().toString();
    }

    return QString();
}

}

FileChooserPortal::FileChooserPortal(QObject *parent)
    : QObject(parent)
{
}

bool FileChooserPortal::requestSave(const QString &title, const QString &suggestedFileName, const QUrl &currentFolder)
{
    return beginRequest(Operation::Save,
                        QStringLiteral("SaveFile"),
                        title,
                        buildSaveOptions(nextHandleToken(), suggestedFileName, currentFolder));
}

bool FileChooserPortal::requestOpen(const QString &title, const QUrl &currentFolder)
{
    return beginRequest(Operation::Open,
                        QStringLiteral("OpenFile"),
                        title,
                        buildOpenOptions(nextHandleToken(), currentFolder));
}

FileChooserPortal::ParsedResponse FileChooserPortal::parseResponse(uint response, const QVariantMap &results)
{
    if (response == 1U) {
        return {
            .disposition = ResponseDisposition::Cancelled,
        };
    }

    if (response != 0U) {
        return {
            .disposition = ResponseDisposition::Failed,
            .error = QStringLiteral("File chooser request failed with portal response code %1.").arg(response),
        };
    }

    const QVariant urisValue = results.value(QStringLiteral("uris"));
    const QString uri = firstUriString(urisValue);
    if (uri.isEmpty()) {
        return {
            .disposition = ResponseDisposition::Failed,
            .error = QStringLiteral("File chooser response did not contain exactly one usable URI."),
        };
    }

    const QUrl url(uri);
    if (!url.isValid() || !url.isLocalFile()) {
        return {
            .disposition = ResponseDisposition::Failed,
            .error = QStringLiteral("File chooser returned an invalid or non-local URI: %1").arg(uri),
        };
    }

    return {
        .disposition = ResponseDisposition::Accepted,
        .url = url,
    };
}

void FileChooserPortal::handleResponse(uint response, const QVariantMap &results)
{
    const Operation operation = m_activeOperation;
    clearActiveRequest();

    const ParsedResponse parsed = parseResponse(response, results);
    switch (parsed.disposition) {
    case ResponseDisposition::Accepted:
        if (operation == Operation::Save) {
            emit saveAccepted(parsed.url);
        } else if (operation == Operation::Open) {
            emit openAccepted(parsed.url);
        }
        break;
    case ResponseDisposition::Cancelled:
        if (operation == Operation::Save) {
            emit saveCancelled();
        } else if (operation == Operation::Open) {
            emit openCancelled();
        }
        break;
    case ResponseDisposition::Failed:
        emit failed(parsed.error);
        break;
    }
}

bool FileChooserPortal::beginRequest(Operation operation, const QString &method, const QString &title, const QVariantMap &options)
{
    if (m_activeOperation != Operation::None) {
        emit failed(QStringLiteral("A file chooser request is already in progress."));
        return false;
    }

    const QString token = options.value(QStringLiteral("handle_token")).toString();
    const QString handlePath = predictedHandlePath(token);
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        emit failed(QStringLiteral("Session D-Bus is unavailable."));
        return false;
    }

    if (!subscribeToResponsePath(handlePath)) {
        emit failed(QStringLiteral("Failed to subscribe to the portal file chooser response."));
        return false;
    }

    m_activeOperation = operation;
    m_activeHandlePath = handlePath;
    m_activeToken = token;

    QDBusMessage message = QDBusMessage::createMethodCall(QString::fromLatin1(kPortalService),
                                                          QString::fromLatin1(kPortalPath),
                                                          QString::fromLatin1(kFileChooserInterface),
                                                          method);
    message << QString() << title << options;

    auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(message), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, handlePath](QDBusPendingCallWatcher *self) {
        QDBusPendingReply<QDBusObjectPath> reply = *self;
        watcher->deleteLater();

        if (!reply.isError()) {
            if (m_activeHandlePath != handlePath) {
                return;
            }

            const QString actualHandlePath = reply.value().path();
            if (actualHandlePath.size() > 1 && actualHandlePath != handlePath) {
                if (!subscribeToResponsePath(actualHandlePath)) {
                    const QString error = QStringLiteral("Portal returned request handle %1, but CatClicker could not subscribe to it.")
                                              .arg(actualHandlePath);
                    clearActiveRequest();
                    emit failed(error);
                    return;
                }

                unsubscribeFromResponsePath(handlePath);
                m_activeHandlePath = actualHandlePath;
            }
            return;
        }

        if (m_activeHandlePath != handlePath) {
            return;
        }

        const QString error = QStringLiteral("Failed to start the portal file chooser: %1").arg(reply.error().message());
        clearActiveRequest();
        emit failed(error);
    });

    return true;
}

void FileChooserPortal::clearActiveRequest()
{
    if (!m_activeHandlePath.isEmpty()) {
        unsubscribeFromResponsePath(m_activeHandlePath);
    }
    m_activeOperation = Operation::None;
    m_activeHandlePath.clear();
    m_activeToken.clear();
}

bool FileChooserPortal::subscribeToResponsePath(const QString &handlePath)
{
    return QDBusConnection::sessionBus().connect(QString::fromLatin1(kPortalService),
                                                 handlePath,
                                                 QString::fromLatin1(kRequestInterface),
                                                 QStringLiteral("Response"),
                                                 this,
                                                 SLOT(handleResponse(uint,QVariantMap)));
}

void FileChooserPortal::unsubscribeFromResponsePath(const QString &handlePath)
{
    QDBusConnection::sessionBus().disconnect(QString::fromLatin1(kPortalService),
                                             handlePath,
                                             QString::fromLatin1(kRequestInterface),
                                             QStringLiteral("Response"),
                                             this,
                                             SLOT(handleResponse(uint,QVariantMap)));
}

QString FileChooserPortal::nextHandleToken() const
{
    return QStringLiteral("catclicker_%1_%2")
        .arg(QDateTime::currentMSecsSinceEpoch())
        .arg(QUuid::createUuid().toString(QUuid::Id128));
}

QString FileChooserPortal::predictedHandlePath(const QString &token) const
{
    const QString sender = portalSenderToRequestPathElement(QDBusConnection::sessionBus().baseService());
    return QStringLiteral("/org/freedesktop/portal/desktop/request/%1/%2").arg(sender, token);
}

QString FileChooserPortal::portalSenderToRequestPathElement(QString sender)
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

QVariantMap FileChooserPortal::buildSaveOptions(const QString &token, const QString &suggestedFileName, const QUrl &currentFolder)
{
    QVariantMap options;
    options.insert(QStringLiteral("handle_token"), token);
    options.insert(QStringLiteral("modal"), true);
    options.insert(QStringLiteral("accept_label"), QStringLiteral("Save"));
    options.insert(QStringLiteral("current_name"), suggestedFileName);
    const QVariant folderValue = folderOptionValue(currentFolder);
    if (folderValue.isValid()) {
        options.insert(QStringLiteral("current_folder"), folderValue);
    }
    return options;
}

QVariantMap FileChooserPortal::buildOpenOptions(const QString &token, const QUrl &currentFolder)
{
    QVariantMap options;
    options.insert(QStringLiteral("handle_token"), token);
    options.insert(QStringLiteral("modal"), true);
    options.insert(QStringLiteral("accept_label"), QStringLiteral("Open"));
    options.insert(QStringLiteral("multiple"), false);
    options.insert(QStringLiteral("directory"), false);
    const QVariant folderValue = folderOptionValue(currentFolder);
    if (folderValue.isValid()) {
        options.insert(QStringLiteral("current_folder"), folderValue);
    }
    return options;
}

QVariant FileChooserPortal::folderOptionValue(const QUrl &currentFolder)
{
    if (!currentFolder.isLocalFile()) {
        return {};
    }

    QByteArray folder = currentFolder.toLocalFile().toUtf8();
    folder.append('\0');
    return folder;
}

}
