#include "qishuiimportdialog.h"

#include <QRegularExpression>

QishuiImportDialog::QishuiImportDialog(ApiClient *apiClient, QWidget *parent)
    : ExternalImportDialog(apiClient,
                           SourceConfig{QStringLiteral("qishui"),
                                        QStringLiteral("importQishuiPlaylist"),
                                        QStringLiteral("importQishuiDesc"),
                                        QStringLiteral("inputQishuiLink"),
                                        QStringLiteral("invalidQishuiLink"),
                                        QStringLiteral("emptyQishuiPlaylist"),
                                        QStringLiteral("qishuiPlaylistInfo")},
                           parent)
{
}

QString QishuiImportDialog::parseInput(const QString &input) const
{
    const QString trimmed = input.trimmed();
    if (trimmed.isEmpty() || trimmed.size() > 2048)
        return QString();

    static QRegularExpression digitsOnly(QStringLiteral("^\\d+$"));
    if (digitsOnly.match(trimmed).hasMatch())
        return trimmed;

    static QRegularExpression playlistId(
        QStringLiteral("[?&]playlist_id=(\\d+)"), QRegularExpression::CaseInsensitiveOption);
    auto match = playlistId.match(trimmed);
    if (match.hasMatch())
        return match.captured(1);

    static QRegularExpression playlistPath(
        QStringLiteral("/playlist/(\\d+)"), QRegularExpression::CaseInsensitiveOption);
    match = playlistPath.match(trimmed);
    if (match.hasMatch())
        return match.captured(1);

    static QRegularExpression qishuiUrl(
        QStringLiteral("(?:qishui\\.com|qishui\\.douyin\\.com|music\\.douyin\\.com)"),
        QRegularExpression::CaseInsensitiveOption);
    if (qishuiUrl.match(trimmed).hasMatch())
        return trimmed;

    return QString();
}

void QishuiImportDialog::fetchPlaylist(const QString &id, FetchCallback cb)
{
    apiClient()->fetchQishuiPlaylist(
        id,
        [cb](bool success, const QString &message, const ApiClient::QishuiPlaylistInfo &playlist) {
            PlaylistData data;
            data.id = playlist.playlistId;
            data.name = playlist.name;
            data.trackCount = playlist.trackCount;
            data.tracks = playlist.tracks;
            if (cb) cb(success, message, data);
        });
}
