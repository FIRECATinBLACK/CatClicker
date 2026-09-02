#pragma once

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QUrl>
#include <QtCore/QVariantMap>

namespace CatClicker {

class FileChooserPortal : public QObject {
    Q_OBJECT

public:
    enum class Operation {
        None,
        Save,
        Open,
    };
    Q_ENUM(Operation)

    enum class ResponseDisposition {
        Accepted,
        Cancelled,
        Failed,
    };
    Q_ENUM(ResponseDisposition)

    struct ParsedResponse {
        ResponseDisposition disposition = ResponseDisposition::Failed;
        QUrl url;
        QString error;
    };

    explicit FileChooserPortal(QObject *parent = nullptr);

    bool requestSave(const QString &title, const QString &suggestedFileName, const QUrl &currentFolder);
    bool requestOpen(const QString &title, const QUrl &currentFolder);

    static ParsedResponse parseResponse(uint response, const QVariantMap &results);

signals:
    void saveAccepted(const QUrl &url);
    void openAccepted(const QUrl &url);
    void saveCancelled();
    void openCancelled();
    void failed(const QString &message);

private slots:
    void handleResponse(uint response, const QVariantMap &results);

private:
    Operation m_activeOperation = Operation::None;
    QString m_activeHandlePath;
    QString m_activeToken;

    bool beginRequest(Operation operation, const QString &method, const QString &title, const QVariantMap &options);
    void clearActiveRequest();
    bool subscribeToResponsePath(const QString &handlePath);
    void unsubscribeFromResponsePath(const QString &handlePath);
    QString nextHandleToken() const;
    QString predictedHandlePath(const QString &token) const;
    static QString portalSenderToRequestPathElement(QString sender);
    static QVariantMap buildSaveOptions(const QString &token, const QString &suggestedFileName, const QUrl &currentFolder);
    static QVariantMap buildOpenOptions(const QString &token, const QUrl &currentFolder);
    static QVariant folderOptionValue(const QUrl &currentFolder);
};

}
