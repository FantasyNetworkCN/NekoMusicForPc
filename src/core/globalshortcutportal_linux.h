#pragma once

#include <QObject>
#include <QString>
#include <QDBusObjectPath>
#include <QVariantMap>

class QWindow;

/** Linux：xdg-desktop-portal org.freedesktop.portal.GlobalShortcuts */
class GlobalShortcutPortalLinux final : public QObject
{
    Q_OBJECT

public:
    explicit GlobalShortcutPortalLinux(QObject *parent = nullptr);
    ~GlobalShortcutPortalLinux() override;

    static bool isAvailable();

    void setHostWindow(QWindow *window);
    void bind(bool requestConfigureUi = false);
    void unbind();
    void openConfigureUi();
    bool isBound() const { return m_bound; }

signals:
    void shortcutActivated(const QString &shortcutId);
    void bindSucceeded();
    void bindFailed(const QString &reason);
    void configureUiFailed(const QString &reason);

private slots:
    void onPortalActivated(const QDBusObjectPath &sessionHandle, const QString &shortcutId,
                            qulonglong timestamp, const QVariantMap &options);
    void onPortalRequestResponse(uint response, const QVariantMap &results);

private:
    enum class PendingRequest { None, CreateSession, BindShortcuts, ConfigureShortcuts };

    void beginCreateSession();
    void beginBindShortcuts();
    void beginConfigureShortcuts();
    void closeSession();
    void connectActivatedSignal();
    void disconnectActivatedSignal();
    void watchPortalRequest(const QDBusObjectPath &requestPath, PendingRequest kind);
    void clearPendingRequest();
    QString parentWindowHandle() const;
    QString nextToken(const char *prefix) const;

    QWindow *m_hostWindow = nullptr;
    QString m_sessionPath;
    QString m_pendingRequestPath;
    QString m_activationToken;
    PendingRequest m_pendingRequest = PendingRequest::None;
    bool m_bound = false;
    bool m_bindingInProgress = false;
    bool m_openConfigureAfterBind = false;
};
