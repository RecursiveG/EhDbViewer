#ifndef EHENTAIAPI_H
#define EHENTAIAPI_H

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <functional>
#include <variant>

class EhentaiApi {
  public:
    template <typename ReturnT> using Callback = std::function<void(std::variant<ReturnT, QNetworkReply *>)>;
    static void GalleryMetadata(QNetworkAccessManager *nm, int64_t gid, QString token, Callback<QJsonDocument> cb);
};

#endif // EHENTAIAPI_H
