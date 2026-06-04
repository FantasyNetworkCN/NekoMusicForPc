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
class QPushButton;
class QVBoxLayout;
class QWidget;
class ShortcutCaptureButton;

class SettingsPage : public QWidget
{
    Q_OBJECT

public:
    explicit SettingsPage(QWidget *parent = nullptr);

signals:
    void languageChanged(int language);
    void checkForUpdatesRequested();

protected:
    void paintEvent(QPaintEvent *) override;

private:
    void setupUi();
    void retranslate();
    void setupShortcutRow(QVBoxLayout *parentLayout, QWidget *cardBody, AppShortcuts::Action action,
                          QLabel **labelOut, ShortcutCaptureButton **captureOut, QPushButton **resetOut);
    void applyShortcutChange(AppShortcuts::Action action, const QKeySequence &seq,
                             ShortcutCaptureButton *editor);
    void refreshShortcutEditors();
    void setupPersonalizationSection(QVBoxLayout *cardLay, QWidget *cardBody);
    void updateBackdropOptionRows();
    void refreshBackdropPathLabel();
    void refreshBackdropColorSwatch();

    QComboBox *m_langCombo = nullptr;
    QComboBox *m_themeCombo = nullptr;
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
    QLabel *m_langLabel = nullptr;
    QLabel *m_shortcutsSectionLabel = nullptr;
    QLabel *m_shortcutPlayPauseLabel = nullptr;
    QLabel *m_shortcutPrevLabel = nullptr;
    QLabel *m_shortcutNextLabel = nullptr;
    QLabel *m_shortcutHintLabel = nullptr;
    QLabel *m_shortcutStatusLabel = nullptr;
    QPushButton *m_shortcutConfigureBtn = nullptr;
    ShortcutCaptureButton *m_shortcutPlayPauseBtn = nullptr;
    ShortcutCaptureButton *m_shortcutPrevBtn = nullptr;
    ShortcutCaptureButton *m_shortcutNextBtn = nullptr;
    QPushButton *m_shortcutResetAllBtn = nullptr;
    QPushButton *m_shortcutResetPlayPauseBtn = nullptr;
    QPushButton *m_shortcutResetPrevBtn = nullptr;
    QPushButton *m_shortcutResetNextBtn = nullptr;
    QLabel *m_aboutLabel = nullptr;
    QLabel *m_versionLabel = nullptr;
    QLabel *m_systemLabel = nullptr;
    QPushButton *m_githubBtn = nullptr;
    QPushButton *m_apiDocsBtn = nullptr;
    QPushButton *m_checkUpdateBtn = nullptr;
};
