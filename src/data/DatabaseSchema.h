#ifndef DATABASESCHEMA_H
#define DATABASESCHEMA_H

#include <QString>
#include <cinttypes>

#include "EhentaiApi.h"

namespace schema {

struct TableRevision {
    QString table_name;
    int64_t revision;

    static QString TableName() { return "table_revision"; }
    static QString CreationSql() {
        return R"_SQL_(
        create table if not exists table_revision(
            table_name text primary key,
            revision integer not null
        )
        )_SQL_";
    }
};

struct ImageFolders {
    int64_t fid; // folder id
    QString folder_path;
    QString title;
    int64_t record_time;
    QString eh_gid;

    static int SchemaRevision() { return 1; }
    static QString TableName() { return "img_folders"; }
    static QString CreationSql() {
        return R"_SQL_(
        create table if not exists img_folders( -- main table
            fid integer primary key,            -- folder id, auto increment
            folder_path text unique not null,   -- full path
            title text not null,                -- display title
            record_time integer not null,       -- time when this item is created, usually mtime, use ehentai_metadata.posted if possible
            eh_gid text not null                -- empty string means no eh data, foreign key for ehentai_metadata.gid
        )
        )_SQL_";
    }
};

struct CoverImages {
    int64_t fid;
    QString cover_fname;
    QString cover_base64;

    static int SchemaRevision() { return 1; }
    static QString TableName() { return "cover_images"; }
    static QString CreationSql() {
        return R"_SQL_(
        create table if not exists cover_images(        -- table about front cover thumbnail
            fid integer unique not null,  -- foreign key for img_folders.fid
            cover_fname text not null,    -- file name of the cover, as in img_folders[fid].folder_path
            cover_base64 text not null    -- thumbnail data
        )
        )_SQL_";
    }
};

struct FolderTags {
    int64_t fid;
    QString ns;
    QString stem;

    static int SchemaRevision() { return 1; }
    static QString TableName() { return "folder_tags"; }
    static QString CreationSql() {
        return R"_SQL_(
        create table if not exists folder_tags(
            fid integer not null,       -- foreign key for img_folders.fid
            namespace text not null,    -- usually expressed as "namespace:stem"
            stem text not null
        )
        )_SQL_";
    }
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
    qlonglong
        meta_updated; // unix timestamp, second, when does the data for this gid is pull

    static int SchemaRevision() { return 1; }
    static QString TableName() { return "ehentai_metadata"; }
    static QString CreationSql() {
        return R"_SQL_(
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
        )_SQL_";
    }
};

struct EhentaiTags {
    QString gid;
    QString tag;

    static int SchemaRevision() { return 1; }
    static QString TableName() { return "ehentai_tags"; }
    static QString CreationSql() {
        return R"_SQL_(
        create table if not exists ehentai_tags(
            gid text not null,
            tag text not null
        )
        )_SQL_";
    }
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

#endif // DATABASESCHEMA_H
