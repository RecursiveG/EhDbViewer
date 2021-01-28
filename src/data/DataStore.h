#ifndef DATASTORE_H
#define DATASTORE_H

#include <QDir>
#include <QSettings>
#include <QStringList>
#include <QtSql>
#include <cinttypes>
#include <functional>
#include <optional>

#include "DatabaseSchema.h"
#include "EhentaiApi.h"

class DataStore {
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
    // Search all folders that are similar to `title`
    static std::optional<QList<schema::FolderPreview>> DbSearchSimilar(QSqlDatabase &db, QString title);

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

#endif // DATASTORE_H
