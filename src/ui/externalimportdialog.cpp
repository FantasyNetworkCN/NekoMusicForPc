/**
 * @file externalimportdialog.cpp
 * @brief 外部歌单导入对话框公共基类实现
 */

#include "externalimportdialog.h"
#include "core/i18n.h"
#include "core/usermanager.h"
#include "theme/theme.h"
#include "theme/thememanager.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QProgressBar>
#include <QGraphicsDropShadowEffect>
#include <QNetworkReply>
#include <QPointer>

namespace {

void polishFramelessDialog(QDialog *dlg)
{
    dlg->setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint | Qt::WindowSystemMenuHint);
    dlg->setAttribute(Qt::WA_TranslucentBackground);
    dlg->setModal(true);

    auto *shadow = new QGraphicsDropShadowEffect(dlg);
    shadow->setBlurRadius(26);
    shadow->setOffset(0, 4);
    shadow->setColor(QColor(0, 0, 0, 100));
    dlg->setGraphicsEffect(shadow);
}

} // namespace

ExternalImportDialog::ExternalImportDialog(ApiClient *apiClient, const SourceConfig &config, QWidget *parent)
    : QDialog(parent)
    , m_apiClient(apiClient)
    , m_config(config)
{
    polishFramelessDialog(this);
    setFixedSize(520, 480);
    setupUi();
}

ExternalImportDialog::~ExternalImportDialog()
{
    if (m_pullReply) {
        m_pullReply->abort();
        m_pullReply = nullptr;
    }
}

void ExternalImportDialog::setupUi()
{
    const bool dark = Theme::ThemeManager::instance().isDarkMode();

    // 颜色定义
    const QString bgColor = dark ? QStringLiteral("rgba(36, 36, 36, 245)") : QStringLiteral("rgba(255, 255, 255, 248)");
    const QString borderColor = dark ? QString::fromUtf8(Theme::kBorderGlass) : QStringLiteral("rgba(64, 197, 241, 0.22)");
    const QString textMainColor = dark ? QString::fromUtf8(Theme::kTextMain) : QStringLiteral("#182B45");
    const QString textSubColor = dark ? QString::fromUtf8(Theme::kTextSub) : QStringLiteral("rgba(33, 37, 41, 0.65)");
    const QString inputBgColor = dark ? QStringLiteral("rgba(255, 255, 255, 0.08)") : QStringLiteral("rgba(33, 37, 41, 0.05)");
    const QString inputFocusBg = dark ? QStringLiteral("rgba(255, 255, 255, 0.12)") : QStringLiteral("#ffffff");
    const QString btnSecondaryBg = dark ? QStringLiteral("rgba(255, 255, 255, 0.1)") : QStringLiteral("rgba(33, 37, 41, 0.06)");

    auto *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(20, 20, 20, 20);
    outerLayout->setSpacing(0);

    // 主容器（玻璃效果）
    auto *mainBox = new QWidget(this);
    mainBox->setObjectName(QStringLiteral("externalImportBox"));
    mainBox->setStyleSheet(
        QStringLiteral(
            "QWidget#externalImportBox {"
            "  background: %1;"
            "  border: 1px solid %2;"
            "  border-radius: %3px;"
            "}")
            .arg(bgColor, borderColor)
            .arg(Theme::kRMd));

    auto *mainLayout = new QVBoxLayout(mainBox);
    mainLayout->setContentsMargins(24, 24, 24, 24);
    mainLayout->setSpacing(16);

    // 标题
    auto *titleLabel = new QLabel(I18n::instance().tr(m_config.titleKey), mainBox);
    titleLabel->setStyleSheet(QStringLiteral("font-size: 18px; font-weight: bold; color: %1; background: transparent;").arg(textMainColor));
    mainLayout->addWidget(titleLabel);

    // 说明文字
    auto *descLabel = new QLabel(I18n::instance().tr(m_config.descKey), mainBox);
    descLabel->setWordWrap(true);
    descLabel->setStyleSheet(QStringLiteral("color: %1; font-size: 13px; background: transparent;").arg(textSubColor));
    mainLayout->addWidget(descLabel);

    // 输入区域
    auto *inputLayout = new QHBoxLayout();
    inputLayout->setSpacing(8);

    m_inputEdit = new QLineEdit(mainBox);
    m_inputEdit->setPlaceholderText(I18n::instance().tr(m_config.placeholderKey));
    m_inputEdit->setStyleSheet(QStringLiteral(
        "QLineEdit { "
        "  background: %1; "
        "  border: 1px solid %2; "
        "  border-radius: 8px; "
        "  padding: 10px 14px; "
        "  color: %3; "
        "  font-size: 14px; "
        "}"
        "QLineEdit:focus { border-color: %4; background: %5; }"
    ).arg(inputBgColor, borderColor, textMainColor, QString::fromUtf8(Theme::kLavender), inputFocusBg));
    inputLayout->addWidget(m_inputEdit, 1);

    m_fetchBtn = new QPushButton(I18n::instance().tr(QStringLiteral("fetchPlaylist")), mainBox);
    m_fetchBtn->setCursor(Qt::PointingHandCursor);
    m_fetchBtn->setStyleSheet(QStringLiteral(
        "QPushButton { "
        "  background: %1; "
        "  color: white; "
        "  border: none; "
        "  border-radius: 8px; "
        "  padding: 10px 20px; "
        "  font-size: 14px; "
        "}"
        "QPushButton:hover { background: %2; }"
        "QPushButton:disabled { background: %3; color: %4; }"
    ).arg(QString::fromUtf8(Theme::kLavender), QString::fromUtf8(Theme::kLavenderLt),
          dark ? QStringLiteral("rgba(255,255,255,0.2)") : QStringLiteral("rgba(22, 119, 184, 0.45)"),
          dark ? QStringLiteral("rgba(255,255,255,0.4)") : QStringLiteral("rgba(33, 37, 41, 0.45)")));
    connect(m_fetchBtn, &QPushButton::clicked, this, &ExternalImportDialog::onFetchPlaylist);
    inputLayout->addWidget(m_fetchBtn);

    mainLayout->addLayout(inputLayout);

    // 歌单信息显示
    m_playlistInfoLabel = new QLabel(mainBox);
    m_playlistInfoLabel->setWordWrap(true);
    m_playlistInfoLabel->setStyleSheet(QStringLiteral("color: %1; font-size: 14px; padding: 8px 0; background: transparent;").arg(textMainColor));
    m_playlistInfoLabel->hide();
    mainLayout->addWidget(m_playlistInfoLabel);

    // 目标歌单选择
    auto *targetLayout = new QHBoxLayout();
    targetLayout->setSpacing(8);

    auto *targetLabel = new QLabel(I18n::instance().tr(QStringLiteral("selectTargetPlaylist")), mainBox);
    targetLabel->setStyleSheet(QStringLiteral("color: %1; font-size: 14px; background: transparent;").arg(textMainColor));
    targetLayout->addWidget(targetLabel);

    m_targetPlaylistCombo = new QComboBox(mainBox);
    m_targetPlaylistCombo->setStyleSheet(QStringLiteral(
        "QComboBox { "
        "  background: %1; "
        "  border: 1px solid %2; "
        "  border-radius: 8px; "
        "  padding: 8px 12px; "
        "  color: %3; "
        "  font-size: 14px; "
        "  min-width: 200px; "
        "}"
        "QComboBox::drop-down { border: none; width: 24px; }"
        "QComboBox QAbstractItemView { "
        "  background: %4; "
        "  border: 1px solid %2; "
        "  border-radius: 8px; "
        "  color: %3; "
        "  selection-background-color: %5; "
        "}"
    ).arg(inputBgColor, borderColor, textMainColor,
          dark ? QString::fromUtf8(Theme::kBgSurface) : QStringLiteral("#ffffff"),
          dark ? QStringLiteral("rgba(22, 119, 184, 0.38)") : QStringLiteral("rgba(22, 119, 184, 0.22)")));
    m_targetPlaylistCombo->hide();
    connect(m_targetPlaylistCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        Q_UNUSED(index);
        const int targetId = m_targetPlaylistCombo->currentData().toInt();
        const bool isCreateNew = (targetId == kImportTargetNewPlaylist);
        m_newPlaylistEdit->setVisible(isCreateNew);
        if (isCreateNew && m_newPlaylistEdit->text().trimmed().isEmpty())
            m_newPlaylistEdit->setText(m_playlistData.name);
    });
    targetLayout->addWidget(m_targetPlaylistCombo, 1);

    mainLayout->addLayout(targetLayout);

    // 新建歌单输入框
    m_newPlaylistEdit = new QLineEdit(mainBox);
    m_newPlaylistEdit->setPlaceholderText(I18n::instance().tr(QStringLiteral("inputNewPlaylistName")));
    m_newPlaylistEdit->setStyleSheet(QStringLiteral(
        "QLineEdit { "
        "  background: %1; "
        "  border: 1px solid %2; "
        "  border-radius: 8px; "
        "  padding: 10px 14px; "
        "  color: %3; "
        "  font-size: 14px; "
        "}"
        "QLineEdit:focus { border-color: %4; background: %5; }"
    ).arg(inputBgColor, borderColor, textMainColor, QString::fromUtf8(Theme::kLavender), inputFocusBg));
    m_newPlaylistEdit->hide();
    mainLayout->addWidget(m_newPlaylistEdit);

    // 进度条
    m_progressBar = new QProgressBar(mainBox);
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_progressBar->setTextVisible(false);
    m_progressBar->setStyleSheet(QStringLiteral(
        "QProgressBar { "
        "  background: %1; "
        "  border: none; "
        "  border-radius: 4px; "
        "  height: 8px; "
        "}"
        "QProgressBar::chunk { "
        "  background: %2; "
        "  border-radius: 4px; "
        "}"
    ).arg(dark ? QStringLiteral("rgba(255,255,255,0.1)") : QStringLiteral("rgba(33, 37, 41, 0.12)"),
          QString::fromUtf8(Theme::kLavender)));
    m_progressBar->hide();
    mainLayout->addWidget(m_progressBar);

    // 状态标签
    m_statusLabel = new QLabel(mainBox);
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setStyleSheet(QStringLiteral("color: %1; font-size: 13px; background: transparent;").arg(textSubColor));
    m_statusLabel->hide();
    mainLayout->addWidget(m_statusLabel);

    // 错误标签
    m_errorLabel = new QLabel(mainBox);
    m_errorLabel->setWordWrap(true);
    m_errorLabel->setStyleSheet(QStringLiteral("color: #FF6B6B; font-size: 13px; background: transparent;"));
    m_errorLabel->hide();
    mainLayout->addWidget(m_errorLabel);

    mainLayout->addStretch();

    // 底部按钮
    auto *btnLayout = new QHBoxLayout();
    btnLayout->setSpacing(12);

    m_closeBtn = new QPushButton(I18n::instance().tr(QStringLiteral("close")), mainBox);
    m_closeBtn->setCursor(Qt::PointingHandCursor);
    m_closeBtn->setStyleSheet(QStringLiteral(
        "QPushButton { "
        "  background: %1; "
        "  color: %2; "
        "  border: 1px solid %3; "
        "  border-radius: 8px; "
        "  padding: 10px 24px; "
        "  font-size: 14px; "
        "}"
        "QPushButton:hover { background: %4; }"
    ).arg(btnSecondaryBg, textMainColor, borderColor,
          dark ? QStringLiteral("rgba(255,255,255,0.15)") : QStringLiteral("rgba(22, 119, 184, 0.12)")));
    connect(m_closeBtn, &QPushButton::clicked, this, &QDialog::reject);
    btnLayout->addWidget(m_closeBtn);

    btnLayout->addStretch();

    m_importBtn = new QPushButton(I18n::instance().tr(QStringLiteral("startImport")), mainBox);
    m_importBtn->setCursor(Qt::PointingHandCursor);
    m_importBtn->setStyleSheet(QStringLiteral(
        "QPushButton { "
        "  background: %1; "
        "  color: white; "
        "  border: none; "
        "  border-radius: 8px; "
        "  padding: 10px 24px; "
        "  font-size: 14px; "
        "}"
        "QPushButton:hover { background: %2; }"
        "QPushButton:disabled { background: %3; color: %4; }"
    ).arg(QString::fromUtf8(Theme::kLavender), QString::fromUtf8(Theme::kLavenderLt),
          dark ? QStringLiteral("rgba(255,255,255,0.2)") : QStringLiteral("rgba(22, 119, 184, 0.45)"),
          dark ? QStringLiteral("rgba(255,255,255,0.4)") : QStringLiteral("rgba(33, 37, 41, 0.45)")));
    m_importBtn->setEnabled(false);
    connect(m_importBtn, &QPushButton::clicked, this, &ExternalImportDialog::onStartImport);
    btnLayout->addWidget(m_importBtn);

    mainLayout->addLayout(btnLayout);

    outerLayout->addWidget(mainBox);
}

void ExternalImportDialog::onFetchPlaylist()
{
    const QString input = m_inputEdit->text();
    const QString id = parseInput(input);
    if (id.isEmpty()) {
        setError(I18n::instance().tr(m_config.invalidKey));
        return;
    }

    setError(QString());
    m_fetchBtn->setEnabled(false);
    m_fetchBtn->setText(I18n::instance().tr(QStringLiteral("loading")));

    fetchPlaylist(id, [this, id](bool success, const QString &message, const PlaylistData &data) {
        m_fetchBtn->setEnabled(true);
        m_fetchBtn->setText(I18n::instance().tr(QStringLiteral("fetchPlaylist")));

        if (!success) {
            setError(message.isEmpty() ? I18n::instance().tr(QStringLiteral("fetchPlaylistFailed")) : message);
            return;
        }
        if (data.tracks.isEmpty()) {
            setError(I18n::instance().tr(m_config.emptyKey));
            return;
        }

        m_playlistData = data;
        if (m_playlistData.id.isEmpty())
            m_playlistData.id = id;

        const int displayCount = data.trackCount > 0 ? data.trackCount : data.tracks.size();
        m_playlistInfoLabel->setText(I18n::instance().tr(m_config.playlistInfoKey)
                                         .arg(data.name.isEmpty() ? m_playlistData.id : data.name)
                                         .arg(displayCount));
        m_playlistInfoLabel->show();

        updatePlaylistCombo();
    });
}

void ExternalImportDialog::updatePlaylistCombo()
{
    if (!UserManager::instance().isLoggedIn()) {
        setError(I18n::instance().tr(QStringLiteral("pleaseLoginFirst")));
        return;
    }

    m_apiClient->fetchUserPlaylists([this](bool success, const QList<QVariantMap> &playlists) {
        if (!success) {
            setError(I18n::instance().tr(QStringLiteral("loadPlaylistsFailed")));
            return;
        }

        m_userPlaylists.clear();
        m_targetPlaylistCombo->clear();

        for (const auto &pl : playlists) {
            int id = pl.value(QStringLiteral("id")).toInt();
            QString name = pl.value(QStringLiteral("name")).toString();
            m_userPlaylists.append(qMakePair(id, name));
            m_targetPlaylistCombo->addItem(name, id);
        }

        m_targetPlaylistCombo->addItem(I18n::instance().tr(QStringLiteral("createNewPlaylist")), kImportTargetNewPlaylist);

        m_targetPlaylistCombo->setCurrentIndex(0);
        m_newPlaylistEdit->hide();

        m_targetPlaylistCombo->show();
        m_importBtn->setEnabled(true);
    });
}

void ExternalImportDialog::onStartImport()
{
    if (m_playlistData.tracks.isEmpty()) {
        setError(I18n::instance().tr(QStringLiteral("noTracksToImport")));
        return;
    }

    const int targetPlaylistId = m_targetPlaylistCombo->currentData().toInt();
    QString newPlaylistName;
    if (targetPlaylistId == kImportTargetNewPlaylist) {
        newPlaylistName = m_newPlaylistEdit->text().trimmed();
        if (newPlaylistName.isEmpty()) {
            setError(I18n::instance().tr(QStringLiteral("inputNewPlaylistName")));
            return;
        }
    }

    if (!UserManager::instance().isLoggedIn()) {
        setError(I18n::instance().tr(QStringLiteral("pleaseLoginFirst")));
        return;
    }

    // 禁用按钮
    m_fetchBtn->setEnabled(false);
    m_importBtn->setEnabled(false);
    m_targetPlaylistCombo->setEnabled(false);
    m_newPlaylistEdit->setEnabled(false);

    // 显示进度
    m_progressBar->show();
    m_progressBar->setRange(0, 0);
    m_statusLabel->show();
    setError(QString());

    startPull(targetPlaylistId, newPlaylistName);
}

void ExternalImportDialog::startPull(int targetPlaylistId, const QString &newPlaylistName)
{
    QPointer<ExternalImportDialog> guard(this);

    ApiClient::ExternalPullCallbacks callbacks;
    callbacks.onStart = [guard](const ApiClient::ExternalPullStart &start) {
        if (!guard)
            return;
        guard->m_totalTracks = start.total > 0 ? start.total : guard->m_playlistData.tracks.size();
        guard->m_finishedTracks = 0;
        guard->m_progressBar->setRange(0, qMax(1, guard->m_totalTracks));
        guard->m_progressBar->setValue(0);
        guard->setProgress(I18n::instance().tr(QStringLiteral("searchingTracks")));
    };
    callbacks.onTrack = [guard](const ApiClient::ExternalPullTrack &track) {
        if (guard)
            guard->onPullTrack(track);
    };
    callbacks.onProgress = [guard](const ApiClient::ExternalPullProgress &progress) {
        if (guard)
            guard->onPullProgress(progress);
    };
    callbacks.onDone = [guard](const ApiClient::ExternalPullSummary &summary) {
        if (guard)
            guard->finishImport(summary);
    };
    callbacks.onError = [guard](const QString &message) {
        if (!guard)
            return;
        guard->setError(message.isEmpty()
                            ? I18n::instance().tr(QStringLiteral("addToPlaylistFailed"))
                            : message);
        guard->restoreImportControls();
    };

    m_pullReply = m_apiClient->pullExternalPlaylist(
        m_config.source, m_playlistData.id, targetPlaylistId, newPlaylistName, callbacks);
}

void ExternalImportDialog::onPullTrack(const ApiClient::ExternalPullTrack &track)
{
    if (track.status == QLatin1String("downloading") || track.status == QLatin1String("matching")) {
        if (!track.title.isEmpty())
            setProgress(I18n::instance().tr(QStringLiteral("searchingTracks")));
        return;
    }

    m_finishedTracks++;
    if (m_totalTracks > 0)
        m_progressBar->setValue(qMin(m_finishedTracks, m_totalTracks));
    setProgress(I18n::instance().tr(QStringLiteral("addingToPlaylist")).arg(m_finishedTracks));
}

void ExternalImportDialog::onPullProgress(const ApiClient::ExternalPullProgress &progress)
{
    if (progress.percent < 0)
        return;
    setProgress(I18n::instance().tr(QStringLiteral("addingToPlaylist")).arg(m_finishedTracks + 1)
                + QStringLiteral(" %1%").arg(progress.percent));
}

void ExternalImportDialog::restoreImportControls()
{
    m_pullReply = nullptr;
    m_fetchBtn->setEnabled(true);
    m_importBtn->setEnabled(true);
    m_targetPlaylistCombo->setEnabled(true);
    m_newPlaylistEdit->setEnabled(true);
    m_progressBar->hide();
    m_statusLabel->hide();
}

void ExternalImportDialog::finishImport(const ApiClient::ExternalPullSummary &summary)
{
    m_pullReply = nullptr;
    const int total = summary.total > 0 ? summary.total : m_totalTracks;
    const int added = summary.imported + summary.existed;

    m_progressBar->setRange(0, qMax(1, total));
    m_progressBar->setValue(total);
    m_statusLabel->setText(I18n::instance().tr(QStringLiteral("importSuccess"))
                               .arg(added)
                               .arg(total)
                               .arg(summary.failed));
    m_statusLabel->setStyleSheet(QStringLiteral("color: %1; font-size: 13px; background: transparent;")
                                     .arg(QString::fromUtf8(Theme::kMint)));

    m_closeBtn->setText(I18n::instance().tr(QStringLiteral("close")));
    m_importBtn->hide();
    m_fetchBtn->hide();

    emit importCompleted(added, total, summary.failed, false);
}

void ExternalImportDialog::setError(const QString &error)
{
    if (error.isEmpty()) {
        m_errorLabel->hide();
    } else {
        m_errorLabel->setText(error);
        m_errorLabel->show();
    }
}

void ExternalImportDialog::setProgress(const QString &status)
{
    m_statusLabel->setText(status);
    m_statusLabel->show();
}
