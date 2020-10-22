#include "EhentaiApi.h"
#include "EhDbViewerDataStore.h"
#include <QJsonDocument>

void EhentaiApi::GalleryMetadata(QNetworkAccessManager *nm, int64_t gid, QString token, Callback<QJsonDocument> cb) {
    auto buildRequest = []() -> QNetworkRequest {
        //        auto settings = EhDbViewerDataStore::GetSettings();
        //        QString mid = settings.value("ehentai/ipb_member_id", "").toString();
        //        QString phash = settings.value("ehentai/ipb_pass_hash", "").toString();
        //        if (mid.size() > 0 && phash.size() > 0) {
        //            req.setUrl(QUrl{"https://exhentai.org/api.php"});
        //            QVariant cookies;
        //            cookies.setValue(
        //                QList{QNetworkCookie{"ipb_member_id", mid.toUtf8()}, QNetworkCookie{"ipb_pass_hash",
        //                phash.toUtf8()}});
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
    QObject::connect(reply, &QNetworkReply::finished, [reply, cb{std::move(cb)}] {
        if (reply->error() != QNetworkReply::NetworkError::NoError) {
            qDebug() << "network request error " << reply->error();
            cb(reply);
        } else {
            auto reply_payload = reply->readAll();
            qDebug() << "===" << reply_payload;
            auto json_doc = QJsonDocument::fromJson(reply_payload);
            assert(json_doc.isObject());
            auto root = json_doc.object();
            assert(root.contains("gmetadata"));
            // TODO parse the json to a struct
            cb(json_doc);
        }
        reply->deleteLater();
    });
}
