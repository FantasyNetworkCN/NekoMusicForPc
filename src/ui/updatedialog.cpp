/**
 * @file updatedialog.cpp
 * @brief 版本更新通知对话框实现
 */

#include "updatedialog.h"
#include "core/i18n.h"
#include "core/linuxinstallbackend.h"
#include "theme/theme.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QProgressBar>
#include <QTextBrowser>
#include <QPainter>
#include <QPainterPath>
#include <QDesktopServices>
#include <QUrl>
#include <QProcess>
#include <QFile>
#include <QTextStream>
#include <QStandardPaths>
#include <QDir>

UpdateDialog::UpdateDialog(const QString &currentVersion, const QString &newVersion,
                           const QString &downloadUrl, UpdateInstallKind installKind,
                           QWidget *parent)
    : QDialog(parent), m_currentVersion(currentVersion), m_newVersion(newVersion),
      m_downloadUrl(downloadUrl), m_installKind(installKind)
{
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint | Qt::WindowSystemMenuHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setModal(true);
    setFixedSize(540, 540);

    setupUi();
}

void UpdateDialog::setupUi()
{
    auto *mainLay = new QVBoxLayout(this);
    mainLay->setContentsMargins(0, 0, 0, 0);
    mainLay->setSpacing(0);

    // 背景容器
    auto *container = new QWidget(this);
    container->setObjectName("updateDialogBg");
    container->setStyleSheet(
        "QWidget#updateDialogBg { "
        "  background: rgba(30, 30, 50, 0.98); "
        "  border: 1px solid rgba(255, 255, 255, 0.1); "
        "  border-radius: 16px; "
        "}");

    auto *containerLay = new QVBoxLayout(container);
    containerLay->setContentsMargins(32, 32, 32, 24);
    containerLay->setSpacing(20);

    // 标题
    m_titleLbl = new QLabel(I18n::instance().tr("newVersionAvailable"), container);
    m_titleLbl->setObjectName("updateTitle");
    m_titleLbl->setStyleSheet(
        "QLabel#updateTitle { "
        "  font-size: 20px; font-weight: 700; color: white; "
        "}");
    m_titleLbl->setAlignment(Qt::AlignCenter);
    containerLay->addWidget(m_titleLbl);

    // 版本信息
    auto *infoWidget = new QWidget(container);
    auto *infoLay = new QVBoxLayout(infoWidget);
    infoLay->setContentsMargins(16, 0, 16, 0);
    infoLay->setSpacing(12);

    m_currentVerLbl = new QLabel(
        QString("%1: %2").arg(I18n::instance().tr("currentVersion"), m_currentVersion),
        infoWidget);
    m_currentVerLbl->setObjectName("currentVer");
    m_currentVerLbl->setStyleSheet(
        "QLabel#currentVer { font-size: 14px; color: rgba(255, 255, 255, 0.6); }");
    infoLay->addWidget(m_currentVerLbl);

    m_newVerLbl = new QLabel(
        QString("%1: %2").arg(I18n::instance().tr("newVersion"), m_newVersion),
        infoWidget);
    m_newVerLbl->setObjectName("newVer");
    m_newVerLbl->setStyleSheet(
        "QLabel#newVer { font-size: 16px; font-weight: 600; color: " + QString(Theme::kLavender) + "; }");
    infoLay->addWidget(m_newVerLbl);

    containerLay->addWidget(infoWidget);

    m_changelogTitle = new QLabel(I18n::instance().tr("changelog"), container);
    m_changelogTitle->setObjectName("updateChangelogTitle");
    m_changelogTitle->setStyleSheet(
        "QLabel#updateChangelogTitle { font-size: 14px; font-weight: 600; color: rgba(255,255,255,0.92); }");
    containerLay->addWidget(m_changelogTitle);

    m_changelogBrowser = new QTextBrowser(container);
    m_changelogBrowser->setObjectName("updateChangelog");
    m_changelogBrowser->setOpenExternalLinks(true);
    m_changelogBrowser->setMinimumHeight(210);
    m_changelogBrowser->setStyleSheet(
        "QTextBrowser#updateChangelog {"
        "  background: rgba(255, 255, 255, 0.06);"
        "  border: 1px solid rgba(255, 255, 255, 0.10);"
        "  border-radius: 10px;"
        "  color: rgba(255, 255, 255, 0.88);"
        "  padding: 10px;"
        "  selection-background-color: #667eea;"
        "}");
    m_changelogBrowser->setPlainText(I18n::instance().tr("changelogLoading"));
    containerLay->addWidget(m_changelogBrowser, 1);

    // 进度条（初始隐藏）
    m_progressBar = new QProgressBar(container);
    m_progressBar->setObjectName("updateProgress");
    m_progressBar->setFixedHeight(8);
    m_progressBar->setTextVisible(false);
    m_progressBar->setStyleSheet(
        "QProgressBar#updateProgress { "
        "  background: rgba(255, 255, 255, 0.1); "
        "  border-radius: 4px; "
        "}"
        "QProgressBar::chunk#updateProgress { "
        "  background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #667eea, stop:1 #764ba2); "
        "  border-radius: 4px; "
        "}");
    m_progressBar->hide();
    containerLay->addWidget(m_progressBar);

    // 状态标签（初始隐藏）
    m_statusLbl = new QLabel(container);
    m_statusLbl->setObjectName("updateStatus");
    m_statusLbl->setAlignment(Qt::AlignCenter);
    m_statusLbl->setStyleSheet(
        "QLabel#updateStatus { font-size: 13px; color: rgba(255, 255, 255, 0.6); }");
    m_statusLbl->hide();
    containerLay->addWidget(m_statusLbl);

    containerLay->addStretch();

    // 按钮
    auto *btnLay = new QHBoxLayout();
    btnLay->setSpacing(12);

    m_remindLaterBtn = new QPushButton(I18n::instance().tr("remindLater"), container);
    m_remindLaterBtn->setFixedHeight(40);
    m_remindLaterBtn->setCursor(Qt::PointingHandCursor);
    m_remindLaterBtn->setStyleSheet(
        "QPushButton { "
        "  background: rgba(255, 255, 255, 0.1); "
        "  color: rgba(255, 255, 255, 0.9); "
        "  border: 1px solid rgba(255, 255, 255, 0.2); "
        "  border-radius: 8px; "
        "  font-size: 14px; "
        "}"
        "QPushButton:hover { background: rgba(255, 255, 255, 0.15); }");
    connect(m_remindLaterBtn, &QPushButton::clicked, this, [this]()
            {
        emit remindLater();
        accept(); });
    btnLay->addWidget(m_remindLaterBtn, 1);

    m_updateBtn = new QPushButton(I18n::instance().tr("updateNow"), container);
    m_updateBtn->setFixedHeight(40);
    m_updateBtn->setCursor(Qt::PointingHandCursor);
    m_updateBtn->setStyleSheet(
        "QPushButton { "
        "  background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #667eea, stop:1 #764ba2); "
        "  color: white; "
        "  border: none; "
        "  border-radius: 8px; "
        "  font-size: 14px; font-weight: 600; "
        "}"
        "QPushButton:hover { opacity: 0.9; }");
    connect(m_updateBtn, &QPushButton::clicked, this, [this]() {
        if (m_installKind == UpdateInstallKind::ArchAurHelper) {
            launchAurUpdateInTerminal();
            accept();
            return;
        }
        if (m_installKind == UpdateInstallKind::OpenWebPage) {
            QDesktopServices::openUrl(QUrl(m_downloadUrl));
            accept();
            return;
        }
        emit downloadRequested(m_downloadUrl);
        setupDownloadingUi();
    });
    btnLay->addWidget(m_updateBtn, 1);

    if (m_installKind == UpdateInstallKind::ArchAurHelper) {
        const QString cmd = aurUpdateCommand();
        if (cmd.isEmpty()) {
            m_statusLbl->setText(I18n::instance().tr("updateViaAurNoHelper"));
            m_updateBtn->setText(I18n::instance().tr("openAurPage"));
        } else {
            m_statusLbl->setText(I18n::instance().tr("updateViaAurHint").arg(cmd));
        }
        m_statusLbl->setWordWrap(true);
        m_statusLbl->show();
    } else if (m_installKind == UpdateInstallKind::OpenWebPage) {
        m_statusLbl->setText(I18n::instance().tr("updateOpenWebHint"));
        m_statusLbl->setWordWrap(true);
        m_statusLbl->show();
    }

    containerLay->addLayout(btnLay);

    mainLay->addWidget(container);
}

void UpdateDialog::setupDownloadingUi()
{
    m_updateBtn->setEnabled(false);
    m_updateBtn->setText(I18n::instance().tr("downloading"));
    m_progressBar->show();
    m_progressBar->setValue(0);
    m_statusLbl->show();
    m_statusLbl->setText("0%");
}

void UpdateDialog::setupFinishedUi(const QString &filePath)
{
    m_progressBar->setValue(100);
    m_statusLbl->setText(I18n::instance().tr("downloadComplete"));
    m_updateBtn->setEnabled(true);
    m_updateBtn->setText(I18n::instance().tr("installNow"));
    m_updateBtn->disconnect();
    connect(m_updateBtn, &QPushButton::clicked, this, [this, filePath]()
            {
#if defined(Q_OS_WIN)
                // Windows: 直接打开 exe 安装程序
                QProcess::startDetached(filePath);
                accept();
#elif defined(Q_OS_MACOS)
                // Mac: 打开 dmg文件
                // 鬼知道傻逼Mac怎么安装，反正是冤大头设备
                QProcess::startDetached("open", {filePath});
                accept();
#elif defined(Q_OS_LINUX)
                if (detectLinuxInstallBackend() != LinuxInstallBackend::Debian) {
                    QDesktopServices::openUrl(QUrl::fromLocalFile(filePath));
                    accept();
                    return;
                }
                // Debian 系：pkexec dpkg -i
                // 1. pkexec dpkg -i 安装包（会弹出系统密码框）
                // 2. 杀死旧进程
                // 3. sleep 1秒
                // 4. 重新启动程序
                QString scriptPath = QDir::tempPath() + "/nekomusic_update.sh";
                QFile scriptFile(scriptPath);
                if (scriptFile.open(QIODevice::WriteOnly | QIODevice::Text))
                {
                    QTextStream out(&scriptFile);
                    out << "#!/bin/bash\n";
                    out << "# 更新安装脚本\n\n";
                    out << "# 安装 deb 包（pkexec 会弹出图形密码认证）\n";
                    out << "pkexec env DISPLAY=$DISPLAY XAUTHORITY=$XAUTHORITY dpkg -i \"" << filePath << "\"\n\n";
                    out << "# 等待安装完成\n";
                    out << "sleep 1\n\n";
                    out << "# 用 pgrep 找到进程PID，然后 kill\n";
                    out << "OLD_PID=$(pgrep -f 'NekoMusic|nekomusic' | head -1)\n";
                    out << "if [ -n \"$OLD_PID\" ]; then\n";
                    out << "    kill $OLD_PID\n";
                    out << "    sleep 1\n";
                    out << "    # 如果还没死，强制 kill\n";
                    out << "    if kill -0 $OLD_PID 2>/dev/null; then\n";
                    out << "        kill -9 $OLD_PID\n";
                    out << "        sleep 1\n";
                    out << "    fi\n";
                    out << "fi\n\n";
                    out << "# 重新启动程序（尝试多个可能的路径）\n";
                    out << "if [ -f \"/opt/Neko歌姬计划/nekomusic\" ]; then\n";
                    out << "    nohup /opt/Neko歌姬计划/nekomusic >/dev/null 2>&1 &\n";
                    out << "elif command -v nekomusic >/dev/null 2>&1; then\n";
                    out << "    nohup nekomusic >/dev/null 2>&1 &\n";
                    out << "elif command -v NekoMusic >/dev/null 2>&1; then\n";
                    out << "    nohup NekoMusic >/dev/null 2>&1 &\n";
                    out << "fi\n\n";
                    out << "# 清理脚本自身\n";
                    out << "rm -f \"" << scriptPath << "\"\n";
                    scriptFile.close();

                    // 设置脚本可执行
                    scriptFile.setPermissions(QFileDevice::ExeOwner | QFileDevice::ReadOwner);

                    // 后台执行脚本
                    QProcess::startDetached("bash", {scriptPath});
                }
                accept();
#else
                QDesktopServices::openUrl(QUrl::fromLocalFile(filePath));
                accept();
#endif
            });
    m_remindLaterBtn->setText(I18n::instance().tr("installLater"));
}

void UpdateDialog::setupFailedUi(const QString &error)
{
    m_progressBar->hide();
    m_statusLbl->show();
    m_statusLbl->setText(QString("%1: %2").arg(I18n::instance().tr("downloadFailed"), error));
    m_statusLbl->setStyleSheet(
        "QLabel#updateStatus { font-size: 13px; color: #ef4444; }");
    m_updateBtn->setText(I18n::instance().tr("retry"));
    m_updateBtn->setEnabled(true);
    m_updateBtn->disconnect();
    connect(m_updateBtn, &QPushButton::clicked, this, [this]()
            {
        emit downloadRequested(m_downloadUrl);
        setupDownloadingUi();
        m_statusLbl->setStyleSheet(
            "QLabel#updateStatus { font-size: 13px; color: rgba(255, 255, 255, 0.6); }"
        ); });
}

void UpdateDialog::showDownloadProgress(qint64 bytesReceived, qint64 bytesTotal)
{
    if (bytesTotal > 0)
    {
        int percent = static_cast<int>(bytesReceived * 100 / bytesTotal);
        m_progressBar->setValue(percent);

        // 格式化进度文本
        QString receivedStr;
        if (bytesReceived < 1024 * 1024)
        {
            receivedStr = QString("%1 KiB").arg(bytesReceived / 1024);
        }
        else
        {
            receivedStr = QString("%1 MiB").arg(bytesReceived / (1024.0 * 1024.0), 0, 'f', 1);
        }
        QString totalStr;
        if (bytesTotal < 1024 * 1024)
        {
            totalStr = QString("%1 KiB").arg(bytesTotal / 1024);
        }
        else
        {
            totalStr = QString("%1 MiB").arg(bytesTotal / (1024.0 * 1024.0), 0, 'f', 1);
        }
        m_statusLbl->setText(QString("%1% - %2 / %3").arg(percent).arg(receivedStr).arg(totalStr));
    }
}

void UpdateDialog::showDownloadFinished(const QString &filePath)
{
    setupFinishedUi(filePath);
}

void UpdateDialog::showDownloadFailed(const QString &error)
{
    setupFailedUi(error);
}

void UpdateDialog::setChangelog(const QString &version, const QString &markdown)
{
    if (version != m_newVersion || !m_changelogBrowser)
        return;
    m_changelogBrowser->setMarkdown(markdown);
}

void UpdateDialog::setChangelogFailed(const QString &version)
{
    if (version != m_newVersion || !m_changelogBrowser)
        return;
    m_changelogBrowser->setPlainText(I18n::instance().tr("changelogLoadFailed"));
}

void UpdateDialog::paintEvent(QPaintEvent *event)
{
    QWidget::paintEvent(event);
    // 透明背景，由子控件处理
}
