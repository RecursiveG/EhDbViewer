#include "DataStore.h"

#include <QDebug>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QRegExp>
#include <QtSql>
#include <optional>
#include <unordered_set>

#include "DatabaseSchema.h"
#include "src/FuzzSearcher.h"

using std::optional;

const QString DataStore::kEhDbViewerOrgName = "EhDbViewer";
const QString DataStore::kEhDbViewerAppName = "EhDbViewer";
const QString DataStore::kDefaultConnectionName = "db-conn-default";

QSettings DataStore::GetSettings() {
    return {QSettings::Format::IniFormat, QSettings::UserScope, kEhDbViewerOrgName, kEhDbViewerAppName};
}

QString DataStore::GetSqlitePath() {
    auto settings = GetSettings();
    if (!settings.contains("core/db_path")) {
        QDir config_dir = QFileInfo(settings.fileName()).dir();
        config_dir.makeAbsolute();
        settings.setValue("core/db_path", config_dir.filePath("EhDbViewer.db"));
        settings.sync();
    }
    return GetSettings().value("core/db_path").toString();
}

std::optional<QSqlDatabase> DataStore::OpenDatabase(QString connection_name) {
    if (QSqlDatabase::contains(connection_name)) {
        return QSqlDatabase::database(connection_name);
    } else {
        QString db_path = GetSqlitePath();
        if (db_path.size() <= 0) {
            qCritical() << "Invalid db_path";
            return {};
        }
        auto db = QSqlDatabase::addDatabase("QSQLITE", connection_name);
        if (!db.isValid()) {
            qCritical() << "failed to add QSQLITE database";
            return {};
        }
        db.setDatabaseName(db_path);
        if (!db.open()) {
            qCritical() << "failed to open database";
            return {};
        }
        return db;
    }
}

std::optional<QSqlDatabase> DataStore::OpenDatabase(QString db_path, QString conn_name) {
    if (QSqlDatabase::contains(conn_name)) {
        QSqlDatabase::removeDatabase(conn_name);
    }
    if (db_path.size() <= 0) {
        qCritical() << "Invalid db_path";
        return {};
    }
    auto db = QSqlDatabase::addDatabase("QSQLITE", conn_name);
    if (!db.isValid()) {
        qCritical() << "failed to add QSQLITE database";
        return {};
    }
    db.setDatabaseName(db_path);
    if (!db.open()) {
        qCritical() << "failed to open database";
        return {};
    }
    return db;
}

namespace {

// returns false if table creation failed.
template <typename Schema> bool CreateTable(QSqlDatabase &db) {
    QSqlQuery query{db};
    if (!query.prepare("SELECT revision FROM table_revision WHERE table_name=?")) {
        qCritical() << query.lastError();
        return false;
    }
    query.addBindValue(Schema::TableName());
    if (!query.exec()) {
        qCritical() << query.lastError();
        return false;
    }
    if (!query.isActive()) {
        qCritical() << query.lastError();
        return false;
    }

    if (!query.first()) {
        auto result = db.exec(Schema::CreationSql());
        if (result.lastError().type() != QSqlError::NoError) {
            qCritical() << "Failed to create table" << Schema::TableName() << result.lastError();
            return false;
        }

        QSqlQuery ins{db};
        if (!ins.prepare("INSERT INTO table_revision(table_name, revision) VALUES(?,?)")) {
            qCritical() << ins.lastError();
            return false;
        }
        ins.addBindValue(Schema::TableName());
        ins.addBindValue(Schema::SchemaRevision());
        if (!ins.exec()) {
            qCritical() << ins.lastError();
            return false;
        }
        qInfo() << "Created table" << Schema::TableName();
        return true;
    } else {
        QVariant v = query.value(0);
        if (!v.canConvert(QMetaType::LongLong)) {
            qCritical() << "Invalid table revision value:" << v;
            return false;
        }
        qlonglong rev = v.toLongLong();
        if (rev == Schema::SchemaRevision()) {
            return true;
        } else {
            qCritical() << "Revision mismatch for table" << Schema::TableName();
            return false;
        }
    }
}

template <> bool CreateTable<schema::TableRevision>(QSqlDatabase &db) {
    auto result = db.exec(schema::TableRevision::CreationSql());
    if (result.lastError().type() != QSqlError::NoError) {
        qCritical() << "Failed to create table" << schema::TableRevision::TableName() << result.lastError();
        return false;
    } else {
        qInfo() << "Created table" << schema::TableRevision::TableName();
        return true;
    }
}

} // namespace

bool DataStore::DbCreateTables(QSqlDatabase &db) {
#define CREATE_TABLE(sch_class)                                                                                        \
    do {                                                                                                               \
        if (!CreateTable<::schema::sch_class>(db)) {                                                                   \
            db.rollback();                                                                                             \
            return false;                                                                                              \
        }                                                                                                              \
    } while (0)

    if (!db.transaction()) {
        qCritical() << db.lastError();
        return false;
    }
    CREATE_TABLE(TableRevision);
    CREATE_TABLE(ImageFolders);
    CREATE_TABLE(CoverImages);
    CREATE_TABLE(FolderTags);
    CREATE_TABLE(EhentaiMetadata);
    CREATE_TABLE(EhentaiTags);
    if (!db.commit()) {
        qCritical() << db.lastError();
        db.rollback();
        return false;
    }
    return true;
#undef CREATE_TABLE
}

std::optional<int64_t> DataStore::DbMaxFid(QSqlDatabase &db) {
    QSqlQuery query(db);
    if (!query.prepare("SELECT COUNT(*) FROM img_folders"))
        return {};
    auto count = SelectSingleNumber(&query);
    if (!count)
        return {};
    if (*count == 0)
        return 0;

    if (!query.prepare("SELECT MAX(fid) FROM img_folders"))
        return {};
    count = SelectSingleNumber(&query);
    return count;
}

std::optional<QSet<QString>> DataStore::DbListAllFolders(QSqlDatabase &db) {
    QSet<QString> ret;
    QSqlQuery query{db};
    if (!query.exec("SELECT folder_path FROM img_folders"))
        return {};
    while (query.next()) {
        ret << query.value(0).toString();
    }
    return ret;
}

std::optional<QList<schema::FolderPreview>> DataStore::DbListAllFolderPreviews(QSqlDatabase &db) {
    QElapsedTimer timer;
    timer.start();
    QList<schema::FolderPreview> ret;
    QSqlQuery query{db};
    if (!query.exec("SELECT img_folders.fid as fid, folder_path, title, record_time, cover_base64, eh_gid "
                    "FROM img_folders LEFT JOIN cover_images "
                    "ON img_folders.fid == cover_images.fid")) {
        qCritical() << "select join failed" << query.lastError();
        return {};
    }

    while (query.next()) {
        auto data = schema::FolderPreview{
            .fid = query.value("fid").toLongLong(),
            .folder_path = query.value("folder_path").toString(),
            .title = query.value("title").toString(),
            .record_time = query.value("record_time").toLongLong(),
            .cover_base64 = query.value("cover_base64").toString(),
            .eh_gid = query.value("eh_gid").toString(),
        };
        if (data.folder_path.isEmpty() || data.title.isEmpty() || data.cover_base64.isEmpty()) {
            qCritical() << "failed to parse data row for fid " << data.fid;
            continue;
        }
        ret << data;
    }
    qInfo() << "DbListAllFolderPreviews() completed in " << timer.elapsed() << "ms";
    return ret;
}

std::optional<QMap<int64_t, QStringList>> DataStore::DbListSearchKeywords(QSqlDatabase &db) {
    QElapsedTimer timer;
    timer.start();

    QMap<int64_t, QStringList> ret;
    QSqlQuery query(db);
    QString sql = "";
    // local keywords
    sql = "SELECT if.fid AS fid, namespace||':'||stem AS kw "
          "FROM img_folders AS if INNER JOIN folder_tags AS ft "
          "ON if.fid == ft.fid ";
    // ehentai keywords
    sql += "UNION "
           "SELECT if.fid AS fid, tag AS kw "
           "FROM img_folders AS if INNER JOIN ehentai_tags AS et "
           "ON if.eh_gid == et.gid ";
    // local title
    sql += "UNION SELECT fid, title AS kw FROM img_folders ";
    // ehentai title
    sql += "UNION "
           "SELECT if.fid AS fid, em.title AS kw "
           "FROM img_folders AS if INNER JOIN ehentai_metadata AS em "
           "ON if.eh_gid == em.gid ";
    // ehentai jpn_title
    sql += "UNION "
           "SELECT if.fid AS fid, em.title_jpn AS kw "
           "FROM img_folders AS if INNER JOIN ehentai_metadata AS em "
           "ON if.eh_gid == em.gid ";
    if (!query.exec(sql)) {
        qCritical() << "select join failed" << query.lastError();
        return {};
    }
    while (query.next()) {
        uint64_t fid = query.value("fid").toULongLong();
        QString kw = query.value("kw").toString();
        if (kw.isNull() || kw.length() == 0)
            continue;
        ret[fid] << kw;
    }
    qInfo() << "DbListSearchKeywords() completed in " << timer.elapsed() << "ms";
    // TODO unicode normalize
    return ret;
}

namespace {
// forall re in regex, exists kw in kws s.t. re matches kw
bool MatchesAll(const QStringList &kws, const std::vector<QRegExp> &regex) {
    for (const auto &r : regex) {
        bool has_kw_match = false;
        for (const QString &s : kws) {
            if (r.indexIn(s) >= 0) {
                has_kw_match = true;
                break;
            }
        }
        if (!has_kw_match)
            return false;
    }

    return true;
}

// exists kw in kws, exists re in regex s.t. re matches kw
bool MatchesAny(const QStringList &kws, const std::vector<QRegExp> &regex) {
    for (const QString &s : kws) {
        for (const auto &r : regex) {
            if (r.indexIn(s) >= 0)
                return true;
        }
    }
    return false;
}
} // namespace

std::optional<QList<schema::FolderPreview>> DataStore::DbSearch(QSqlDatabase &db, QStringList include_kw,
                                                                QStringList exclude_kw) {
    std::vector<QRegExp> include_regex;
    std::vector<QRegExp> exclude_regex;
    std::unordered_set<int64_t> selected_fid;
    for (const QString &s : include_kw) {
        QRegExp r{s};
        if (!r.isValid()) {
            qCritical() << "Invalid search regex: " << s;
            return {};
        }
        r.setCaseSensitivity(Qt::CaseInsensitive);
        include_regex.push_back(r);
    }
    for (const QString &s : exclude_kw) {
        QRegExp r{s};
        if (!r.isValid()) {
            qCritical() << "Invalid search regex: " << s;
            return {};
        }
        r.setCaseSensitivity(Qt::CaseInsensitive);
        exclude_regex.push_back(r);
    }
    auto keywords = DbListSearchKeywords(db);
    if (!keywords)
        return {};
    auto all_previews = DbListAllFolderPreviews(db);
    if (!all_previews)
        return {};
    QList<schema::FolderPreview> ret;

    QElapsedTimer timer;
    timer.start();
    for (auto it = keywords->constKeyValueBegin(); it != keywords->constKeyValueEnd(); it++) {
        const auto &fid = it->first;
        const auto &kws = it->second;
        if (MatchesAll(kws, include_regex) && !MatchesAny(kws, exclude_regex))
            selected_fid.insert(fid);
    }
    for (const auto &r : *all_previews) {
        if (selected_fid.find(r.fid) != selected_fid.end()) {
            ret << r;
        }
    }
    qInfo() << "DbSearch() matching and filtering finished in " << timer.elapsed() << "ms";
    return ret;
}

std::optional<QList<schema::FolderPreview>> DataStore::DbSearchSimilar(QSqlDatabase &db, QString title) {
    auto all_previews = DbListAllFolderPreviews(db);
    if (!all_previews)
        return {};
    QList<schema::FolderPreview> ret;
    QElapsedTimer timer;
    timer.start();
    FuzzSearcher searcher;
    ret = searcher.filterMatching<schema::FolderPreview>(*all_previews, title,
                                                         [](const schema::FolderPreview &pv) { return pv.title; });
    qInfo() << "DbSearchSimilar() matching and filtering finished in" << timer.elapsed() << "ms";
    return ret;
}

optional<schema::CoverImages> DataStore::DbQueryCoverImages(QSqlDatabase &db, int64_t fid) {
    QSqlQuery query{db};
    QString sql = "SELECT fid, cover_fname, cover_base64 FROM cover_images WHERE fid=?";
    if (!query.prepare(sql)) {
        qCritical() << query.lastError();
        return {};
    }
    query.addBindValue(qlonglong(fid));
    if (!query.exec()) {
        qCritical() << query.lastError();
        return {};
    }
    while (query.next()) {
        auto data = schema::CoverImages{
            .fid = query.value("fid").toLongLong(),
            .cover_fname = query.value("cover_fname").toString(),
            .cover_base64 = query.value("cover_base64").toString(),
        };
        assert(!query.next());
        return data;
    }
    qWarning() << "no cover_images for fid=" << fid;
    return {};
}

namespace {
std::optional<schema::EhentaiMetadata> DbQueryEhMetaInternal(QSqlQuery &query) {
    if (!query.exec()) {
        qCritical() << query.lastError();
        return {};
    }
    if (query.next()) {
        return schema::EhentaiMetadata{
            .gid = query.value("gid").toString(),
            .token = query.value("token").toString(),
            .title = query.value("title").toString(),
            .title_jpn = query.value("title_jpn").toString(),
            .category = EhentaiApi::CategoryFromString(query.value("category").toString().toStdString())
                            .value_or(EhCategory::UNKNOWN),
            .thumb = query.value("thumb").toString(),
            .uploader = query.value("uploader").toString(),
            .posted = query.value("posted").toLongLong(),
            .filecount = query.value("filecount").toLongLong(),
            .filesize = query.value("filesize").toLongLong(),
            .expunged = query.value("expunged").toLongLong(),
            .rating = query.value("rating").toDouble(),
            .meta_updated = query.value("meta_updated").toLongLong(),
        };
    } else {
        qCritical() << "No ehentai_metadata found";
        return {};
    }
}
} // namespace

std::optional<schema::EhentaiMetadata> DataStore::DbQueryEhMetaByGid(QSqlDatabase &db, QString gid) {
    QSqlQuery query{db};
    QString sql = "SELECT * FROM ehentai_metadata WHERE gid=?";
    if (!query.prepare(sql)) {
        qCritical() << query.lastError();
        return {};
    }
    query.addBindValue(gid);
    return DbQueryEhMetaInternal(query);
}
std::optional<schema::EhentaiMetadata> DataStore::DbQueryEhMetaByFid(QSqlDatabase &db, int64_t fid) {
    QSqlQuery query{db};
    QString sql = "SELECT * FROM ehentai_metadata AS em "
                  "INNER JOIN img_folders AS if "
                  "ON if.eh_gid = em.gid "
                  "WHERE if.fid=?";
    if (!query.prepare(sql)) {
        qCritical() << query.lastError();
        return {};
    }
    query.addBindValue(QString::number(fid));
    return DbQueryEhMetaInternal(query);
}

std::optional<QStringList> DataStore::DbQueryEhTagsByGid(QSqlDatabase &db, QString gid) {
    QElapsedTimer timer;
    timer.start();
    QSqlQuery query{db};
    QString sql = "SELECT tag FROM ehentai_tags WHERE gid=?";
    if (!query.prepare(sql)) {
        qCritical() << query.lastError();
        return {};
    }
    query.addBindValue(gid);
    if (!query.exec()) {
        qCritical() << query.lastError();
        return {};
    }

    QStringList ret;
    while (query.next()) {
        QString tag = query.value(0).toString();
        if (!tag.isEmpty())
            ret << tag;
    }
    qInfo() << "DbQueryEhTagsByGid() completed in " << timer.elapsed() << "ms";
    return ret;
}

bool DataStore::DbInsert(QSqlDatabase &db, schema::ImageFolders data) {
    QSqlQuery query{db};
    if (!query.prepare("INSERT INTO img_folders(fid, folder_path, title, record_time, eh_gid) VALUES(?,?,?,?,?)")) {
        qCritical() << query.lastError();
        return false;
    }
    query.addBindValue(qlonglong(data.fid));
    query.addBindValue(data.folder_path);
    query.addBindValue(data.title);
    query.addBindValue(qlonglong(data.record_time));
    query.addBindValue(data.eh_gid);
    bool success = query.exec();
    if (!success)
        qCritical() << query.lastError();
    return success;
}

bool DataStore::DbInsert(QSqlDatabase &db, schema::CoverImages data) {
    QSqlQuery query{db};
    if (!query.prepare("INSERT INTO cover_images(fid, cover_fname, cover_base64) VALUES(?,?,?)")) {
        qCritical() << query.lastError();
        return false;
    }
    query.addBindValue(qlonglong(data.fid));
    query.addBindValue(data.cover_fname);
    query.addBindValue(data.cover_base64);
    bool success = query.exec();
    if (!success)
        qCritical() << query.lastError();
    return success;
}

bool DataStore::DbInsert(QSqlDatabase &db, schema::EhentaiMetadata data) {
    QSqlQuery query{db};
    QString sql = "INSERT INTO ehentai_metadata("
                  "gid, token, title, title_jpn, category, thumb, uploader, posted, filecount, filesize, expunged, "
                  "rating, meta_updated) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?)";
    if (!query.prepare(sql)) {
        qCritical() << query.lastError();
        return false;
    }
    query.addBindValue(data.gid);
    query.addBindValue(data.token);
    query.addBindValue(data.title);
    query.addBindValue(data.title_jpn);
    query.addBindValue(QString::fromStdString(EhentaiApi::CategoryToString(data.category)));
    query.addBindValue(data.thumb);
    query.addBindValue(data.uploader);
    query.addBindValue(data.posted);
    query.addBindValue(data.filecount);
    query.addBindValue(data.filesize);
    query.addBindValue(data.expunged);
    query.addBindValue(data.rating);
    query.addBindValue(data.meta_updated);
    bool success = query.exec();
    if (!success)
        qCritical() << query.lastError();
    return success;
}

bool DataStore::DbInsertReqTransaction(QSqlDatabase &db, const EhGalleryMetadata &data) {
    schema::EhentaiMetadata d{
        .gid = QString::number(data.gid),
        .token = QString::fromStdString(data.token),
        .title = QString::fromStdString(data.title),
        .title_jpn = QString::fromStdString(data.title_jpn),
        .category = data.category,
        .thumb = QString::fromStdString(data.thumb),
        .uploader = QString::fromStdString(data.uploader),
        .posted = data.posted,
        .filecount = data.filecount,
        .filesize = data.filesize,
        .expunged = data.expunged,
        .rating = data.rating,
        .meta_updated = data.fetched_time,
    };
    QStringList new_tag_list;
    for (const auto &s : data.tags)
        new_tag_list << QString::fromStdString(s);

    QSqlQuery del_query{db};
    if (!del_query.prepare("DELETE FROM ehentai_metadata WHERE gid=?")) {
        qCritical() << del_query.lastError();
        return false;
    }
    del_query.addBindValue(d.gid);
    if (!del_query.exec()) {
        qCritical() << del_query.lastError();
        return false;
    }
    if (!DbInsert(db, d))
        return false;
    return DbReplaceEhTagsReqTransaction(db, d.gid, new_tag_list);
}

bool DataStore::DbReplaceEhTagsReqTransaction(QSqlDatabase &db, QString gid, QStringList tags) {
    QSqlQuery del_query{db};
    if (!del_query.prepare("DELETE FROM ehentai_tags WHERE gid=?")) {
        qCritical() << del_query.lastError();
        return false;
    }
    del_query.addBindValue(gid);
    if (!del_query.exec()) {
        qCritical() << del_query.lastError();
        return false;
    }

    QSqlQuery insert_query{db};
    if (!insert_query.prepare("INSERT INTO ehentai_tags(gid,tag) VALUES(?,?)")) {
        qCritical() << insert_query.lastError();
        return false;
    }
    insert_query.bindValue(0, gid);
    for (auto s : tags) {
        insert_query.bindValue(1, s);
        if (!insert_query.exec()) {
            qCritical() << insert_query.lastError();
            return false;
        }
        if (insert_query.numRowsAffected() != 1) {
            qCritical() << "inserted but only affect" << insert_query.numRowsAffected() << "rows";
            return false;
        }
    }
    return true;
}

std::optional<QString> DataStore::DbTransaction(std::function<bool(QSqlDatabase *)> f, QString connection_name) {
    auto db = OpenDatabase(connection_name).value();
    if (!db.transaction()) {
        return "failed to start transaction";
    }

    bool need_submit;
    try {
        need_submit = f(&db);
    } catch (...) {
        db.rollback();
        return "exception thrown when executing transaction function";
    }

    if (need_submit) {
        if (!db.commit()) {
            return "database transaction commit failed";
        } else {
            return {};
        }
    } else {
        db.rollback();
        return {};
    }
}

std::optional<QList<schema::EhBackupImport>> DataStore::EhBakDbImport(QSqlDatabase *ehdb) {
    QList<schema::EhBackupImport> ret;
    QSqlQuery query{*ehdb};
    QString sql = "SELECT downloads.gid as gid, token, title, title_jpn, thumb, category, posted, uploader, rating,"
                  "       simple_language, state, legacy, time, label, dirname "
                  "FROM downloads LEFT JOIN download_dirname "
                  "ON downloads.gid == download_dirname.gid";
    if (!query.exec(sql)) {
        qCritical() << "failed to read ehdb: " << query.lastError();
        return {};
    }
    while (query.next()) {
        auto data = schema::EhBackupImport{
            .gid = query.value("gid").toLongLong(),
            .token = query.value("token").toString().toStdString(),
            .title = query.value("title").toString().toStdString(),
            .title_jpn = query.value("title_jpn").toString().toStdString(),
            .thumb = query.value("thumb").toString().toStdString(),
            .category =
                EhentaiApi::CategoryFromEhViewerValue(query.value("category").toInt()).value_or(EhCategory::UNKNOWN),
            .posted = query.value("posted").toString().toStdString(),
            .uploader = query.value("uploader").toString().toStdString(),
            .rating = query.value("rating").toDouble(),
            .simple_language = query.value("simple_language").toString().toStdString(),
            .state = query.value("state").toInt(),
            .legacy = query.value("legacy").toInt(),
            .time = query.value("time").toLongLong(),
            .label = query.value("label").toString().toStdString(),
            .dirname = query.value("dirname").toString().toStdString(),
        };
        if (data.gid <= 0 || data.dirname.empty()) {
            qCritical() << "eh backup db data corrupt for gid=" << data.gid;
            continue;
        }
        ret << data;
    }
    return ret;
}

std::optional<uint64_t> DataStore::SelectSingleNumber(QSqlQuery *query) {
    if (!query->exec())
        return {};
    if (!query->first())
        return {};
    QVariant var = query->value(0);
    if (!var.canConvert(QMetaType::LongLong))
        return {};
    return var.toLongLong();
}
