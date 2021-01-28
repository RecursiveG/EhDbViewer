#ifndef EHENTAIAPI_H
#define EHENTAIAPI_H

#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <functional>
#include <string>
#include <variant>
#include <vector>

enum EhCategory {
    UNKNOWN = 0,
    // order same as EhViewer int
    MISC,
    DOUJINSHI,
    MANGA,
    ARTIST_CG,
    GAME_CG,
    IMAGE_SET,
    COSPLAY,
    ASIAN_PORN,
    NON_H,
    WESTERN,
    // EhViewer doesn't use PRIVATE
    PRIVATE
};

struct EhTorrentMetadata {
    int64_t added; // timestamp sec, convert from string
    int64_t fsize; // convert from string
    std::string hash;
    std::string name;
    int64_t tsize; // convert from string

    bool isValid();
    static std::optional<EhTorrentMetadata> parse(const QJsonObject &obj);
};

struct EhGalleryMetadata {
    // 15 attributes
    std::string archiver_key;
    EhCategory category; // convert from string
    bool expunged;
    int64_t filecount; // convert from string
    int64_t filesize;
    int64_t gid;
    int64_t posted; // timestamp sec, convert from string
    double rating;  // convert from string
    std::vector<std::string> tags;
    std::string thumb;
    std::string title;
    std::string title_jpn;
    std::string token;
    std::vector<EhTorrentMetadata> torrents;
    std::string uploader;

    int64_t fetched_time; // unix timestamp in seconds.
    bool isValid();
    QString display() const;
    static std::optional<EhGalleryMetadata> parse(const QJsonObject &obj);
};

class EhentaiApi {
  public:
    static std::string CategoryToString(EhCategory c);
    static std::optional<EhCategory> CategoryFromString(std::string val);
    static int CategoryToEhViewerValue(EhCategory c);
    // Input value is EhViewer's bitfield representation.
    static std::optional<EhCategory> CategoryFromEhViewerValue(int val);

    template <typename ReturnT> using Callback = std::function<void(std::variant<ReturnT, QNetworkReply *>)>;
    static void GalleryMetadata(QNetworkAccessManager *nm, int64_t gid, QString token, Callback<EhGalleryMetadata> cb);
};

#endif // EHENTAIAPI_H
