#ifndef EHDBVIEWERDATASTORE_H
#define EHDBVIEWERDATASTORE_H

#include <EhentaiApi.h>
#include <QDir>
#include <QSettings>
#include <QStringList>
#include <QtSql>
#include <cinttypes>
#include <functional>
#include <optional>

namespace schema {
// exactly the same as table img_folders
struct ImageFolders {
    int64_t fid; // folder id
    QString folder_path;
    QString title;
    int64_t record_time;
    QString eh_gid;
};

// exactly the same as table cover_images
struct CoverImages {
    int64_t fid;
    QString cover_fname;
    QString cover_base64;
};

// exactly the same as table ehentai_metadata
struct EhentaiMetadata {
    // EH API, type modified
    QString gid;
    QString token;
    QString title;
    QString title_jpn;
    EhCategory category;
    QString thumb;
    QString uploader;
    qlonglong posted; // unix timestamp, second
    qlonglong filecount;
    qlonglong filesize;
    qlonglong expunged; // 1 for true, 0 for false
    double rating;
    // Extra data
    qlonglong meta_updated; // unix timestamp, second, when does the data for this gid is pull
};

// represent a search result, used for display and quick preview
struct FolderPreview {
    int64_t fid;
    QString folder_path;
    QString title;
    int64_t record_time;
    QString cover_base64;
    QString eh_gid;
};

struct EhBackupImport {
    // DOWNLOADS table
    int64_t gid;
    std::string token;
    std::string title;
    std::string title_jpn;
    std::string thumb;
    // https://github.com/seven332/EhViewer/blob/master/app/src/main/java/com/hippo/ehviewer/client/EhConfig.java#L282
    // EhViewer uses bitfields
    EhCategory category;
    std::string posted; // e.g. 2008-04-06 18:13
    std::string uploader;
    double rating;
    std::string simple_language;
    int64_t state;
    int64_t legacy;
    int64_t time;
    std::string label;
    // DOWNLOAD_DIRNAME table
    std::string dirname;
};

} // namespace schema

class EhDbViewerDataStore {
  public:
    static const QString kEhDbViewerOrgName;
    static const QString kEhDbViewerAppName;
    static const QString kDefaultConnectionName;

    static QSettings GetSettings();
    static QString GetSqlitePath();

    static std::optional<QSqlDatabase> OpenDatabase(QString connection_name = kDefaultConnectionName);
    // force open a sqlite db file. i.e. delete the conn if the conn name exists
    static std::optional<QSqlDatabase> OpenDatabase(QString db_path, QString conn_name);
    // create tables if they don't exists, return false if failure
    static bool DbCreateTables(QSqlDatabase &db);

    // return {} if error
    static std::optional<int64_t> DbMaxFid(QSqlDatabase &db);
    static std::optional<QSet<QString>> DbListAllFolders(QSqlDatabase &db);
    static std::optional<QList<schema::FolderPreview>> DbListAllFolderPreviews(QSqlDatabase &db);
    static std::optional<QMap<int64_t, QStringList>> DbListSearchKeywords(QSqlDatabase &db);

    // A result is included if it's matches all in include_kw and none in exclude_kw.
    // When include_kw is empty, then all results will be considered.
    // TODO match mode: regex vs wildcard
    // TODO compatible normalization
    static std::optional<QList<schema::FolderPreview>> DbSearch(QSqlDatabase &db, QStringList include_kw,
                                                                QStringList exclude_kw);

    // querys, return {} if error
    static std::optional<schema::CoverImages> DbQueryCoverImages(QSqlDatabase &db, int64_t fid);
    static std::optional<schema::EhentaiMetadata> DbQueryEhMetaByGid(QSqlDatabase &db, QString gid);
    static std::optional<schema::EhentaiMetadata> DbQueryEhMetaByFid(QSqlDatabase &db, int64_t fid);
    static std::optional<QStringList> DbQueryEhTagsByGid(QSqlDatabase &db, QString gid);

    // return false if error
    static bool DbInsert(QSqlDatabase &db, schema::ImageFolders data);
    static bool DbInsert(QSqlDatabase &db, schema::CoverImages data);
    static bool DbInsert(QSqlDatabase &db, schema::EhentaiMetadata data);
    // require the caller to warp db in a transaction.
    static bool DbInsertReqTransaction(QSqlDatabase &db, const EhGalleryMetadata &data);
    static bool DbReplaceEhTagsReqTransaction(QSqlDatabase &db, QString gid, QStringList tags);

    // the inner function should return true if need submission, or false for rollback
    // the function returns a string if anything is wrong with the transaction.
    static std::optional<QString> DbTransaction(std::function<bool(QSqlDatabase *db)> f,
                                                QString connection_name = kDefaultConnectionName);

    static std::optional<QList<schema::EhBackupImport>> EhBakDbImport(QSqlDatabase *ehdb);

    //
    // Helper functions
    //
    static std::optional<uint64_t> SelectSingleNumber(QSqlQuery *query);
};

#endif // EHDBVIEWERDATASTORE_H
