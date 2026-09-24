#pragma once

/**
 * @file settingspage.h
 * @brief 设置页面
 */

#include <QWidget>
#include "core/appshortcuts.h"
#include "theme/thememanager.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QNetworkAccessManager;
class QNetworkReply;
class QPixmap;
class QPushButton;
class QResizeEvent;
class QScrollArea;
class QStackedWidget;
class QVBoxLayout;
class QWidget;
class ApiClient;
class ShortcutCaptureButton;
class ToggleSwitch;

class SettingsPage : public QWidget
{
    Q_OBJECT

public:
    explicit SettingsPage(ApiClient *apiClient, QWidget *parent = nullptr);

signals:
    void languageChanged(int language);
    void checkForUpdatesRequested();

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
    void refreshAccountSection();
    void startEditNickname();
    void cancelEditNickname();
    void submitNickname();
    void loadAccountAvatar(int userId);
    void setAccountAvatar(const QPixmap &pixmap);
    void updateBackdropOptionRows();
    void refreshBackdropPathLabel();
    void refreshBackdropColorSwatch();

    QComboBox *m_langCombo = nullptr;
    QComboBox *m_themeCombo = nullptr;
    QPushButton *m_generalTabBtn = nullptr;
    QPushButton *m_appearanceTabBtn = nullptr;
    QPushButton *m_shortcutsTabBtn = nullptr;
    QPushButton *m_aboutTabBtn = nullptr;
    QScrollArea *m_scrollArea = nullptr;
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
    QNetworkAccessManager *m_nam = nullptr;
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
    QPushButton *m_checkUpdateBtn = nullptr;
};
