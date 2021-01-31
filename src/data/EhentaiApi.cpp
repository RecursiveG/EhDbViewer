#include "EhentaiApi.h"
#include "DataStore.h"
#include <QJsonDocument>

bool EhTorrentMetadata::isValid() {
    bool valid = true;
    valid &= added > 0;
    valid &= fsize > 0;
    valid &= !hash.empty();
    valid &= !name.empty();
    valid &= tsize > 0;
    return valid;
}

std::optional<EhTorrentMetadata> EhTorrentMetadata::parse(const QJsonObject &obj) {
    EhTorrentMetadata ret;
    ret.added = obj["added"].toString().toLongLong();
    ret.fsize = obj["fsize"].toString().toLongLong();
    ret.hash = obj["hash"].toString().toStdString();
    ret.name = obj["name"].toString().toStdString();
    ret.tsize = obj["tsize"].toString().toLongLong();
    if (!ret.isValid()) {
        qWarning() << "Failed to parse eh torrent data: " << obj;
        return {};
    } else {
        return ret;
    }
}

bool EhGalleryMetadata::isValid() {
    bool valid = true;
    valid &= !archiver_key.empty();
    valid &= category != UNKNOWN;
    valid &= filecount > 0;
    valid &= filesize > 0;
    valid &= gid > 0;
    valid &= posted > 0;
    valid &= rating >= 0;
    for (const std::string &tag : tags) {
        valid &= !tag.empty();
    }
    valid &= !thumb.empty();
    valid &= !title.empty();
    // title_jpn can be empty
    valid &= !token.empty();
    valid &= !uploader.empty();
    return valid;
}

QString EhGalleryMetadata::display() const {
    auto display_timestamp = [](int64_t t_s) {
        QDateTime timestamp;
        timestamp.setSecsSinceEpoch(t_s);
        return timestamp.toString("yyyy-MM-dd hh:mm:ss");
    };

    QString ret;
    ret += QString("gid: %1\n").arg(gid);
    ret += QString("title: %1\n").arg(QString::fromStdString(title));
    ret += QString("title_jpn: %1\n").arg(QString::fromStdString(title_jpn));
    ret += QString("category: %1\n")
               .arg(QString::fromStdString(EhentaiApi::CategoryToString(category)));
    ret += QString("rating: %1\n").arg(rating);
    ret += QString("uploader: %1\n").arg(QString::fromStdString(uploader));
    ret += QString("token: %1\n").arg(QString::fromStdString(token));
    ret += QString("archiver_key: %1\n").arg(QString::fromStdString(archiver_key));

    ret += "tags:\n";
    if (tags.empty()) {
        ret += "  --no tags--\n";
    } else {
        for (const auto &t : tags) {
            ret += QString("  %1\n").arg(QString::fromStdString(t));
        }
    }

    ret += QString("posted: %1\n").arg(display_timestamp(posted));
    return ret;
}

std::optional<EhGalleryMetadata> EhGalleryMetadata::parse(const QJsonObject &obj) {
    bool rating_ok = false;
    EhGalleryMetadata ret;
    ret.archiver_key = obj["archiver_key"].toString().toStdString();
    ret.category =
        EhentaiApi::CategoryFromString(obj["category"].toString().toStdString())
            .value_or(UNKNOWN);
    ret.expunged = obj["expunged"].toBool();
    ret.filecount = obj["filecount"].toString().toLongLong();
    ret.filesize = obj["filesize"].toInt();
    ret.gid = obj["gid"].toInt();
    ret.posted = obj["posted"].toString().toLongLong();
    ret.rating = obj["rating"].toString().toDouble(&rating_ok);
    for (const auto &t : obj["tags"].toArray()) {
        ret.tags.push_back(t.toString().toStdString());
    }
    ret.thumb = obj["thumb"].toString().toStdString();
    ret.title = obj["title"].toString().toStdString();
    ret.title_jpn = obj["title_jpn"].toString().toStdString();
    ret.token = obj["token"].toString().toStdString();
    for (const auto &t : obj["torrents"].toArray()) {
        auto pt = EhTorrentMetadata::parse(t.toObject());
        if (pt) {
            ret.torrents.push_back(pt.value());
        }
    }
    ret.uploader = obj["uploader"].toString().toStdString();
    if (!rating_ok) {
        qWarning() << "failed to parse rating: " << obj;
        return {};
    }
    if (!ret.isValid()) {
        qWarning() << "failed to parse gmetadata" << obj;
        qDebug() << ret.display();
        return {};
    }
    return ret;
}

namespace {
const char *kCategoryNameTable[] = {"__unknown__", "Misc",    "Doujinshi", "Manga",
                                    "Artist CG",   "Game CG", "Image Set", "Cosplay",
                                    "Asian Porn",  "Non-H",   "Western",   "Private"};
} // namespace

std::string EhentaiApi::CategoryToString(EhCategory c) {
    if (c >= MISC && c <= PRIVATE)
        return kCategoryNameTable[c];
    return "__unknown__";
}

std::optional<EhCategory> EhentaiApi::CategoryFromString(std::string val) {
    if (val == "Misc") {
        return MISC;
    } else if (val == "Doujinshi") {
        return DOUJINSHI;
    } else if (val == "Manga") {
        return MANGA;
    } else if (val == "Artist CG") {
        return ARTIST_CG;
    } else if (val == "Game CG") {
        return GAME_CG;
    } else if (val == "Image Set") {
        return IMAGE_SET;
    } else if (val == "Cosplay") {
        return COSPLAY;
    } else if (val == "Asian Porn") {
        return ASIAN_PORN;
    } else if (val == "Non-H") {
        return NON_H;
    } else if (val == "Western") {
        return WESTERN;
    } else if (val == "Private") {
        return PRIVATE;
    } else {
        return {};
    }
}

int EhentaiApi::CategoryToEhViewerValue(EhCategory c) {
    if (c >= MISC && c <= WESTERN)
        return 1 << (c - 1);
    return 0;
}

std::optional<EhCategory> EhentaiApi::CategoryFromEhViewerValue(int val) {
    if (val == 0)
        return {};
    int val_origin = val;
    int bit = 1;
    while ((val % 2) == 0) {
        bit++;
        val >>= 1;
    }
    if (val != 1)
        qWarning() << "Lossy convertion from EhViewer category value" << val_origin;
    if (bit >= MISC && bit <= WESTERN)
        return EhCategory(bit);
    return {};
}

void EhentaiApi::GalleryMetadata(QNetworkAccessManager *nm, int64_t gid, QString token,
                                 Callback<EhGalleryMetadata> cb) {
    auto buildRequest = []() -> QNetworkRequest {
        //        auto settings = EhDbViewerDataStore::GetSettings();
        //        QString mid = settings.value("ehentai/ipb_member_id", "").toString();
        //        QString phash = settings.value("ehentai/ipb_pass_hash", "").toString();
        //        if (mid.size() > 0 && phash.size() > 0) {
        //            req.setUrl(QUrl{"https://exhentai.org/api.php"});
        //            QVariant cookies;
        //            cookies.setValue(
        //                QList{QNetworkCookie{"ipb_member_id", mid.toUtf8()},
        //                QNetworkCookie{"ipb_pass_hash", phash.toUtf8()}});
        //            req.setHeader(QNetworkRequest::CookieHeader, cookies);
        //        }
        QNetworkRequest req{QUrl{"https://e-hentai.org/api.php"}};
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        req.setHeader(QNetworkRequest::UserAgentHeader, "EhDbViewer/0.1");
        return req;
    };
    auto buildPayload = [gid, token]() -> QByteArray {
        /* {
          "method": "gdata",
          "gidlist": [
              [618395,"0439fa3666"]
          ],
          "namespace": 1
        } */
        QJsonObject root;
        QJsonArray gidlist;
        QJsonArray inner_pair;
        inner_pair.append(qlonglong(gid));
        inner_pair.append(token);
        gidlist.append(inner_pair);
        root["method"] = "gdata";
        root["gidlist"] = gidlist;
        root["namespace"] = 1;
        return QJsonDocument(root).toJson();
    };

    QNetworkReply *reply = nm->post(buildRequest(), buildPayload());
    QObject::connect(reply, &QNetworkReply::finished, [reply, cb{std::move(cb)}, gid] {
        do { // use break inside to return early on error path
            if (reply->error() != QNetworkReply::NetworkError::NoError) {
                qDebug() << "network request error " << reply->error();
                cb(reply);
                break;
            }
            // https://ehwiki.org/wiki/API
            auto reply_payload = reply->readAll();
            auto json_doc = QJsonDocument::fromJson(reply_payload);
            auto meta = json_doc["gmetadata"][0].toObject();
            if (meta.isEmpty()) {
                qDebug() << reply_payload;
                qCritical() << "reply missing item in gmetadata";
                cb(reply);
                break;
            }
            if (meta["gid"].toInt() != gid) {
                qCritical() << "eh gallery metadata api gid not match";
                cb(reply);
                break;
            }
            if (meta.contains("error")) {
                qCritical() << "reply contains error";
                cb(reply);
                break;
            }
            auto maybe_metadata = EhGalleryMetadata::parse(meta);
            if (maybe_metadata) {
                maybe_metadata->fetched_time = QDateTime::currentSecsSinceEpoch();
                cb(maybe_metadata.value());
            } else {
                cb(reply);
            }
        } while (false);
        reply->deleteLater();
    });
}
