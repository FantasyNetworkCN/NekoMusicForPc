/**
 * @file qqimportdialog.cpp
 * @brief QQ 音乐歌单导入对话框实现
 */

#include "qqimportdialog.h"
#include "core/apiclient.h"
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
#include <QPainter>
#include <QRegularExpression>
#include <QGraphicsDropShadowEffect>

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

QqImportDialog::QqImportDialog(ApiClient *apiClient, QWidget *parent)
    : QDialog(parent)
    , m_apiClient(apiClient)
{
    polishFramelessDialog(this);
    setFixedSize(520, 480);
    setupUi();
}

void QqImportDialog::setupUi()
{
    const bool dark = Theme::ThemeManager::instance().isDarkMode();
    
    // 颜色定义
    const QString bgColor = dark ? QStringLiteral("rgba(36, 36, 36, 245)") : QStringLiteral("rgba(255, 255, 255, 248)");
    const QString borderColor = dark ? QString::fromUtf8(Theme::kBorderGlass) : QStringLiteral("rgba(111, 66, 193, 0.22)");
    const QString textMainColor = dark ? QString::fromUtf8(Theme::kTextMain) : QStringLiteral("#212529");
    const QString textSubColor = dark ? QString::fromUtf8(Theme::kTextSub) : QStringLiteral("rgba(33, 37, 41, 0.65)");
    const QString inputBgColor = dark ? QStringLiteral("rgba(255, 255, 255, 0.08)") : QStringLiteral("rgba(33, 37, 41, 0.05)");
    const QString inputFocusBg = dark ? QStringLiteral("rgba(255, 255, 255, 0.12)") : QStringLiteral("#ffffff");
    const QString btnSecondaryBg = dark ? QStringLiteral("rgba(255, 255, 255, 0.1)") : QStringLiteral("rgba(33, 37, 41, 0.06)");
    
    auto *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(20, 20, 20, 20);
    outerLayout->setSpacing(0);
    
    // 主容器（玻璃效果）
    auto *mainBox = new QWidget(this);
    mainBox->setObjectName(QStringLiteral("qqImportBox"));
    mainBox->setStyleSheet(
        QStringLiteral(
            "QWidget#qqImportBox {"
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
    auto *titleLabel = new QLabel(I18n::instance().tr(QStringLiteral("importQqPlaylist")), mainBox);
    titleLabel->setStyleSheet(QStringLiteral("font-size: 18px; font-weight: bold; color: %1; background: transparent;").arg(textMainColor));
    mainLayout->addWidget(titleLabel);

    // 说明文字
    auto *descLabel = new QLabel(I18n::instance().tr(QStringLiteral("importQqDesc")), mainBox);
    descLabel->setWordWrap(true);
    descLabel->setStyleSheet(QStringLiteral("color: %1; font-size: 13px; background: transparent;").arg(textSubColor));
    mainLayout->addWidget(descLabel);

    // 输入区域
    auto *inputLayout = new QHBoxLayout();
    inputLayout->setSpacing(8);
    
    m_inputEdit = new QLineEdit(mainBox);
    m_inputEdit->setPlaceholderText(I18n::instance().tr(QStringLiteral("inputQqLink")));
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
          dark ? QStringLiteral("rgba(255,255,255,0.2)") : QStringLiteral("rgba(230, 57, 80, 0.45)"),
          dark ? QStringLiteral("rgba(255,255,255,0.4)") : QStringLiteral("rgba(33, 37, 41, 0.45)")));
    connect(m_fetchBtn, &QPushButton::clicked, this, &QqImportDialog::onFetchPlaylist);
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
          dark ? QStringLiteral("rgba(230, 57, 80, 0.38)") : QStringLiteral("rgba(230, 57, 80, 0.22)")));
    m_targetPlaylistCombo->hide();
    connect(m_targetPlaylistCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        Q_UNUSED(index);
        const int targetId = m_targetPlaylistCombo->currentData().toInt();
        const bool isCreateNew = (targetId == kImportTargetNewPlaylist);
        m_newPlaylistEdit->setVisible(isCreateNew);
        if (isCreateNew && m_newPlaylistEdit->text().trimmed().isEmpty())
            m_newPlaylistEdit->setText(m_qqPlaylistName);
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
          dark ? QStringLiteral("rgba(255,255,255,0.15)") : QStringLiteral("rgba(230, 57, 80, 0.12)")));
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
          dark ? QStringLiteral("rgba(255,255,255,0.2)") : QStringLiteral("rgba(230, 57, 80, 0.45)"),
          dark ? QStringLiteral("rgba(255,255,255,0.4)") : QStringLiteral("rgba(33, 37, 41, 0.45)")));
    m_importBtn->setEnabled(false);
    connect(m_importBtn, &QPushButton::clicked, this, &QqImportDialog::onStartImport);
    btnLayout->addWidget(m_importBtn);
    
    mainLayout->addLayout(btnLayout);
    
    outerLayout->addWidget(mainBox);
}

QString QqImportDialog::parsePlaylistId(const QString &input)
{
    const QString trimmed = input.trimmed();
    if (trimmed.isEmpty())
        return QString();

    static QRegularExpression digitsOnly(QStringLiteral("^\\d+$"));
    if (digitsOnly.match(trimmed).hasMatch())
        return trimmed;

    static QRegularExpression urlIdParam(QStringLiteral("[?&]id=(\\d+)"), QRegularExpression::CaseInsensitiveOption);
    const auto match = urlIdParam.match(trimmed);
    if (match.hasMatch())
        return match.captured(1);

    return QString();
}

void QqImportDialog::onFetchPlaylist()
{
    const QString input = m_inputEdit->text();
    const QString disstid = parsePlaylistId(input);

    if (disstid.isEmpty()) {
        setError(I18n::instance().tr(QStringLiteral("invalidQqLink")));
        return;
    }

    setError(QString());
    m_fetchBtn->setEnabled(false);
    m_fetchBtn->setText(I18n::instance().tr(QStringLiteral("loading")));

    m_apiClient->fetchQqPlaylist(disstid, [this, disstid](bool success, const QString &message, const ApiClient::QqPlaylistInfo &playlist) {
        m_fetchBtn->setEnabled(true);
        m_fetchBtn->setText(I18n::instance().tr(QStringLiteral("fetchPlaylist")));

        if (!success) {
            setError(message.isEmpty() ? I18n::instance().tr(QStringLiteral("fetchPlaylistFailed")) : message);
            return;
        }

        if (playlist.tracks.isEmpty()) {
            setError(I18n::instance().tr(QStringLiteral("emptyQqPlaylist")));
            return;
        }

        m_qqDisstid = playlist.disstid.isEmpty() ? disstid : playlist.disstid;
        m_qqPlaylistName = playlist.name;
        m_qqTracks = playlist.tracks;

        const int displayCount = playlist.trackCount > 0 ? playlist.trackCount : playlist.tracks.size();
        m_playlistInfoLabel->setText(I18n::instance().tr(QStringLiteral("qqPlaylistInfo"))
                                         .arg(playlist.name.isEmpty() ? m_qqDisstid : playlist.name)
                                         .arg(displayCount));
        m_playlistInfoLabel->show();

        updatePlaylistCombo();
    });
}

void QqImportDialog::updatePlaylistCombo()
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

        m_targetPlaylistCombo->addItem(I18n::instance().tr(QStringLiteral("favorites")), kImportTargetFavorites);

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

void QqImportDialog::onStartImport()
{
    if (m_qqTracks.isEmpty()) {
        setError(I18n::instance().tr(QStringLiteral("noTracksToImport")));
        return;
    }
    
    int targetPlaylistId = m_targetPlaylistCombo->currentData().toInt();

    if (targetPlaylistId == kImportTargetNewPlaylist) {
        const QString newName = m_newPlaylistEdit->text().trimmed();
        if (newName.isEmpty()) {
            setError(I18n::instance().tr(QStringLiteral("inputNewPlaylistName")));
            return;
        }
    }
    
    // 禁用按钮
    m_fetchBtn->setEnabled(false);
    m_importBtn->setEnabled(false);
    m_targetPlaylistCombo->setEnabled(false);
    m_newPlaylistEdit->setEnabled(false);
    
    // 显示进度
    m_progressBar->show();
    m_progressBar->setValue(5);
    m_statusLabel->show();
    setError(QString());

    doImport(targetPlaylistId);
}

void QqImportDialog::restoreImportControls()
{
    m_fetchBtn->setEnabled(true);
    m_importBtn->setEnabled(true);
    m_targetPlaylistCombo->setEnabled(true);
    m_newPlaylistEdit->setEnabled(true);
    m_progressBar->hide();
    m_statusLabel->hide();
}

void QqImportDialog::doImport(int targetPlaylistId)
{
    m_statusLabel->setText(I18n::instance().tr(QStringLiteral("searchingTracks")));
    m_progressBar->setValue(15);

    QList<ApiClient::BatchSearchItem> searchItems;
    for (const auto &track : m_qqTracks) {
        ApiClient::BatchSearchItem item;
        item.title = track.name;
        item.artist = track.artist;
        searchItems.append(item);
    }

    m_apiClient->batchSearchMusic(searchItems, [this, targetPlaylistId](bool success, const ApiClient::BatchSearchResult &result) {
        if (!success) {
            setError(I18n::instance().tr(QStringLiteral("searchServiceBusy")));
            restoreImportControls();
            return;
        }

        m_progressBar->setValue(50);

        if (result.matchedMusicIds.isEmpty()) {
            setError(I18n::instance().tr(QStringLiteral("noMatchedTracks")));
            restoreImportControls();
            return;
        }

        addMatchedTracks(targetPlaylistId, result);
    });
}

void QqImportDialog::addMatchedTracks(int targetPlaylistId, const ApiClient::BatchSearchResult &searchResult)
{
    const bool importToFavorites = (targetPlaylistId == kImportTargetFavorites);

    auto finishCb = [this, searchResult, importToFavorites](bool addSuccess, const ApiClient::BatchAddResult &addResult) {
        finishImport(searchResult, addSuccess, addResult, importToFavorites);
    };

    auto beginAdd = [this, searchResult, finishCb](int resolvedPlaylistId) {
        m_statusLabel->setText(I18n::instance().tr(QStringLiteral("addingToPlaylist"))
            .arg(searchResult.matchedMusicIds.size()));
        m_progressBar->setValue(70);
        m_apiClient->batchAddMusicToPlaylist(resolvedPlaylistId, searchResult.matchedMusicIds, finishCb);
    };

    if (importToFavorites) {
        m_statusLabel->setText(I18n::instance().tr(QStringLiteral("addingToPlaylist"))
            .arg(searchResult.matchedMusicIds.size()));
        m_progressBar->setValue(70);
        m_apiClient->batchAddFavorites(searchResult.matchedMusicIds, finishCb);
        return;
    }

    if (targetPlaylistId == kImportTargetNewPlaylist) {
        const QString newName = m_newPlaylistEdit->text().trimmed();
        m_statusLabel->setText(I18n::instance().tr(QStringLiteral("creatingPlaylist")));
        m_progressBar->setValue(60);

        m_apiClient->createPlaylist(newName, QString(), [this, searchResult, beginAdd](bool success, const QString &msg, const QVariantMap &data) {
            if (!success) {
                setError(I18n::instance().tr(QStringLiteral("createPlaylistFailed")));
                restoreImportControls();
                return;
            }

            const int newPlaylistId = data.value(QStringLiteral("id")).toInt();
            if (newPlaylistId <= 0) {
                setError(I18n::instance().tr(QStringLiteral("createPlaylistFailed")));
                restoreImportControls();
                return;
            }

            beginAdd(newPlaylistId);
        });
        return;
    }

    beginAdd(targetPlaylistId);
}

void QqImportDialog::finishImport(const ApiClient::BatchSearchResult &searchResult,
                                       bool addSuccess,
                                       const ApiClient::BatchAddResult &addResult,
                                       bool importedToFavorites)
{
    m_progressBar->setValue(100);

    if (!addSuccess) {
        setError(I18n::instance().tr(QStringLiteral("addToPlaylistFailed")));
        restoreImportControls();
        return;
    }

    m_statusLabel->setText(I18n::instance().tr(QStringLiteral("importSuccess"))
        .arg(addResult.addedCount)
        .arg(m_qqTracks.size())
        .arg(searchResult.failCount));
    m_statusLabel->setStyleSheet(QStringLiteral("color: %1; font-size: 13px; background: transparent;")
        .arg(QString::fromUtf8(Theme::kMint)));

    m_closeBtn->setText(I18n::instance().tr(QStringLiteral("close")));
    m_importBtn->hide();
    m_fetchBtn->hide();

    emit importCompleted(addResult.addedCount, m_qqTracks.size(), searchResult.failCount, importedToFavorites);
}

void QqImportDialog::setError(const QString &error)
{
    if (error.isEmpty()) {
        m_errorLabel->hide();
    } else {
        m_errorLabel->setText(error);
        m_errorLabel->show();
    }
}

void QqImportDialog::setProgress(const QString &status)
{
    m_statusLabel->setText(status);
    m_statusLabel->show();
}

void QqImportDialog::setStep(int step)
{
    m_currentStep = step;
}
