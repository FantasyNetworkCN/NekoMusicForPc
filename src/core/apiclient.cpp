/**
 * @file apiclient.cpp
 * @brief API 客户端实现
 */

#include "apiclient.h"
#include "httpprotocollabel.h"
#include "theme/theme.h"
#include "core/usermanager.h"

#include <QNetworkReply>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrlQuery>
#include <QDir>
#include <QStandardPaths>
#include <QSaveFile>
#include <QMutex>
#include <QMutexLocker>
#include <QSet>
#include <functional>
#include <memory>

ApiClient::ApiClient(QObject *parent) : QObject(parent) {}

namespace {

/**
 * 解析 SSE 增量字节流，把每个完整帧（event/data）派发给 onEvent。
 * @param buffer    未消费的原始字节，会被就地裁剪
 * @param eventName 跨块累积的事件名
 * @param payload   跨块累积的数据体
 */
void consumeSseChunk(QByteArray &buffer, QString &eventName, QByteArray &payload,
                     const std::function<void(const QString &, const QByteArray &)> &onEvent)
{
    int newline;
    while ((newline = buffer.indexOf('\n')) >= 0) {
        QByteArray line = buffer.left(newline);
        buffer.remove(0, newline + 1);
        if (line.endsWith('\r'))
            line.chop(1);

        if (line.isEmpty()) {
            if (payload.isEmpty() && eventName.isEmpty())
                continue;
            onEvent(eventName.isEmpty() ? QStringLiteral("message") : eventName, payload);
            eventName.clear();
            payload.clear();
        } else if (line.startsWith(':')) {
            // 心跳/注释帧，忽略
        } else if (line.startsWith("event:")) {
            eventName = QString::fromUtf8(line.mid(6)).trimmed();
        } else if (line.startsWith("data:")) {
            if (!payload.isEmpty())
                payload.append('\n');
            payload.append(line.mid(5).trimmed());
        }
    }
}

} // namespace

// 现有函数
void ApiClient::fetchRanking(MusicListCb cb) {
    QUrl url(QString::fromUtf8("%1/api/music/ranking")
                 .arg(QString::fromUtf8(Theme::kApiBase)));
    auto *reply = m_nam.get(QNetworkRequest(url));
    connect(reply, &QNetworkReply::finished, this, [reply, cb]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) { cb(false, {}); return; }
        auto doc = QJsonDocument::fromJson(reply->readAll());
        bool ok = doc.object().value("success").toBool();
        QList<QVariantMap> res;
        if (ok) for (const auto &v : doc.object().value("data").toArray())
            res.append(v.toObject().toVariantMap());
        cb(ok, res);
    });
}

void ApiClient::fetchLatest(int limit, MusicListCb cb) {
    QUrl url(QString::fromUtf8("%1/api/music/latest?limit=%2")
                 .arg(QString::fromUtf8(Theme::kApiBase))
                 .arg(limit));
    auto *reply = m_nam.get(QNetworkRequest(url));
    connect(reply, &QNetworkReply::finished, this, [reply, cb]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) { cb(false, {}); return; }
        auto doc = QJsonDocument::fromJson(reply->readAll());
        bool ok = doc.object().value("success").toBool();
        QList<QVariantMap> res;
        if (ok) for (const auto &v : doc.object().value("data").toArray())
            res.append(v.toObject().toVariantMap());
        cb(ok, res);
    });
}

void ApiClient::fetchDailyRecommendations(MusicListCb cb) {
    if (!UserManager::instance().isLoggedIn()) {
        cb(false, {});
        return;
    }
    QUrl url(QString::fromUtf8("%1/api/user/recommendations/daily")
                 .arg(QString::fromUtf8(Theme::kApiBase)));
    QNetworkRequest req(url);
    req.setRawHeader("Authorization", UserManager::instance().token().toUtf8());
    auto *reply = m_nam.get(req);
    connect(reply, &QNetworkReply::finished, this, [reply, cb]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) { cb(false, {}); return; }
        const auto doc = QJsonDocument::fromJson(reply->readAll());
        const auto root = doc.object();
        bool ok = root.value("success").toBool();
        QList<QVariantMap> res;
        if (ok) {
            QJsonArray arr = root.value("data").toArray();
            if (arr.isEmpty())
                arr = root.value("results").toArray();
            if (arr.isEmpty())
                arr = root.value("recommendations").toArray();

            for (const auto &v : arr) {
                QJsonObject obj = v.toObject();
                // 兼容 { music: {...} } 结构
                if (obj.contains("music") && obj.value("music").isObject())
                    obj = obj.value("music").toObject();
                res.append(obj.toVariantMap());
            }
        }
        cb(ok, res);
    });
}

void ApiClient::fetchFavorites(MusicListCb cb) {
    QUrl url(QString::fromUtf8("%1/api/user/favorites").arg(Theme::kApiBase));
    QNetworkRequest req(url);
    if (UserManager::instance().isLoggedIn()) {
        req.setRawHeader("Authorization", UserManager::instance().token().toUtf8());
    }
    auto *reply = m_nam.get(req);
    connect(reply, &QNetworkReply::finished, this, [reply, cb]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) { cb(false, {}); return; }
        auto doc = QJsonDocument::fromJson(reply->readAll());
        bool ok = doc.object().value("success").toBool();
        QList<QVariantMap> res;
        if (ok) for (const auto &v : doc.object().value("favorites").toArray())
            res.append(v.toObject().toVariantMap());
        cb(ok, res);
    });
}

void ApiClient::login(const QString &email, const QString &password, AuthCb cb) {
    QUrl url(QString::fromUtf8("%1/api/user/login").arg(Theme::kApiBase));
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QJsonObject body;
    body["email"] = email;
    body["password"] = password;
    auto *reply = m_nam.post(req, QJsonDocument(body).toJson());
    connect(reply, &QNetworkReply::finished, this, [reply, cb]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            cb(false, reply->errorString(), {}, {});
            return;
        }
        auto doc = QJsonDocument::fromJson(reply->readAll());
        bool ok = doc.object().value("success").toBool();
        QString message = doc.object().value("message").toString();
        QString token;
        QVariantMap user;
        if (ok) {
            auto data = doc.object().value("data").toObject();
            token = data.value("token").toString();
            user = data.value("user").toObject().toVariantMap();
        }
        cb(ok, message, token, user);
    });
}

void ApiClient::registerUser(const QString &nickname, const QString &password,
                              const QString &email, const QString &verificationCode, AuthCb cb) {
    QUrl url(QString::fromUtf8("%1/api/user/register").arg(Theme::kApiBase));
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QJsonObject body;
    body["nickname"] = nickname;
    body["password"] = password;
    body["email"] = email;
    body["verificationCode"] = verificationCode;
    auto *reply = m_nam.post(req, QJsonDocument(body).toJson());
    connect(reply, &QNetworkReply::finished, this, [reply, cb]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            cb(false, reply->errorString(), {}, {});
            return;
        }
        auto doc = QJsonDocument::fromJson(reply->readAll());
        bool ok = doc.object().value("success").toBool();
        QString message = doc.object().value("message").toString();
        QString token;
        QVariantMap user;
        if (ok) {
            auto data = doc.object().value("data").toObject();
            token = data.value("token").toString();
            user = data.value("user").toObject().toVariantMap();
        }
        cb(ok, message, token, user);
    });
}

void ApiClient::sendVerificationCode(const QString &email, const QString &nickname,
                                     const QString &captchaPassToken,
                                     std::function<void(bool, const QString &)> cb) {
    QUrl url(QString::fromUtf8("%1/api/user/send-verification").arg(Theme::kApiBase));
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QJsonObject body;
    body["email"] = email;
    body["nickname"] = nickname;
    body["captchaPassToken"] = captchaPassToken;
    auto *reply = m_nam.post(req, QJsonDocument(body).toJson());
    connect(reply, &QNetworkReply::finished, this, [reply, cb]() {
        reply->deleteLater();
        const QByteArray raw = reply->readAll();
        auto doc = QJsonDocument::fromJson(raw);
        const QString message = doc.object().value("message").toString();
        if (reply->error() != QNetworkReply::NoError) {
            cb(false, message.isEmpty() ? reply->errorString() : message);
            return;
        }
        const bool ok = doc.object().value("success").toBool();
        cb(ok, message);
    });
}

void ApiClient::fetchSliderCaptchaChallenge(SliderCaptchaChallengeCb cb) {
    QUrl url(QString::fromUtf8("%1/api/captcha/slider").arg(Theme::kApiBase));
    auto *reply = m_nam.get(QNetworkRequest(url));
    connect(reply, &QNetworkReply::finished, this, [reply, cb]() {
        reply->deleteLater();
        const QByteArray raw = reply->readAll();
        auto doc = QJsonDocument::fromJson(raw);
        const QString msg = doc.object().value("message").toString();
        if (reply->error() != QNetworkReply::NoError) {
            cb(false, msg.isEmpty() ? reply->errorString() : msg, {});
            return;
        }
        const bool ok = doc.object().value("success").toBool();
        const QVariantMap data = doc.object().value("data").toObject().toVariantMap();
        cb(ok, msg, data);
    });
}

void ApiClient::verifySliderCaptcha(const QString &captchaToken, int captchaOffsetX, SliderCaptchaVerifyCb cb) {
    QUrl url(QString::fromUtf8("%1/api/captcha/slider/verify").arg(Theme::kApiBase));
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QJsonObject body;
    body["captchaToken"] = captchaToken;
    body["captchaOffsetX"] = captchaOffsetX;
    auto *reply = m_nam.post(req, QJsonDocument(body).toJson());
    connect(reply, &QNetworkReply::finished, this, [reply, cb]() {
        reply->deleteLater();
        const QByteArray raw = reply->readAll();
        auto doc = QJsonDocument::fromJson(raw);
        const QString msg = doc.object().value("message").toString();
        if (reply->error() != QNetworkReply::NoError) {
            cb(false, msg.isEmpty() ? reply->errorString() : msg, {});
            return;
        }
        const bool ok = doc.object().value("success").toBool();
        QString pass;
        if (ok) {
            const auto data = doc.object().value("data").toObject();
            pass = data.value("captchaPassToken").toString();
        }
        cb(ok, msg, pass);
    });
}

void ApiClient::sendResetCode(const QString &email, std::function<void(bool, const QString&)> cb) {
    QUrl url(QString::fromUtf8("%1/api/user/send-reset-code").arg(Theme::kApiBase));
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QJsonObject body;
    body["email"] = email;
    auto *reply = m_nam.post(req, QJsonDocument(body).toJson());
    connect(reply, &QNetworkReply::finished, this, [reply, cb]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            cb(false, reply->errorString());
            return;
        }
        auto doc = QJsonDocument::fromJson(reply->readAll());
        bool ok = doc.object().value("success").toBool();
        QString message = doc.object().value("message").toString();
        cb(ok, message);
    });
}

void ApiClient::resetPassword(const QString &email, const QString &verificationCode,
                               const QString &newPassword, std::function<void(bool, const QString&)> cb) {
    QUrl url(QString::fromUtf8("%1/api/user/reset-password").arg(Theme::kApiBase));
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QJsonObject body;
    body["email"] = email;
    body["verificationCode"] = verificationCode;
    body["newPassword"] = newPassword;
    auto *reply = m_nam.post(req, QJsonDocument(body).toJson());
    connect(reply, &QNetworkReply::finished, this, [reply, cb]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            cb(false, reply->errorString());
            return;
        }
        auto doc = QJsonDocument::fromJson(reply->readAll());
        bool ok = doc.object().value("success").toBool();
        QString message = doc.object().value("message").toString();
        cb(ok, message);
    });
}

// 新实现的API
void ApiClient::searchMusic(const QString &query, int page, int pageSize, MusicSearchCb cb) {
    QUrl url(QString::fromUtf8("%1/api/music/search").arg(Theme::kApiBase));
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QJsonObject body;
    body["query"] = query;
    auto *reply = m_nam.post(req, QJsonDocument(body).toJson());
    connect(reply, &QNetworkReply::finished, this, [reply, cb, query]() {
        reply->deleteLater();
        int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError) {
            qDebug() << "[搜索API]搜索:" << query << "，HTTP状态码:" << statusCode
                     << "，协议:" << httpProtocolLabel(reply) << "，错误:" << reply->errorString();
            if (cb) cb(false, 0, 0, 0, {});
            return;
        }
        auto response = reply->readAll();
        auto doc = QJsonDocument::fromJson(response);
        bool ok = doc.object().value("success").toBool();
        QList<QVariantMap> results;
        int total = 0, currentPage = 0, currentPageSize = 0;
        if (ok) {
            auto obj = doc.object();
            // Support both formats: with or without "data" wrapper
            if (obj.contains("data")) {
                auto data = obj.value("data").toObject();
                total = data.value("total").toInt();
                currentPage = data.value("page").toInt();
                currentPageSize = data.value("pageSize").toInt();
                for (const auto &v : data.value("results").toArray()) {
                    results.append(v.toObject().toVariantMap());
                }
            } else {
                total = obj.value("total").toInt();
                currentPage = 1;
                currentPageSize = obj.value("results").toArray().size();
                for (const auto &v : obj.value("results").toArray()) {
                    results.append(v.toObject().toVariantMap());
                }
            }
            qDebug() << "[搜索API]搜索:" << query << "，HTTP状态码:" << statusCode
                     << "，协议:" << httpProtocolLabel(reply) << "，成功，找到" << total << "个结果";
        } else {
            QString message = doc.object().value("message").toString();
            qDebug() << "[搜索API]搜索:" << query << "，HTTP状态码:" << statusCode
                     << "，协议:" << httpProtocolLabel(reply) << "，失败，消息:" << message;
        }
        if (cb) cb(ok, total, currentPage, currentPageSize, results);
    });
}

void ApiClient::fetchMusicInfo(int musicId, MusicInfoCb cb) {
    QUrl url(QString::fromUtf8("%1/api/music/info/%2").arg(Theme::kApiBase).arg(musicId));
    auto *reply = m_nam.get(QNetworkRequest(url));
    connect(reply, &QNetworkReply::finished, this, [reply, cb]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            if (cb) cb(false, {});
            return;
        }
        auto doc = QJsonDocument::fromJson(reply->readAll());
        bool ok = doc.object().value("success").toBool();
        QVariantMap musicInfo;
        if (ok) {
            auto data = doc.object().value("data").toObject();
            musicInfo = data.toVariantMap();
        }
        if (cb) cb(ok, musicInfo);
    });
}

void ApiClient::fetchLyrics(int musicId, LyricsCb cb) {
    QUrl url(QString::fromUtf8("%1/api/music/lyrics/%2")
                 .arg(QString::fromUtf8(Theme::kApiBase))
                 .arg(musicId));
    auto *reply = m_nam.get(QNetworkRequest(url));
    connect(reply, &QNetworkReply::finished, this, [reply, cb]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            if (cb) cb(false, "");
            return;
        }
        auto doc = QJsonDocument::fromJson(reply->readAll());
        bool ok = doc.object().value("success").toBool();
        QString lyrics = "";
        if (ok) {
            lyrics = doc.object().value("data").toString();
        }
        if (cb) cb(ok, lyrics);
    });
}

void ApiClient::searchArtists(const QString &query, ArtistSearchCb cb) {
    QUrl url(QString::fromUtf8("%1/api/artists/search").arg(Theme::kApiBase));
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QJsonObject body;
    body["query"] = query;
    auto *reply = m_nam.post(req, QJsonDocument(body).toJson());
    connect(reply, &QNetworkReply::finished, this, [reply, cb]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            if (cb) cb(false, {});
            return;
        }
        auto doc = QJsonDocument::fromJson(reply->readAll());
        bool ok = doc.object().value("success").toBool();
        QVariantMap artistInfo;
        if (ok) {
            artistInfo = doc.object().value("artist").toObject().toVariantMap();
        }
        if (cb) cb(ok, artistInfo);
    });
}

void ApiClient::fetchUploadedMusic(UploadedMusicCb cb) {
    QUrl url(QString::fromUtf8("%1/api/user/uploaded-music").arg(Theme::kApiBase));
    QNetworkRequest req(url);
    if (UserManager::instance().isLoggedIn()) {
        req.setRawHeader("Authorization", UserManager::instance().token().toUtf8());
    }
    auto *reply = m_nam.get(req);
    connect(reply, &QNetworkReply::finished, this, [reply, cb]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            if (cb) cb(false, 0, {});
            return;
        }
        auto doc = QJsonDocument::fromJson(reply->readAll());
        bool ok = doc.object().value("success").toBool();
        QList<QVariantMap> musicList;
        int total = 0;
        if (ok) {
            total = doc.object().value("total").toInt();
            for (const auto &v : doc.object().value("musicList").toArray()) {
                musicList.append(v.toObject().toVariantMap());
            }
        }
        if (cb) cb(ok, total, musicList);
    });
}

void ApiClient::changePassword(const QString &oldPassword, const QString &newPassword, std::function<void(bool, const QString&)> cb) {
    QUrl url(QString::fromUtf8("%1/api/user/password/change").arg(Theme::kApiBase));
    QNetworkRequest req(url);
    if (UserManager::instance().isLoggedIn()) {
        req.setRawHeader("Authorization", QString("Bearer %1").arg(UserManager::instance().token()).toUtf8());
    }
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QJsonObject body;
    body["oldPassword"] = oldPassword;
    body["newPassword"] = newPassword;
    auto *reply = m_nam.post(req, QJsonDocument(body).toJson());
    connect(reply, &QNetworkReply::finished, this, [reply, cb]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            if (cb) cb(false, reply->errorString());
            return;
        }
        auto doc = QJsonDocument::fromJson(reply->readAll());
        bool ok = doc.object().value("success").toBool();
        QString message = ok ? doc.object().value("message").toString() : doc.object().value("error").toString();
        if (cb) cb(ok, message);
    });
}

void ApiClient::changeNickname(const QString &nickname, NicknameChangeCb cb) {
    QUrl url(QString::fromUtf8("%1/api/user/nickname/change").arg(Theme::kApiBase));
    QNetworkRequest req(url);
    if (UserManager::instance().isLoggedIn()) {
        req.setRawHeader("Authorization", QString("Bearer %1").arg(UserManager::instance().token()).toUtf8());
    }
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QJsonObject body;
    body["nickname"] = nickname;
    auto *reply = m_nam.post(req, QJsonDocument(body).toJson());
    connect(reply, &QNetworkReply::finished, this, [reply, cb]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            if (cb) cb(false, reply->errorString(), QString());
            return;
        }
        const QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
        const bool ok = obj.value("success").toBool();
        const QString message = obj.value("message").toString();
        QString savedNickname;
        if (ok)
            savedNickname = obj.value("data").toObject().value("nickname").toString();
        if (cb) cb(ok, message, savedNickname);
    });
}

void ApiClient::fetchUserInfo(UserInfoCb cb) {
    QUrl url(QString::fromUtf8("%1/api/user/info").arg(Theme::kApiBase));
    QNetworkRequest req(url);
    if (UserManager::instance().isLoggedIn()) {
        req.setRawHeader("Authorization", QString("Bearer %1").arg(UserManager::instance().token()).toUtf8());
    }
    auto *reply = m_nam.get(req);
    connect(reply, &QNetworkReply::finished, this, [reply, cb]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            if (cb) cb(false, reply->errorString(), QVariantMap());
            return;
        }
        const QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
        const bool ok = obj.value("success").toBool();
        const QString message = obj.value("message").toString();
        const QVariantMap user =
            obj.value("data").toObject().value("user").toObject().toVariantMap();
        if (cb) cb(ok, message, user);
    });
}

void ApiClient::fetchPlaylists(const QString &query, PlaylistsCb cb) {
    QUrl url(QString::fromUtf8("%1/api/playlists/search").arg(Theme::kApiBase));
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QJsonObject body;
    body["query"] = query;
    auto *reply = m_nam.post(req, QJsonDocument(body).toJson());
    connect(reply, &QNetworkReply::finished, this, [reply, cb]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) { cb(false, {}); return; }
        auto doc = QJsonDocument::fromJson(reply->readAll());
        bool ok = doc.object().value("success").toBool();
        QList<QVariantMap> res;
        if (ok) for (const auto &v : doc.object().value("results").toArray())
            res.append(v.toObject().toVariantMap());
        cb(ok, res);
    });
}

void ApiClient::fetchPlaylistDetail(int playlistId, PlaylistDetailCb cb) {
    QUrl url(QString::fromUtf8("%1/api/playlist/%2").arg(Theme::kApiBase).arg(playlistId));
    auto *reply = m_nam.get(QNetworkRequest(url));
    connect(reply, &QNetworkReply::finished, this, [reply, cb]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            if (cb) cb(false, {});
            return;
        }
        auto doc = QJsonDocument::fromJson(reply->readAll());
        bool ok = doc.object().value("success").toBool();
        QVariantMap detail;
        if (ok) {
            detail = doc.object().value("playlist").toObject().toVariantMap();
        }
        if (cb) cb(ok, detail);
    });
}

void ApiClient::fetchUserPlaylists(UserPlaylistsCb cb) {
    QUrl url(QString::fromUtf8("%1/api/user/playlists").arg(Theme::kApiBase));
    QNetworkRequest req(url);
    if (UserManager::instance().isLoggedIn()) {
        req.setRawHeader("Authorization", UserManager::instance().token().toUtf8());
    }
    auto *reply = m_nam.get(req);
    connect(reply, &QNetworkReply::finished, this, [reply, cb]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            if (cb) cb(false, {});
            return;
        }
        auto doc = QJsonDocument::fromJson(reply->readAll());
        bool ok = doc.object().value("success").toBool();
        QList<QVariantMap> res;
        if (ok) for (const auto &v : doc.object().value("playlists").toArray())
            res.append(v.toObject().toVariantMap());
        if (cb) cb(ok, res);
    });
}

void ApiClient::createPlaylist(const QString &name, const QString &description, CreatePlaylistCb cb) {
    QUrl url(QString::fromUtf8("%1/api/user/playlist/create").arg(Theme::kApiBase));
    QNetworkRequest req(url);
    req.setRawHeader("Authorization", UserManager::instance().token().toUtf8());
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QJsonObject body;
    body["name"] = name;
    if (!description.isEmpty()) body["description"] = description;
    auto *reply = m_nam.post(req, QJsonDocument(body).toJson());
    connect(reply, &QNetworkReply::finished, this, [reply, cb]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            if (cb) cb(false, reply->errorString(), {});
            return;
        }
        auto doc = QJsonDocument::fromJson(reply->readAll());
        bool ok = doc.object().value("success").toBool();
        QString message = doc.object().value("message").toString();
        QVariantMap playlist;
        if (ok) {
            playlist = doc.object().value("playlist").toObject().toVariantMap();
        }
        if (cb) cb(ok, message, playlist);
    });
}

void ApiClient::updatePlaylist(int playlistId, const QString &name, const QString &description, UpdatePlaylistCb cb) {
    QUrl url(QString::fromUtf8("%1/api/user/playlist/update").arg(Theme::kApiBase));
    QNetworkRequest req(url);
    req.setRawHeader("Authorization", UserManager::instance().token().toUtf8());
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QJsonObject body;
    body["id"] = playlistId;
    body["name"] = name;
    if (!description.isNull()) body["description"] = description;
    auto *reply = m_nam.post(req, QJsonDocument(body).toJson());
    connect(reply, &QNetworkReply::finished, this, [reply, cb]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            if (cb) cb(false, reply->errorString(), {});
            return;
        }
        auto doc = QJsonDocument::fromJson(reply->readAll());
        bool ok = doc.object().value("success").toBool();
        QString message = doc.object().value("message").toString();
        QVariantMap playlist;
        if (ok) {
            playlist = doc.object().value("playlist").toObject().toVariantMap();
        }
        if (cb) cb(ok, message, playlist);
    });
}

void ApiClient::deletePlaylist(int playlistId, DeletePlaylistCb cb) {
    QUrl url(QString::fromUtf8("%1/api/user/playlist/delete").arg(Theme::kApiBase));
    QNetworkRequest req(url);
    req.setRawHeader("Authorization", UserManager::instance().token().toUtf8());
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QJsonObject body;
    body["id"] = playlistId;
    auto *reply = m_nam.post(req, QJsonDocument(body).toJson());
    connect(reply, &QNetworkReply::finished, this, [reply, cb]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            if (cb) cb(false, reply->errorString());
            return;
        }
        auto doc = QJsonDocument::fromJson(reply->readAll());
        bool ok = doc.object().value("success").toBool();
        QString message = doc.object().value("message").toString();
        if (cb) cb(ok, message);
    });
}

void ApiClient::fetchPlaylistMusic(int playlistId, PlaylistMusicCb cb) {
    QUrl url(QString::fromUtf8("%1/api/user/playlist/music/%2").arg(Theme::kApiBase).arg(playlistId));
    QNetworkRequest req(url);
    if (UserManager::instance().isLoggedIn()) {
        req.setRawHeader("Authorization", UserManager::instance().token().toUtf8());
    }
    auto *reply = m_nam.get(req);
    connect(reply, &QNetworkReply::finished, this, [reply, cb, playlistId]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            qDebug() << "[歌单音乐] playlistId =" << playlistId << ", 协议:" << httpProtocolLabel(reply)
                     << ", 网络错误:" << reply->errorString();
            if (cb) cb(false, 0, {});
            return;
        }
        auto body = reply->readAll();
        auto doc = QJsonDocument::fromJson(body);
        bool ok = doc.object().value("success").toBool();
        QList<QVariantMap> res;
        int total = 0;
        if (ok) {
            total = doc.object().value("total").toInt();
            for (const auto &v : doc.object().value("musicList").toArray())
                res.append(v.toObject().toVariantMap());
            qDebug() << "[歌单音乐] playlistId =" << playlistId << ", 协议:" << httpProtocolLabel(reply)
                     << ", total =" << total << ", 实际获取 =" << res.size();
        } else {
            qDebug() << "[歌单音乐] playlistId =" << playlistId << ", 协议:" << httpProtocolLabel(reply)
                     << ", API返回success=false, body:" << body;
        }
        if (cb) cb(ok, total, res);
    });
}

void ApiClient::addMusicToPlaylist(int playlistId, int musicId, AddMusicToPlaylistCb cb) {
    QUrl url(QString::fromUtf8("%1/api/user/playlist/music/add").arg(Theme::kApiBase));
    QNetworkRequest req(url);
    req.setRawHeader("Authorization", UserManager::instance().token().toUtf8());
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QJsonObject body;
    body["playlistId"] = playlistId;
    body["musicId"] = musicId;
    auto *reply = m_nam.post(req, QJsonDocument(body).toJson());
    connect(reply, &QNetworkReply::finished, this, [reply, cb]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            if (cb) cb(false, reply->errorString());
            return;
        }
        auto doc = QJsonDocument::fromJson(reply->readAll());
        bool ok = doc.object().value("success").toBool();
        QString message = doc.object().value("message").toString();
        if (cb) cb(ok, message);
    });
}

void ApiClient::removeMusicFromPlaylist(int playlistId, int musicId, RemoveMusicFromPlaylistCb cb) {
    QUrl url(QString::fromUtf8("%1/api/user/playlist/music/remove").arg(Theme::kApiBase));
    QNetworkRequest req(url);
    req.setRawHeader("Authorization", UserManager::instance().token().toUtf8());
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QJsonObject body;
    body["playlistId"] = playlistId;
    body["musicId"] = musicId;
    auto *reply = m_nam.post(req, QJsonDocument(body).toJson());
    connect(reply, &QNetworkReply::finished, this, [reply, cb]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            if (cb) cb(false, reply->errorString());
            return;
        }
        auto doc = QJsonDocument::fromJson(reply->readAll());
        bool ok = doc.object().value("success").toBool();
        QString message = doc.object().value("message").toString();
        if (cb) cb(ok, message);
    });
}

void ApiClient::fetchFavoritePlaylists(UserPlaylistsCb cb) {
    QUrl url(QString::fromUtf8("%1/api/user/favorite-playlists").arg(Theme::kApiBase));
    QNetworkRequest req(url);
    req.setRawHeader("Authorization", UserManager::instance().token().toUtf8());
    auto *reply = m_nam.get(req);
    connect(reply, &QNetworkReply::finished, this, [reply, cb]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            if (cb) cb(false, {});
            return;
        }
        auto doc = QJsonDocument::fromJson(reply->readAll());
        bool ok = doc.object().value("success").toBool();
        QList<QVariantMap> res;
        if (ok) for (const auto &v : doc.object().value("playlists").toArray())
            res.append(v.toObject().toVariantMap());
        if (cb) cb(ok, res);
    });
}

void ApiClient::favoritePlaylist(int playlistId, AddMusicToPlaylistCb cb) {
    QUrl url(QString::fromUtf8("%1/api/user/favorite-playlists").arg(Theme::kApiBase));
    QNetworkRequest req(url);
    req.setRawHeader("Authorization", UserManager::instance().token().toUtf8());
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QJsonObject body;
    body["playlistId"] = playlistId;
    auto *reply = m_nam.post(req, QJsonDocument(body).toJson());
    connect(reply, &QNetworkReply::finished, this, [reply, cb]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            if (cb) cb(false, reply->errorString());
            return;
        }
        auto doc = QJsonDocument::fromJson(reply->readAll());
        bool ok = doc.object().value("success").toBool();
        QString message = doc.object().value("message").toString();
        if (cb) cb(ok, message);
    });
}

void ApiClient::unfavoritePlaylist(int playlistId, RemoveMusicFromPlaylistCb cb) {
    QUrl url(QString::fromUtf8("%1/api/user/favorite-playlists/%2").arg(Theme::kApiBase).arg(playlistId));
    QNetworkRequest req(url);
    req.setRawHeader("Authorization", UserManager::instance().token().toUtf8());
    auto *reply = m_nam.deleteResource(req);
    connect(reply, &QNetworkReply::finished, this, [reply, cb]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            if (cb) cb(false, reply->errorString());
            return;
        }
        auto doc = QJsonDocument::fromJson(reply->readAll());
        bool ok = doc.object().value("success").toBool();
        QString message = doc.object().value("message").toString();
        if (cb) cb(ok, message);
    });
}

void ApiClient::fetchFavoritePlaylistMusic(int playlistId, PlaylistMusicCb cb) {
    QUrl url(QString::fromUtf8("%1/api/user/favorite-playlists/%2").arg(Theme::kApiBase).arg(playlistId));
    QNetworkRequest req(url);
    req.setRawHeader("Authorization", UserManager::instance().token().toUtf8());
    auto *reply = m_nam.get(req);
    connect(reply, &QNetworkReply::finished, this, [reply, cb]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            if (cb) cb(false, 0, {});
            return;
        }
        auto doc = QJsonDocument::fromJson(reply->readAll());
        bool ok = doc.object().value("success").toBool();
        QList<QVariantMap> res;
        if (ok) for (const auto &v : doc.object().value("music").toArray())
            res.append(v.toObject().toVariantMap());
        if (cb) cb(ok, res.size(), res);
    });
}

void ApiClient::fetchVipPricing(VipPricingCb cb)
{
    QUrl url(QString::fromUtf8("%1/api/vip/pricing").arg(QString::fromUtf8(Theme::kApiBase)));
    auto *reply = m_nam.get(QNetworkRequest(url));
    connect(reply, &QNetworkReply::finished, this, [reply, cb]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            if (cb)
                cb(false, reply->errorString(), {});
            return;
        }
        const auto doc = QJsonDocument::fromJson(reply->readAll());
        const auto root = doc.object();
        const bool ok = root.value(QStringLiteral("success")).toBool();
        const QString message = root.value(QStringLiteral("message")).toString();
        QList<QVariantMap> items;
        if (ok) {
            for (const auto &v : root.value(QStringLiteral("data")).toArray())
                items.append(v.toObject().toVariantMap());
        }
        if (cb)
            cb(ok, message, items);
    });
}

void ApiClient::createVipPayOrder(int pricingId, const QString &payType, VipPayCreateCb cb)
{
    if (!UserManager::instance().isLoggedIn()) {
        if (cb)
            cb(false, QString(), {});
        return;
    }
    QUrl url(QString::fromUtf8("%1/api/vip/pay/create").arg(QString::fromUtf8(Theme::kApiBase)));
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setRawHeader("Authorization", UserManager::instance().token().toUtf8());
    QJsonObject body;
    body.insert(QStringLiteral("pricingId"), pricingId);
    body.insert(QStringLiteral("payType"), payType);
    auto *reply = m_nam.post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [reply, cb]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            if (cb)
                cb(false, reply->errorString(), {});
            return;
        }
        const auto doc = QJsonDocument::fromJson(reply->readAll());
        const auto root = doc.object();
        const bool ok = root.value(QStringLiteral("success")).toBool();
        const QString message = root.value(QStringLiteral("message")).toString();
        const QVariantMap data = root.value(QStringLiteral("data")).toObject().toVariantMap();
        if (cb)
            cb(ok, message, data);
    });
}

void ApiClient::syncSessionVipStatus(VipStatusCb cb)
{
    if (!UserManager::instance().isLoggedIn()) {
        if (cb)
            cb(false, false);
        return;
    }
    QUrl url(QString::fromUtf8("%1/api/user/playlists").arg(Theme::kApiBase));
    QNetworkRequest req(url);
    req.setRawHeader("Authorization", UserManager::instance().token().toUtf8());
    auto *reply = m_nam.get(req);
    connect(reply, &QNetworkReply::finished, this, [reply, cb]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            if (cb)
                cb(false, UserManager::instance().isVip());
            return;
        }
        const auto doc = QJsonDocument::fromJson(reply->readAll());
        const auto root = doc.object();
        const bool ok = root.value(QStringLiteral("success")).toBool();
        const bool isVip = root.value(QStringLiteral("isVip")).toBool();
        const QString vipExpiresAt = root.value(QStringLiteral("vipExpiresAt")).toString();
        if (ok)
            UserManager::instance().updateVipStatus(isVip, vipExpiresAt);
        if (cb)
            cb(ok, isVip);
    });
}

void ApiClient::createVideoRenderJob(int musicId, double startSec, bool watermarked, VideoRenderCreateCb cb)
{
    if (!UserManager::instance().isLoggedIn()) {
        if (cb)
            cb(false, QString(), {});
        return;
    }
    QUrl url(QString::fromUtf8("%1/api/video/render/create").arg(Theme::kApiBase));
    QNetworkRequest req(url);
    req.setRawHeader("Authorization", UserManager::instance().token().toUtf8());
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    QJsonObject body;
    body[QStringLiteral("musicId")] = musicId;
    body[QStringLiteral("startSec")] = startSec;
    body[QStringLiteral("watermarked")] = watermarked;
    auto *reply = m_nam.post(req, QJsonDocument(body).toJson());
    connect(reply, &QNetworkReply::finished, this, [reply, cb]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            if (cb)
                cb(false, reply->errorString(), {});
            return;
        }
        const auto doc = QJsonDocument::fromJson(reply->readAll());
        const auto root = doc.object();
        const bool ok = root.value(QStringLiteral("success")).toBool();
        const QString message = root.value(QStringLiteral("message")).toString();
        const QVariantMap data = root.value(QStringLiteral("data")).toObject().toVariantMap();
        if (cb)
            cb(ok, message, data);
    });
}

void ApiClient::fetchVideoRenderStatus(const QString &jobId, VideoRenderStatusCb cb)
{
    if (!UserManager::instance().isLoggedIn() || jobId.isEmpty()) {
        if (cb)
            cb(false, {});
        return;
    }
    QUrl url(QString::fromUtf8("%1/api/video/render/%2").arg(Theme::kApiBase, jobId));
    QNetworkRequest req(url);
    req.setRawHeader("Authorization", UserManager::instance().token().toUtf8());
    auto *reply = m_nam.get(req);
    connect(reply, &QNetworkReply::finished, this, [reply, cb]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            if (cb)
                cb(false, {});
            return;
        }
        const auto doc = QJsonDocument::fromJson(reply->readAll());
        const auto root = doc.object();
        const bool ok = root.value(QStringLiteral("success")).toBool();
        const QVariantMap data = root.value(QStringLiteral("data")).toObject().toVariantMap();
        if (cb)
            cb(ok, data);
    });
}

void ApiClient::downloadVideoRenderFile(const QString &jobId, const QString &saveFilePath, VideoRenderDownloadCb cb)
{
    if (jobId.isEmpty() || saveFilePath.isEmpty()) {
        if (cb)
            cb(false, QStringLiteral("invalid path"));
        return;
    }
    QUrl url(QString::fromUtf8("%1/api/video/render/%2/download").arg(Theme::kApiBase, jobId));
    QNetworkRequest req(url);
    auto *reply = m_nam.get(req);
    connect(reply, &QNetworkReply::finished, this, [reply, saveFilePath, cb]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            if (cb)
                cb(false, reply->errorString());
            return;
        }
        const QByteArray data = reply->readAll();
        QSaveFile out(saveFilePath);
        if (!out.open(QIODevice::WriteOnly)) {
            if (cb)
                cb(false, out.errorString());
            return;
        }
        if (out.write(data) != data.size() || !out.commit()) {
            if (cb)
                cb(false, out.errorString());
            return;
        }
        if (cb)
            cb(true, QString());
    });
}

// ─── 网易云歌单导入 ────────────────────────────────────

void ApiClient::fetchNeteasePlaylist(qint64 playlistId, NeteasePlaylistCb cb)
{
    QUrl url(QString::fromUtf8("%1/loser/netease/playlist/track/all?id=%2").arg(Theme::kApiBase).arg(playlistId));
    QNetworkRequest req(url);
    req.setTransferTimeout(120000);
    req.setRawHeader("Authorization", UserManager::instance().token().toUtf8());

    auto *reply = m_nam.get(req);
    connect(reply, &QNetworkReply::finished, this, [reply, cb, playlistId]() {
        reply->deleteLater();
        NeteasePlaylistInfo info;
        info.id = playlistId;

        if (reply->error() != QNetworkReply::NoError) {
            qDebug() << "[网易云歌单] 请求失败:" << reply->errorString();
            if (cb) cb(false, reply->errorString(), info);
            return;
        }

        const QByteArray data = reply->readAll();
        const auto doc = QJsonDocument::fromJson(data);
        const auto root = doc.object();

        int code = root.value(QStringLiteral("code")).toInt();
        if (code != 200) {
            QString msg = root.value(QStringLiteral("message")).toString();
            if (msg.isEmpty())
                msg = root.value(QStringLiteral("msg")).toString();
            if (msg.isEmpty())
                msg = QStringLiteral("歌单不存在");
            qDebug() << "[网易云歌单] API 返回错误:" << code << msg;
            if (cb) cb(false, msg, info);
            return;
        }

        info.id = playlistId;
        info.name = QStringLiteral("网易云歌单 %1").arg(playlistId);

        const auto tracks = root.value(QStringLiteral("songs")).toArray();
        info.trackCount = tracks.size();
        for (const auto &trackVal : tracks) {
            const auto track = trackVal.toObject();
            NeteaseTrack t;
            t.name = track.value(QStringLiteral("name")).toString().trimmed();

            const auto artists = track.value(QStringLiteral("ar")).toArray();
            QStringList artistNames;
            for (const auto &artistVal : artists) {
                const QString name = artistVal.toObject().value(QStringLiteral("name")).toString().trimmed();
                if (!name.isEmpty())
                    artistNames.append(name);
            }
            t.artist = artistNames.join(QStringLiteral(" / "));

            if (!t.name.isEmpty())
                info.tracks.append(t);
        }

        qDebug() << "[网易云歌单] 获取成功, 歌单:" << info.name
                 << ", 曲目数:" << info.tracks.size()
                 << ", 标注曲目数:" << info.trackCount;
        if (cb) cb(true, QString(), info);
    });
}

void ApiClient::fetchQqPlaylist(const QString &disstid, QqPlaylistCb cb)
{
    QUrl url(QString::fromUtf8("%1/loser/qq/getSongListDetail?disstid=%2").arg(Theme::kApiBase, disstid));
    QNetworkRequest req(url);
    req.setTransferTimeout(120000);

    auto *reply = m_nam.get(req);
    connect(reply, &QNetworkReply::finished, this, [reply, cb, disstid]() {
        reply->deleteLater();
        QqPlaylistInfo info;
        info.disstid = disstid;

        if (reply->error() != QNetworkReply::NoError) {
            qDebug() << "[QQ歌单] 请求失败:" << reply->errorString();
            if (cb)
                cb(false, reply->errorString(), info);
            return;
        }

        const QByteArray data = reply->readAll();
        const auto doc = QJsonDocument::fromJson(data);
        const auto root = doc.object();
        const auto response = root.value(QStringLiteral("response")).toObject();
        if (response.isEmpty()) {
            if (cb)
                cb(false, QStringLiteral("响应格式错误"), info);
            return;
        }

        const int code = response.value(QStringLiteral("code")).toInt(-1);
        if (code != 0) {
            const QString msg = QStringLiteral("拉取歌单失败 (code=%1)").arg(code);
            qDebug() << "[QQ歌单] API 返回错误:" << code;
            if (cb)
                cb(false, msg, info);
            return;
        }

        const auto cdlist = response.value(QStringLiteral("cdlist")).toArray();
        if (cdlist.isEmpty()) {
            if (cb)
                cb(false, QStringLiteral("歌单不存在"), info);
            return;
        }

        QString dissname;
        int totalBySongNum = 0;
        for (const auto &cdVal : cdlist) {
            const auto cd = cdVal.toObject();
            if (dissname.isEmpty()) {
                dissname = cd.value(QStringLiteral("dissname")).toString().trimmed();
            }
            totalBySongNum += cd.value(QStringLiteral("songnum")).toInt();

            const auto songlist = cd.value(QStringLiteral("songlist")).toArray();
            for (const auto &trackVal : songlist) {
                const auto track = trackVal.toObject();
                NeteaseTrack t;
                QString title = track.value(QStringLiteral("name")).toString().trimmed();
                if (title.isEmpty())
                    title = track.value(QStringLiteral("title")).toString().trimmed();
                t.name = title;

                const auto singers = track.value(QStringLiteral("singer")).toArray();
                QStringList artistNames;
                for (const auto &singerVal : singers) {
                    const QString name =
                        singerVal.toObject().value(QStringLiteral("name")).toString().trimmed();
                    if (!name.isEmpty())
                        artistNames.append(name);
                }
                t.artist = artistNames.join(QStringLiteral(" / "));

                if (!t.name.isEmpty())
                    info.tracks.append(t);
            }
        }

        info.name = dissname;
        info.trackCount = info.tracks.isEmpty() ? totalBySongNum : info.tracks.size();

        qDebug() << "[QQ歌单] 获取成功, 歌单:" << info.name
                 << ", 曲目数:" << info.tracks.size()
                 << ", 标注曲目数:" << info.trackCount;
        if (cb)
            cb(true, QString(), info);
    });
}

void ApiClient::fetchKugouPlaylist(const QString &listId, KugouPlaylistCb cb)
{
    QUrl url(QString::fromUtf8("%1/loser/kugou/getSongListDetail").arg(Theme::kApiBase));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("listid"), listId);
    url.setQuery(query);
    QNetworkRequest req(url);
    req.setTransferTimeout(120000);

    auto *reply = m_nam.get(req);
    connect(reply, &QNetworkReply::finished, this, [reply, cb, listId]() {
        reply->deleteLater();
        KugouPlaylistInfo info;
        info.listId = listId;

        if (reply->error() != QNetworkReply::NoError) {
            qDebug() << "[酷狗歌单] 请求失败:" << reply->errorString();
            if (cb)
                cb(false, reply->errorString(), info);
            return;
        }

        const QByteArray data = reply->readAll();
        const auto doc = QJsonDocument::fromJson(data);
        const auto root = doc.object();
        const auto response = root.value(QStringLiteral("response")).toObject();
        if (response.isEmpty()) {
            QString msg = root.value(QStringLiteral("error")).toString();
            if (msg.isEmpty())
                msg = QStringLiteral("响应格式错误");
            if (cb)
                cb(false, msg, info);
            return;
        }

        const int code = response.value(QStringLiteral("code")).toInt(-1);
        if (code != 0) {
            const QString msg = QStringLiteral("拉取歌单失败 (code=%1)").arg(code);
            qDebug() << "[酷狗歌单] API 返回错误:" << code;
            if (cb)
                cb(false, msg, info);
            return;
        }

        info.listId = response.value(QStringLiteral("listid")).toString();
        if (info.listId.isEmpty())
            info.listId = listId;
        info.name = response.value(QStringLiteral("name")).toString().trimmed();

        const auto songlist = response.value(QStringLiteral("songlist")).toArray();
        for (const auto &trackVal : songlist) {
            const auto track = trackVal.toObject();
            NeteaseTrack t;
            t.name = track.value(QStringLiteral("name")).toString().trimmed();

            const auto singers = track.value(QStringLiteral("singer")).toArray();
            QStringList artistNames;
            for (const auto &singerVal : singers) {
                const QString name =
                    singerVal.toObject().value(QStringLiteral("name")).toString().trimmed();
                if (!name.isEmpty())
                    artistNames.append(name);
            }
            t.artist = artistNames.join(QStringLiteral(" / "));

            if (!t.name.isEmpty())
                info.tracks.append(t);
        }

        const int songnum = response.value(QStringLiteral("songnum")).toInt();
        info.trackCount = info.tracks.isEmpty() ? songnum : info.tracks.size();

        qDebug() << "[酷狗歌单] 获取成功, 歌单:" << info.name
                 << ", 曲目数:" << info.tracks.size()
                 << ", 标注曲目数:" << info.trackCount;
        if (cb)
            cb(true, QString(), info);
    });
}

void ApiClient::batchSearchMusic(const QList<BatchSearchItem> &items, BatchSearchCb cb)
{
    if (items.isEmpty()) {
        BatchSearchResult result;
        result.success = true;
        result.message = QStringLiteral("没有要搜索的歌曲");
        if (cb) cb(true, result);
        return;
    }

    // 每批请求多少首音乐
    const int CHUNK_SIZE = 20;
    struct BatchSearchContext {
        QList<QList<BatchSearchItem>> chunks;
        int nextChunk = 0;
        QList<int> matchedIds;
        int successCount = 0;
        int failCount = 0;
        QString lastMessage;
        bool anyApiSuccess = false;
    };
    auto ctx = std::make_shared<BatchSearchContext>();
    for (int i = 0; i < items.size(); i += CHUNK_SIZE)
        ctx->chunks.append(items.mid(i, CHUNK_SIZE));

    qDebug() << "[批量搜索] 共" << items.size() << "首，分" << ctx->chunks.size() << "批请求（串行）";

    auto processNext = std::make_shared<std::function<void()>>();
    *processNext = [this, cb, ctx, processNext]() {
        if (ctx->nextChunk >= ctx->chunks.size()) {
            BatchSearchResult result;
            result.matchedMusicIds = ctx->matchedIds;
            result.successCount = ctx->successCount;
            result.failCount = ctx->failCount;
            result.message = ctx->lastMessage;
            result.success = ctx->anyApiSuccess || ctx->successCount > 0 || ctx->failCount > 0;
            qDebug() << "[批量搜索] 全部完成，成功" << ctx->successCount << "，失败" << ctx->failCount;
            if (cb) cb(true, result);
            return;
        }

        const int chunkIdx = ctx->nextChunk++;
        const auto &chunk = ctx->chunks.at(chunkIdx);
        const int chunkTotal = ctx->chunks.size();

        QJsonArray itemsArray;
        for (const auto &item : chunk) {
            QJsonObject obj;
            obj[QStringLiteral("title")] = item.title;
            obj[QStringLiteral("artist")] = item.artist;
            itemsArray.append(obj);
        }
        QJsonObject body;
        body[QStringLiteral("items")] = itemsArray;

        QUrl url(QString::fromUtf8("%1/api/music/search").arg(Theme::kApiBase));
        qDebug() << "[批量搜索] 发起第" << (chunkIdx + 1) << "/" << chunkTotal << "批"
                 << "，本批" << chunk.size() << "首，url =" << url.toString();
        for (int i = 0; i < chunk.size(); ++i) {
            const auto &item = chunk.at(i);
            const QString artistPart = item.artist.isEmpty()
                ? QString()
                : QStringLiteral(" — %1").arg(item.artist);
            qDebug() << "[批量搜索]   [" << (chunkIdx + 1) << "/" << chunkTotal << "]"
                     << (i + 1) << "/" << chunk.size() << item.title << artistPart;
        }

        QNetworkRequest req(url);
        req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        req.setTransferTimeout(600000);

        auto *reply = m_nam.post(req, QJsonDocument(body).toJson());
        connect(reply, &QNetworkReply::finished, this, [this, reply, cb, ctx, processNext, chunkIdx, chunk, chunkTotal]() {
            reply->deleteLater();
            const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

            if (reply->error() != QNetworkReply::NoError) {
                qDebug() << "[批量搜索] 第" << (chunkIdx + 1) << "/" << chunkTotal << "批失败"
                         << "，协议:" << httpProtocolLabel(reply)
                         << "，HTTP:" << httpStatus
                         << "，错误:" << reply->errorString();
                BatchSearchResult result;
                result.success = false;
                result.message = reply->errorString();
                if (cb) cb(false, result);
                return;
            }

            const auto doc = QJsonDocument::fromJson(reply->readAll());
            if (!doc.isObject()) {
                qDebug() << "[批量搜索] 第" << (chunkIdx + 1) << "/" << chunkTotal << "批响应无效"
                         << "，协议:" << httpProtocolLabel(reply)
                         << "，HTTP:" << httpStatus;
                BatchSearchResult result;
                result.success = false;
                result.message = QStringLiteral("搜索服务响应无效");
                if (cb) cb(false, result);
                return;
            }

            const auto root = doc.object();
            const bool ok = root.value(QStringLiteral("success")).toBool();
            const QString message = root.value(QStringLiteral("message")).toString();
            if (!message.isEmpty())
                ctx->lastMessage = message;
            if (ok)
                ctx->anyApiSuccess = true;

            const auto results = root.value(QStringLiteral("results")).toArray();
            int chunkMatched = 0;
            int chunkFailed = 0;
            if (results.isEmpty() && !ok) {
                qDebug() << "[批量搜索] 第" << (chunkIdx + 1) << "/" << chunkTotal << "批API返回失败"
                         << "，协议:" << httpProtocolLabel(reply)
                         << "，HTTP:" << httpStatus
                         << "，消息:" << message;
                ctx->failCount += chunk.size();
                chunkFailed = chunk.size();
            } else {
                for (const auto &v : results) {
                    if (v.isNull()) {
                        ctx->failCount++;
                        chunkFailed++;
                    } else {
                        const auto obj = v.toObject();
                        const int musicId = obj.value(QStringLiteral("id")).toInt();
                        if (musicId > 0) {
                            ctx->matchedIds.append(musicId);
                            ctx->successCount++;
                            chunkMatched++;
                        } else {
                            ctx->failCount++;
                            chunkFailed++;
                        }
                    }
                }
                if (results.size() < chunk.size()) {
                    const int missing = chunk.size() - results.size();
                    ctx->failCount += missing;
                    chunkFailed += missing;
                }
                qDebug() << "[批量搜索] 第" << (chunkIdx + 1) << "/" << chunkTotal << "批完成"
                         << "，协议:" << httpProtocolLabel(reply)
                         << "，HTTP:" << httpStatus
                         << "，本批匹配" << chunkMatched << "首，未匹配" << chunkFailed << "首"
                         << "，累计匹配" << ctx->successCount << "首";
            }

            (*processNext)();
        });
    };

    (*processNext)();
}

void ApiClient::batchAddMusicToPlaylist(int playlistId, const QList<int> &musicIds, BatchAddMusicCb cb)
{
    if (musicIds.isEmpty()) {
        BatchAddResult result;
        result.success = true;
        result.message = QStringLiteral("没有要添加的歌曲");
        result.addedCount = 0;
        if (cb) cb(true, result);
        return;
    }
    
    if (!UserManager::instance().isLoggedIn()) {
        BatchAddResult result;
        result.success = false;
        result.message = QStringLiteral("请先登录");
        if (cb) cb(false, result);
        return;
    }
    
    // 去重
    QList<int> uniqueIds;
    QSet<int> seen;
    for (int id : musicIds) {
        if (!seen.contains(id)) {
            seen.insert(id);
            uniqueIds.append(id);
        }
    }
    
    qDebug() << "[批量添加] 歌单ID:" << playlistId << ", 共" << uniqueIds.size() << "首歌曲";
    
    // 使用 musicIds 批量添加 API
    QUrl url(QString::fromUtf8("%1/api/user/playlist/music/add").arg(Theme::kApiBase));
    QNetworkRequest req(url);
    req.setRawHeader("Authorization", UserManager::instance().token().toUtf8());
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    
    // 构建请求体 { "playlistId": xxx, "musicIds": [...] }
    QJsonArray idsArray;
    for (int id : uniqueIds) {
        idsArray.append(id);
    }
    QJsonObject body;
    body[QStringLiteral("playlistId")] = playlistId;
    body[QStringLiteral("musicIds")] = idsArray;
    
    auto *reply = m_nam.post(req, QJsonDocument(body).toJson());
    connect(reply, &QNetworkReply::finished, this, [reply, cb, totalCount = uniqueIds.size()]() {
        reply->deleteLater();
        BatchAddResult result;
        
        if (reply->error() != QNetworkReply::NoError) {
            qDebug() << "[批量添加] 请求失败:" << reply->errorString();
            result.success = false;
            result.message = reply->errorString();
            if (cb) cb(false, result);
            return;
        }
        
        const auto doc = QJsonDocument::fromJson(reply->readAll());
        const auto root = doc.object();
        bool ok = root.value(QStringLiteral("success")).toBool();
        QString message = root.value(QStringLiteral("message")).toString();
        int addedCount = root.value(QStringLiteral("addedCount")).toInt();
        
        result.success = ok;
        result.message = message;
        result.addedCount = addedCount > 0 ? addedCount : (ok ? totalCount : 0);
        
        qDebug() << "[批量添加] 完成, success:" << ok << ", addedCount:" << result.addedCount << ", message:" << message;
        
        if (cb) cb(ok, result);
    });
}

// ─── 外部歌单导入（/loser/{source}/pull，SSE 进度） ────────────

QNetworkReply *ApiClient::pullExternalPlaylist(const QString &source,
                                               const QString &externalPlaylistId,
                                               int targetPlaylistId,
                                               const QString &targetPlaylistName,
                                               ExternalPullCallbacks callbacks)
{
    const QString token = UserManager::instance().token();

    QUrl url(QString::fromUtf8("%1/loser/%2/pull").arg(Theme::kApiBase, source));
    QUrlQuery query;
    if (source == QLatin1String("qq"))
        query.addQueryItem(QStringLiteral("disstid"), externalPlaylistId);
    else if (source == QLatin1String("kugou"))
        query.addQueryItem(QStringLiteral("listid"), externalPlaylistId);
    else
        query.addQueryItem(QStringLiteral("playlistId"), externalPlaylistId);

    const QString trimmedName = targetPlaylistName.trimmed();
    if (!trimmedName.isEmpty())
        query.addQueryItem(QStringLiteral("targetPlaylistName"), trimmedName);
    else
        query.addQueryItem(QStringLiteral("targetPlaylistId"), QString::number(targetPlaylistId));

    // EventSource 场景无法自定义请求头，后端同时支持 token 查询参数
    if (!token.isEmpty())
        query.addQueryItem(QStringLiteral("token"), token);
    url.setQuery(query);

    QNetworkRequest req(url);
    req.setRawHeader("Accept", "text/event-stream");
    if (!token.isEmpty())
        req.setRawHeader("Authorization", token.toUtf8());
    req.setTransferTimeout(0); // 导入可能持续很久，禁用传输超时

    QNetworkReply *reply = m_nam.get(req);

    auto buffer = std::make_shared<QByteArray>();
    auto eventName = std::make_shared<QString>();
    auto payload = std::make_shared<QByteArray>();
    auto completed = std::make_shared<bool>(false);

    auto dispatch = [eventName, payload, callbacks, completed]() {
        if (payload->isEmpty() && eventName->isEmpty())
            return;
        const QString name = eventName->isEmpty() ? QStringLiteral("message") : *eventName;
        const QJsonObject obj = QJsonDocument::fromJson(*payload).object();
        payload->clear();
        eventName->clear();

        if (name == QLatin1String("start")) {
            ExternalPullStart start;
            start.source = obj.value(QStringLiteral("source")).toString();
            start.total = obj.value(QStringLiteral("total")).toInt();
            start.targetPlaylistId = obj.value(QStringLiteral("targetPlaylistId")).toInt();
            start.targetPlaylistCreated = obj.value(QStringLiteral("targetPlaylistCreated")).toBool();
            if (callbacks.onStart) callbacks.onStart(start);
        } else if (name == QLatin1String("track")) {
            ExternalPullTrack track;
            track.index = obj.value(QStringLiteral("index")).toInt();
            track.total = obj.value(QStringLiteral("total")).toInt();
            track.sourceId = obj.value(QStringLiteral("sourceId")).toString();
            track.title = obj.value(QStringLiteral("title")).toString();
            track.artist = obj.value(QStringLiteral("artist")).toString();
            track.status = obj.value(QStringLiteral("status")).toString();
            track.musicId = obj.value(QStringLiteral("musicId")).toInt();
            track.playlistAdded = obj.value(QStringLiteral("playlistAdded")).toBool();
            track.message = obj.value(QStringLiteral("message")).toString();
            if (callbacks.onTrack) callbacks.onTrack(track);
        } else if (name == QLatin1String("progress")) {
            ExternalPullProgress progress;
            progress.index = obj.value(QStringLiteral("index")).toInt();
            progress.total = obj.value(QStringLiteral("total")).toInt();
            progress.bytes = static_cast<qint64>(obj.value(QStringLiteral("bytes")).toDouble());
            progress.totalBytes = static_cast<qint64>(obj.value(QStringLiteral("totalBytes")).toDouble());
            progress.percent = obj.value(QStringLiteral("percent")).toInt(-1);
            if (callbacks.onProgress) callbacks.onProgress(progress);
        } else if (name == QLatin1String("done")) {
            ExternalPullSummary summary;
            summary.total = obj.value(QStringLiteral("total")).toInt();
            summary.imported = obj.value(QStringLiteral("imported")).toInt();
            summary.existed = obj.value(QStringLiteral("existed")).toInt();
            summary.failed = obj.value(QStringLiteral("failed")).toInt();
            *completed = true;
            if (callbacks.onDone) callbacks.onDone(summary);
        } else if (name == QLatin1String("error")) {
            *completed = true;
            if (callbacks.onError)
                callbacks.onError(obj.value(QStringLiteral("message")).toString());
        }
    };

    connect(reply, &QNetworkReply::readyRead, this, [reply, buffer, eventName, payload, dispatch]() {
        buffer->append(reply->readAll());
        int newline;
        while ((newline = buffer->indexOf('\n')) >= 0) {
            QByteArray line = buffer->left(newline);
            buffer->remove(0, newline + 1);
            if (line.endsWith('\r'))
                line.chop(1);
            if (line.isEmpty()) {
                dispatch();
            } else if (line.startsWith(':')) {
                // 心跳/注释帧，忽略
            } else if (line.startsWith("event:")) {
                *eventName = QString::fromUtf8(line.mid(6)).trimmed();
            } else if (line.startsWith("data:")) {
                if (!payload->isEmpty())
                    payload->append('\n');
                payload->append(line.mid(5).trimmed());
            }
        }
    });

    connect(reply, &QNetworkReply::finished, this, [reply, completed, callbacks]() {
        reply->deleteLater();
        if (*completed)
            return;
        *completed = true;

        if (reply->error() != QNetworkReply::NoError) {
            if (callbacks.onError)
                callbacks.onError(reply->errorString());
            return;
        }

        const QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
        QString message = obj.value(QStringLiteral("msg")).toString();
        if (message.isEmpty())
            message = obj.value(QStringLiteral("message")).toString();
        if (message.isEmpty()) {
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            message = status >= 400
                ? QStringLiteral("HTTP %1").arg(status)
                : QStringLiteral("连接已中断");
        }
        if (callbacks.onError)
            callbacks.onError(message);
    });

    return reply;
}

// ─── 扫码登录（/api/user/qrlogin/*，SSE 状态推送） ────────────

void ApiClient::createQrLoginSession(QrLoginCreateCb cb)
{
    QUrl url(QString::fromUtf8("%1/api/user/qrlogin/create").arg(Theme::kApiBase));
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    auto *reply = m_nam.post(req, QByteArrayLiteral("{}"));
    connect(reply, &QNetworkReply::finished, this, [reply, cb]() {
        reply->deleteLater();
        QrLoginSession session;
        if (reply->error() != QNetworkReply::NoError) {
            if (cb) cb(false, reply->errorString(), session);
            return;
        }

        const QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object();
        const QJsonObject data = root.value(QStringLiteral("data")).toObject();
        session.sessionId = data.value(QStringLiteral("sessionId")).toString();
        session.qrContent = data.value(QStringLiteral("qrContent")).toString();
        session.expiresIn = data.value(QStringLiteral("expiresIn")).toInt();

        const bool ok = root.value(QStringLiteral("success")).toBool() && !session.sessionId.isEmpty();
        QString message = root.value(QStringLiteral("message")).toString();
        if (cb) cb(ok, message, session);
    });
}

QNetworkReply *ApiClient::watchQrLoginStatus(const QString &sessionId, QrLoginSseCallbacks callbacks)
{
    QUrl url(QString::fromUtf8("%1/api/user/qrlogin/status").arg(Theme::kApiBase));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("sessionId"), sessionId);
    url.setQuery(query);

    QNetworkRequest req(url);
    req.setRawHeader("Accept", "text/event-stream");
    req.setTransferTimeout(0); // SSE 长连接，禁用传输超时

    QNetworkReply *reply = m_nam.get(req);

    auto buffer = std::make_shared<QByteArray>();
    auto eventName = std::make_shared<QString>();
    auto payload = std::make_shared<QByteArray>();
    auto terminal = std::make_shared<bool>(false); // 已收到终态，服务端随后关闭连接属正常

    connect(reply, &QNetworkReply::readyRead, this,
            [reply, buffer, eventName, payload, terminal, callbacks]() {
                buffer->append(reply->readAll());
                consumeSseChunk(*buffer, *eventName, *payload,
                                [terminal, callbacks](const QString &name, const QByteArray &data) {
                                    if (name != QLatin1String("status"))
                                        return;
                                    const QJsonObject obj = QJsonDocument::fromJson(data).object();
                                    QrLoginStatus status;
                                    status.status = obj.value(QStringLiteral("status")).toString();
                                    status.token = obj.value(QStringLiteral("token")).toString();
                                    status.user =
                                        obj.value(QStringLiteral("user")).toObject().toVariantMap();
                                    if (status.status == QLatin1String("confirmed")
                                        || status.status == QLatin1String("canceled")
                                        || status.status == QLatin1String("expired")) {
                                        *terminal = true;
                                    }
                                    if (callbacks.onStatus)
                                        callbacks.onStatus(status);
                                });
            });

    connect(reply, &QNetworkReply::finished, this, [reply, terminal, callbacks]() {
        reply->deleteLater();
        if (*terminal || reply->error() == QNetworkReply::OperationCanceledError)
            return;

        QString message = reply->errorString();
        const QByteArray body = reply->readAll();
        if (!body.isEmpty()) {
            const QJsonObject obj = QJsonDocument::fromJson(body).object();
            QString fromBody = obj.value(QStringLiteral("msg")).toString();
            if (fromBody.isEmpty())
                fromBody = obj.value(QStringLiteral("message")).toString();
            if (!fromBody.isEmpty())
                message = fromBody;
        }
        if (callbacks.onError)
            callbacks.onError(message);
    });

    return reply;
}

void ApiClient::batchAddFavorites(const QList<int> &musicIds, BatchAddMusicCb cb)
{
    if (musicIds.isEmpty()) {
        BatchAddResult result;
        result.success = true;
        result.message = QStringLiteral("没有要添加的歌曲");
        result.addedCount = 0;
        if (cb) cb(true, result);
        return;
    }

    if (!UserManager::instance().isLoggedIn()) {
        BatchAddResult result;
        result.success = false;
        result.message = QStringLiteral("请先登录");
        if (cb) cb(false, result);
        return;
    }

    QList<int> uniqueIds;
    QSet<int> seen;
    for (int id : musicIds) {
        if (!seen.contains(id)) {
            seen.insert(id);
            uniqueIds.append(id);
        }
    }

    qDebug() << "[批量收藏] 共" << uniqueIds.size() << "首歌曲";

    QUrl url(QString::fromUtf8("%1/api/user/favorites").arg(Theme::kApiBase));
    QNetworkRequest req(url);
    req.setRawHeader("Authorization", UserManager::instance().token().toUtf8());
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

    QJsonArray idsArray;
    for (int id : uniqueIds)
        idsArray.append(id);
    QJsonObject body;
    body[QStringLiteral("musicIds")] = idsArray;

    auto *reply = m_nam.post(req, QJsonDocument(body).toJson());
    connect(reply, &QNetworkReply::finished, this, [reply, cb, totalCount = uniqueIds.size()]() {
        reply->deleteLater();
        BatchAddResult result;

        if (reply->error() != QNetworkReply::NoError) {
            qDebug() << "[批量收藏] 请求失败:" << reply->errorString();
            result.success = false;
            result.message = reply->errorString();
            if (cb) cb(false, result);
            return;
        }

        const auto doc = QJsonDocument::fromJson(reply->readAll());
        const auto root = doc.object();
        bool ok = root.value(QStringLiteral("success")).toBool();
        QString message = root.value(QStringLiteral("message")).toString();
        int addedCount = root.value(QStringLiteral("addedCount")).toInt();

        result.success = ok;
        result.message = message;
        result.addedCount = addedCount > 0 ? addedCount : (ok ? totalCount : 0);

        qDebug() << "[批量收藏] 完成, success:" << ok << ", addedCount:" << result.addedCount
                 << ", message:" << message;

        if (cb) cb(ok, result);
    });
}

void ApiClient::fetchComments(int musicId, int page, int pageSize, CommentsCb cb)
{
    QUrl url(QString::fromUtf8("%1/api/comments").arg(QString::fromUtf8(Theme::kApiBase)));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("musicId"), QString::number(musicId));
    query.addQueryItem(QStringLiteral("page"), QString::number(qMax(1, page)));
    query.addQueryItem(QStringLiteral("pageSize"), QString::number(qMax(1, pageSize)));
    url.setQuery(query);

    QNetworkRequest req(url);
    if (UserManager::instance().isLoggedIn())
        req.setRawHeader("Authorization", QString("Bearer %1").arg(UserManager::instance().token()).toUtf8());

    auto *reply = m_nam.get(req);
    connect(reply, &QNetworkReply::finished, this, [reply, cb]() {
        reply->deleteLater();
        const QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object();
        const bool ok = reply->error() == QNetworkReply::NoError && root.value("success").toBool();
        const QString message = root.value("message").toString();
        const QVariantMap data = root.value("data").toObject().toVariantMap();
        if (cb) cb(ok, message, data);
    });
}

void ApiClient::postComment(int musicId, const QString &content, int parentId, CommentsCb cb)
{
    QUrl url(QString::fromUtf8("%1/api/comments").arg(QString::fromUtf8(Theme::kApiBase)));
    QNetworkRequest req(url);
    if (UserManager::instance().isLoggedIn())
        req.setRawHeader("Authorization", QString("Bearer %1").arg(UserManager::instance().token()).toUtf8());
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QJsonObject body;
    body["musicId"] = musicId;
    body["content"] = content;
    if (parentId > 0)
        body["parentId"] = parentId;

    auto *reply = m_nam.post(req, QJsonDocument(body).toJson());
    connect(reply, &QNetworkReply::finished, this, [reply, cb]() {
        reply->deleteLater();
        const QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object();
        const bool ok = reply->error() == QNetworkReply::NoError && root.value("success").toBool();
        const QString message = root.value("message").toString();
        const QVariantMap data = root.value("data").toObject().toVariantMap();
        if (cb) cb(ok, message, data);
    });
}

void ApiClient::deleteComment(int commentId, CommentsCb cb)
{
    QUrl url(QString::fromUtf8("%1/api/comments").arg(QString::fromUtf8(Theme::kApiBase)));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("id"), QString::number(commentId));
    url.setQuery(query);

    QNetworkRequest req(url);
    if (UserManager::instance().isLoggedIn())
        req.setRawHeader("Authorization", QString("Bearer %1").arg(UserManager::instance().token()).toUtf8());

    auto *reply = m_nam.deleteResource(req);
    connect(reply, &QNetworkReply::finished, this, [reply, cb]() {
        reply->deleteLater();
        const QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object();
        const bool ok = reply->error() == QNetworkReply::NoError && root.value("success").toBool();
        const QString message = root.value("message").toString();
        const QVariantMap data = root.value("data").toObject().toVariantMap();
        if (cb) cb(ok, message, data);
    });
}
