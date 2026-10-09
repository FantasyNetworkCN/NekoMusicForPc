#pragma once

/**
 * @file settingspage.h
 * @brief 设置页面
 */

#include <QWidget>
#include "core/appshortcuts.h"
#include "theme/thememanager.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
#include "core/nekonetworkaccessmanager.h"
class QNetworkReply;
class QPixmap;
class QPushButton;
class QResizeEvent;
class QScrollArea;
class QStackedWidget;
class QVBoxLayout;
class QWidget;
class ApiClient;
class McpServer;
class ShortcutCaptureButton;
class ToggleSwitch;

class SettingsPage : public QWidget
{
    Q_OBJECT

public:
    explicit SettingsPage(ApiClient *apiClient, QWidget *parent = nullptr);

    /** 绑定内置 MCP 服务端，用于展示运行状态。 */
    void attachMcpServer(McpServer *server);

signals:
    void languageChanged(int language);
    void checkForUpdatesRequested();
    /** MCP 开关/端口/令牌等发生变化，主窗口据此重启服务端。 */
    void mcpSettingsChanged();

protected:
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void setupUi();
    void retranslate();
    QWidget *createSettingsCard(QWidget *parent, QVBoxLayout **layoutOut = nullptr);
    QPushButton *createTabButton(const QString &text, const char *iconName, QWidget *parent);
    void setActiveSettingsTab(int index);
    void updateTabBarGeometry();
    void setupShortcutRow(QVBoxLayout *parentLayout, QWidget *cardBody, AppShortcuts::Action action,
                          QLabel **labelOut, ShortcutCaptureButton **captureOut, QPushButton **resetOut);
    void applyShortcutChange(AppShortcuts::Action action, const QKeySequence &seq,
                             ShortcutCaptureButton *editor);
    void refreshShortcutEditors();
    void refreshMicSyncRow();
    void setupPersonalizationSection(QVBoxLayout *cardLay, QWidget *cardBody);
    void setupMcpSection(QVBoxLayout *cardLay, QWidget *cardBody);
    void persistMcpSettings();
    void refreshMcpStatus();
    void copyMcpClientConfig();
    /** 设置页独立的滚动条样式（不依赖全局 QSS，避免被上级样式覆盖）。 */
    void applyScrollbarStyle();
    void refreshAccountSection();
    /** 按当前语言的最宽标签重算账号信息标题列宽度，避免硬编码导致文字被裁切。 */
    void updateAccountCaptionWidth();
    void startEditNickname();
    void cancelEditNickname();
    void submitNickname();
    void loadAccountAvatar(int userId);
    void setAccountAvatar(const QPixmap &pixmap);
    void updateBackdropOptionRows();
    void refreshBackdropPathLabel();
    void refreshBackdropColorSwatch();

    QLabel *m_mcpTitleLabel = nullptr;
    QLabel *m_mcpEnableLabel = nullptr;
    QLabel *m_mcpPortLabel = nullptr;
    QLabel *m_mcpTokenLabel = nullptr;
    QLabel *m_mcpHintLabel = nullptr;
    QPushButton *m_mcpTabBtn = nullptr;
    ToggleSwitch *m_mcpToggle = nullptr;
    QLineEdit *m_mcpPortEdit = nullptr;
    QLineEdit *m_mcpTokenEdit = nullptr;
    QCheckBox *m_mcpRemoteCheck = nullptr;
    QLabel *m_mcpStatusLabel = nullptr;
    QLabel *m_mcpFeedbackLabel = nullptr;
    QPushButton *m_mcpGenerateBtn = nullptr;
    QPushButton *m_mcpCopyBtn = nullptr;
    QPushButton *m_mcpApplyBtn = nullptr;
    McpServer *m_mcpServer = nullptr;
    QComboBox *m_langCombo = nullptr;
    QComboBox *m_themeCombo = nullptr;
    QPushButton *m_generalTabBtn = nullptr;
    QPushButton *m_appearanceTabBtn = nullptr;
    QPushButton *m_shortcutsTabBtn = nullptr;
    QPushButton *m_aboutTabBtn = nullptr;
    QScrollArea *m_scrollArea = nullptr;
    QScrollArea *m_tabScroller = nullptr;
    QStackedWidget *m_settingsStack = nullptr;
    QWidget *m_tabBarWidget = nullptr;
    QLabel *m_titleLabel = nullptr;
    QLabel *m_descLabel = nullptr;
    QLabel *m_themeLabel = nullptr;
    QLabel *m_personalizeSectionLabel = nullptr;
    QLabel *m_backdropKindLabel = nullptr;
    QComboBox *m_backdropKindCombo = nullptr;
    QWidget *m_backdropImageRow = nullptr;
    QPushButton *m_backdropPickImageBtn = nullptr;
    QPushButton *m_backdropResetImageBtn = nullptr;
    QLabel *m_backdropPathLabel = nullptr;
    QWidget *m_backdropSolidRow = nullptr;
    QPushButton *m_backdropPickColorBtn = nullptr;
    QLabel *m_backdropColorSwatch = nullptr;
    ApiClient *m_apiClient = nullptr;
    NekoNetworkAccessManager *m_nam = nullptr;
    QNetworkReply *m_avatarReply = nullptr;

    QLabel *m_accountSectionLabel = nullptr;
    QWidget *m_accountContent = nullptr;
    QLabel *m_accountAvatar = nullptr;
    QLabel *m_accountNicknameCaption = nullptr;
    QLabel *m_accountNicknameValue = nullptr;
    QLineEdit *m_accountNicknameEdit = nullptr;
    QPushButton *m_accountEditBtn = nullptr;
    QPushButton *m_accountSaveBtn = nullptr;
    QPushButton *m_accountCancelBtn = nullptr;
    QLabel *m_accountNicknameError = nullptr;
    QLabel *m_accountEmailCaption = nullptr;
    QLabel *m_accountEmailValue = nullptr;
    QLabel *m_accountVipCaption = nullptr;
    QLabel *m_accountVipValue = nullptr;
    QLabel *m_accountCreatedCaption = nullptr;
    QLabel *m_accountCreatedValue = nullptr;
    QWidget *m_accountGuestWrap = nullptr;
    QLabel *m_accountGuestHint = nullptr;
    QPushButton *m_accountLoginBtn = nullptr;
    bool m_accountSaving = false;
    QLabel *m_langLabel = nullptr;
    QLabel *m_sysNotifyLabel = nullptr;
    QLabel *m_sysNotifyHintLabel = nullptr;
    ToggleSwitch *m_sysNotifyToggle = nullptr;
    QLabel *m_micSyncSectionLabel = nullptr;
    QLabel *m_micSyncEnableLabel = nullptr;
    ToggleSwitch *m_micSyncToggle = nullptr;
    QLabel *m_micSyncHintLabel = nullptr;
    QPushButton *m_micSyncInstallBtn = nullptr;
    QLabel *m_shortcutsSectionLabel = nullptr;
    QLabel *m_shortcutPlayPauseLabel = nullptr;
    QLabel *m_shortcutPrevLabel = nullptr;
    QLabel *m_shortcutNextLabel = nullptr;
    QLabel *m_shortcutMicSyncLabel = nullptr;
    QLabel *m_shortcutDesktopLyricsLabel = nullptr;
    ShortcutCaptureButton *m_shortcutPlayPauseBtn = nullptr;
    ShortcutCaptureButton *m_shortcutPrevBtn = nullptr;
    ShortcutCaptureButton *m_shortcutNextBtn = nullptr;
    ShortcutCaptureButton *m_shortcutMicSyncBtn = nullptr;
    ShortcutCaptureButton *m_shortcutDesktopLyricsBtn = nullptr;
    QPushButton *m_shortcutResetAllBtn = nullptr;
    QPushButton *m_shortcutResetPlayPauseBtn = nullptr;
    QPushButton *m_shortcutResetPrevBtn = nullptr;
    QPushButton *m_shortcutResetNextBtn = nullptr;
    QPushButton *m_shortcutResetMicSyncBtn = nullptr;
    QPushButton *m_shortcutResetDesktopLyricsBtn = nullptr;
    QLabel *m_aboutLabel = nullptr;
    QLabel *m_versionLabel = nullptr;
    QLabel *m_systemLabel = nullptr;
    QPushButton *m_githubBtn = nullptr;
    QPushButton *m_apiDocsBtn = nullptr;
    QPushButton *m_userAgreementBtn = nullptr;
    QPushButton *m_privacyPolicyBtn = nullptr;
    QPushButton *m_checkUpdateBtn = nullptr;
};
