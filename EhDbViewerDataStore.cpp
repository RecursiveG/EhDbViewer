#include "EhDbViewerDataStore.h"
#include <QDebug>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QRegExp>
#include <QtSql>
#include <optional>
#include <unordered_set>

using std::optional;

const QString EhDbViewerDataStore::kEhDbViewerOrgName = "EhDbViewer";
const QString EhDbViewerDataStore::kEhDbViewerAppName = "EhDbViewer";
const QString EhDbViewerDataStore::kDefaultConnectionName = "db-conn-default";

QSettings EhDbViewerDataStore::GetSettings() {
    return {QSettings::Format::IniFormat, QSettings::UserScope, kEhDbViewerOrgName, kEhDbViewerAppName};
}

QString EhDbViewerDataStore::GetSqlitePath() {
    auto settings = GetSettings();
    if (!settings.contains("core/db_path")) {
        QDir config_dir = QFileInfo(settings.fileName()).dir();
        config_dir.makeAbsolute();
        settings.setValue("core/db_path", config_dir.filePath("EhDbViewer.db"));
        settings.sync();
    }
    return GetSettings().value("core/db_path").toString();
}

std::optional<QSqlDatabase> EhDbViewerDataStore::OpenDatabase(QString connection_name) {
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

std::optional<QSqlDatabase> EhDbViewerDataStore::OpenDatabase(QString db_path, QString conn_name) {
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

bool EhDbViewerDataStore::DbCreateTables(QSqlDatabase &db) {
#define CREATE_TABLE(table_name, sql)                                                                                  \
    do {                                                                                                               \
        auto result = db.exec(sql);                                                                                    \
        if (result.lastError().type() != QSqlError::NoError) {                                                         \
            qCritical() << "failed to create table " << table_name << " " << result.lastError();                       \
            return false;                                                                                              \
        }                                                                                                              \
        qInfo() << "created table " << table_name;                                                                     \
    } while (0)

    CREATE_TABLE("img_folders", R"_SQL_(
                                create table if not exists img_folders( -- main table
                                    fid integer primary key,            -- folder id, auto increment
                                    folder_path text unique not null,   -- full path
                                    title text not null,                -- display title
                                    eh_gid text not null                -- empty string means no eh data, foreign key for ehentai_metadata.gid
                                )
                                )_SQL_");

    CREATE_TABLE("cover_images", R"_SQL_(
                                create table if not exists cover_images(        -- table about front cover thumbnail
                                    fid integer unique not null,  -- foreign key for img_folders.fid
                                    cover_fname text not null,    -- file name of the cover, as in img_folders[fid].folder_path
                                    cover_base64 text not null    -- thumbnail data
                                )
                                )_SQL_");

    CREATE_TABLE("folder_tags", R"_SQL_(
                                create table if not exists folder_tags(
                                    fid integer not null,       -- foreign key for img_folders.fid
                                    namespace text not null,
                                    stem text not null
                                )
                                )_SQL_");

    CREATE_TABLE("keywords_translation", R"_SQL_(
                                create table if not exists keywords_translation(
                                    keyword text not null,
                                    tag text not null
                                )
                                )_SQL_");

    CREATE_TABLE("translation_suppressed_keywords", R"_SQL_(
                                create table if not exists translation_suppressed_keywords(
                                    fid integer not null,
                                    keyword text not null
                                )
                                )_SQL_");

    CREATE_TABLE("ehentai_tag_suppression", R"_SQL_(
                                create table if not exists ehentai_tag_suppression(
                                    tag text not null, -- <namespace>:<stem>
                                    eh_tag text not null -- <eh_namespace>:<value>
                                )
                                )_SQL_");

    CREATE_TABLE("required_namespaces", R"_SQL_(
                                create table if not exists required_namespaces(
                                    namespace text primary key
                                )
                                )_SQL_");

    CREATE_TABLE("ehentai_metadata", R"_SQL_(
                                create table if not exists ehentai_metadata( -- E-Hentai metadata
                                    gid text primary key,
                                    token text not null,
                                    title text not null,
                                    title_jpn text not null, -- empty string if no jpn title
                                    category text not null, -- category, see EhentaiApi for possible values.
                                    thumb text not null,
                                    uploader text not null, -- uploader name, empty if unknown
                                    posted integer not null, -- unix timestamp second
                                    filecount integer not null, -- negative one for unknown
                                    filesize integer not null, -- negative one for unknown
                                    expunged integer not null, -- 1 for ture, 0 for false
                                    rating real not null,
                                    meta_updated integer not null -- unix timestamp second: when was the metadata updated
                                )
                                )_SQL_");

    CREATE_TABLE("ehentai_tags", R"_SQL_(
                                create table if not exists ehentai_tags(
                                    gid text not null,
                                    tag text not null
                                )
                                )_SQL_");
#undef CREATE_TABLE
    return true;
}

std::optional<int64_t> EhDbViewerDataStore::DbMaxFid(QSqlDatabase &db) {
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

std::optional<QSet<QString>> EhDbViewerDataStore::DbListAllFolders(QSqlDatabase &db) {
    QSet<QString> ret;
    QSqlQuery query{db};
    if (!query.exec("SELECT folder_path FROM img_folders"))
        return {};
    while (query.next()) {
        ret << query.value(0).toString();
    }
    return ret;
}

std::optional<QList<schema::FolderPreview>> EhDbViewerDataStore::DbListAllFolderPreviews(QSqlDatabase &db) {
    QElapsedTimer timer;
    timer.start();
    QList<schema::FolderPreview> ret;
    QSqlQuery query{db};
    if (!query.exec("SELECT img_folders.fid as fid, folder_path, title, cover_base64, eh_gid "
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

std::optional<QMap<int64_t, QStringList>> EhDbViewerDataStore::DbListSearchKeywords(QSqlDatabase &db) {
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
bool MatchesAll(const QStringList &kws, const std::vector<QRegExp> &regex) {
    for (const QString &s : kws) {
        for (const auto &r : regex) {
            if (r.indexIn(s) < 0)
                return false;
        }
    }
    return true;
}
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

std::optional<QList<schema::FolderPreview>> EhDbViewerDataStore::DbSearch(QSqlDatabase &db, QStringList include_kw,
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

optional<schema::CoverImages> EhDbViewerDataStore::DbQueryCoverImages(QSqlDatabase &db, int64_t fid) {
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

std::optional<schema::EhentaiMetadata> EhDbViewerDataStore::DbQueryEhMetaByGid(QSqlDatabase &db, QString gid) {
    QSqlQuery query{db};
    QString sql = "SELECT * FROM ehentai_metadata WHERE gid=?";
    if (!query.prepare(sql)) {
        qCritical() << query.lastError();
        return {};
    }
    query.addBindValue(gid);
    return DbQueryEhMetaInternal(query);
}
std::optional<schema::EhentaiMetadata> EhDbViewerDataStore::DbQueryEhMetaByFid(QSqlDatabase &db, int64_t fid) {
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

std::optional<QStringList> EhDbViewerDataStore::DbQueryEhTagsByGid(QSqlDatabase &db, QString gid) {
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

bool EhDbViewerDataStore::DbInsert(QSqlDatabase &db, schema::ImageFolders data) {
    QSqlQuery query{db};
    if (!query.prepare("INSERT INTO img_folders(fid, folder_path, title, eh_gid) VALUES(?,?,?,?)")) {
        qCritical() << query.lastError();
        return false;
    }
    query.addBindValue(qlonglong(data.fid));
    query.addBindValue(data.folder_path);
    query.addBindValue(data.title);
    query.addBindValue(data.eh_gid);
    bool success = query.exec();
    if (!success)
        qCritical() << query.lastError();
    return success;
}

bool EhDbViewerDataStore::DbInsert(QSqlDatabase &db, schema::CoverImages data) {
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

bool EhDbViewerDataStore::DbInsert(QSqlDatabase &db, schema::EhentaiMetadata data) {
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

std::optional<QString> EhDbViewerDataStore::DbTransaction(std::function<bool(QSqlDatabase *)> f,
                                                          QString connection_name) {
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

std::optional<QList<schema::EhBackupImport>> EhDbViewerDataStore::EhBakDbImport(QSqlDatabase *ehdb) {
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

std::optional<uint64_t> EhDbViewerDataStore::SelectSingleNumber(QSqlQuery *query) {
    if (!query->exec())
        return {};
    if (!query->first())
        return {};
    QVariant var = query->value(0);
    if (!var.canConvert(QMetaType::LongLong))
        return {};
    return var.toLongLong();
}
