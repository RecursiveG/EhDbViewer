#include "DataImporter.h"

#include <QApplication>
#include <QBuffer>
#include <QColorSpace>
#include <QDebug>
#include <QImage>
#include <QLabel>
#include <QtSql>

#include "DataStore.h"
#include <map>

const QString DataImporter::kNoImageBase64 =
    "iVBORw0KGgoAAAANSUhEUgAAAGQAAABkCAYAAABw4pVUAAAABmJLR0QA/wD/"
    "AP+"
    "gvaeTAAAEJElEQVR4nO3dv49MURTA8e8Ku4kIiUKCRKHRaFQqlYqCjdiQkCgp7Cr5Eyh1dHTb6VSikbAaK1H"
    "QiFixiSAhg8KuXcWbiTFz38977jtnZ"
    "s43ec3uvDt35pP33sydNcDzPM/z5JoDvgAfgVnluUx8U8BnYLO7rQHnVGfk8Yl/"
    "II5ioDNkCI5iqLMMo6wDFzUnNek5isEcxWCOYjBHMZijKLUJvAb2Bn4XQvEtbiutd0NHMQbiKAZBHMUgSFF+"
    "oa9fUhBwlLolBwFHqVMrIOAoVWsNBBylSq2CgKOU1ToIOEpRKiDgKHmpgYCjhFIFAUcZTB0EHKU/"
    "EyDgKL3MgED2p0RW/8RoBlgAloAf3W0JmO/"
    "+TipTIGATZT/"
    "wkuHH3tuWu7eRyBwI2EKZoRijH0XiSDEJAnZQFijH6G1XBe7PLAjYQHlOdZBnAvdnGgT0X311qA7SEbg/"
    "8yCgi1IH5LvA/"
    "Y0ECOidvvyUVZAGyjzVQcb6oj4F7Az8vG2UGbKXtGUYy8C0wP2ZBNkC3AFeAXsCv28bZT/"
    "FKGP9xnAbsNg3vhWUabJT0jOyC30HeNr9mcSR0csUyHbgYeA+Hufc3sL7FOnMgOwCngTGXwUOF+yn/"
    "T5FOhMge4AXgbHfAgcr7D9OKOogB4A3gXFfAftqjDMuKKogh4D3gTGXgN0NxhsHFDWQIwx/"
    "CcEm8AjYETHuqKOogBwDvgXGeoDMZwqjjNI6yEngZ2Cce8DWBuPlNaoorYKcB34HxrhFtlQineT7lFngQ3dL"
    "+"
    "S1IrYFcBv4E9r9ZY4wmSRwpcwNjpDzSWgG5DmwM7LcBXKs52abFoAxipEZJCjJFdgQM7rMGXGo238Y1QcnDS"
    "ImSDGQLcDdw+"
    "19kF3aN6lxT8m6beu0sCcg0/6/Y9rYOcDxuvtFVOVJCR8Y6cKHi/"
    "jGJg+St2H4FjsbPV6SiJ7UIo8r+"
    "sYmC5K3YrpAtk1gq9pSUaulfDCR2xVajsi83GDwyquwfe6SIgEit2GqUh1KGUbR/"
    "DEo0SN6K7XOardhqFPukSqJEg4RWbB+SXdxHqdhrgtQ1JRpkcFtE9kP/"
    "VIUWMi2giILcR3bFNlVzZNe3qt9i1ObpSwzkNmlWbKXrf59R56ul2kKJBtkAblScqHahU8pLbJ2+"
    "okGuVJygdrPUf4I0UKJBRqUPNHti20aZSJC6r3zaRJkYkBNkKCs0+"
    "wi2LZSJAZGojVdfDlKz1CgO0qCUKA7SsFTXFAeJKMWR4iCRpUYpzUGGS4lSmoOES4VSmoPkJ3GhdxDh1D8P8"
    "YZT/TzEC9cUJRrEt7T/qU1p2g/"
    "e6pYKpTTtB255S4FSmvaDtr5Jo3gJG9V/"
    "iDrWOYrBHMVgjmIwRzGYoxgsb5X4tOakJr0QyorqjLwhlFXd6XiQnabekR0dp5Tn4nme541RfwFbbDokN3Pz"
    "agAAAABJRU5ErkJggg==";

QString DataImporter::GenerateImgThumbnail(const QString &file_path) {
    QByteArray byte_arr;
    QBuffer buffer(&byte_arr);
    buffer.open(QIODevice::WriteOnly);

    QImage image(file_path);
    if (image.isNull())
        return "";
    if (image.width() > image.height()) {
        image = image.scaled(320, 200, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    } else {
        image = image.scaled(200, 320, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }

    if (image.isNull())
        return "";
    image.convertToColorSpace(QColorSpace::SRgb);

    if (!image.save(&buffer, "JPG", 85))
        return "";
    auto base64 = byte_arr.toBase64();
    // qDebug() << "thumbnail base64 size " << base64.size() / 1024 << "KB";
    return base64;
}

std::optional<QString> DataImporter::ScanFolder(const QDir &dir,
                                                std::vector<FolderData> *output_list,
                                                QProgressDialog *progress) {
    if (progress->wasCanceled())
        return "User cancelled";

    std::optional<QFileInfo> pic_info{};
    bool has_folder = false;

    const auto stupid_qt = dir.entryInfoList(QDir::NoDotAndDotDot | QDir::AllEntries);
    for (const auto &info : stupid_qt) {
        if (info.isFile()) {
            if (pic_info)
                continue;
            auto ext = info.suffix().toLower();
            if (ext == "jpg" || ext == "png" || ext == "jpeg") {
                pic_info = info;
            }
        } else if (info.isDir()) {
            auto err = ScanFolder({info.absoluteFilePath()}, output_list, progress);
            if (err)
                return err;
            has_folder = true;
        }
    }

    //    if (pic_info && has_folder) {
    //        return QString("Unable to process folder containing both picture and
    //        subfolder: %1").arg(dir.absolutePath());
    //    }

    if (pic_info) {
        output_list->push_back({.folder = dir,
                                .cover_fname = pic_info->fileName(),
                                .thumb_base64 = "",
                                .record_time = pic_info->lastModified()});
        progress->setLabelText(QString("Scanning folder: %1").arg(dir.dirName()));
        // qInfo() << QString("Scanning folder: %1").arg(dir.dirName());
        QApplication::processEvents();
    }
    return {};
}

QString DataImporter::ImportDir(QDir dir, QWidget *parent) {
    QString ret = "";
    auto transaction_msg = DataStore::DbTransaction([dir, parent,
                                                     &ret](QSqlDatabase *db) -> bool {
        // progress dialog
        QProgressDialog progress("", "Abort", 0, 0, parent);
        auto label = new QLabel();
        label->setAlignment(Qt::AlignLeft);
        progress.setLabel(label);

        progress.setMinimumDuration(0);
        progress.setMinimumWidth(500);
        progress.setModal(Qt::WindowModal);
        progress.setValue(0);
        progress.setLabelText("Scanning folders...");
        QApplication::processEvents();

        // scan dir tree -> (folder_path, img_path)
        std::vector<FolderData> discovered_folders;
        auto err = ScanFolder(dir, &discovered_folders, &progress);
        if (err) {
            ret = QString("Error: %1").arg(*err);
            return false;
        }

        // filter out folders that's already in database
        auto db_folders = DataStore::DbListAllFolders(*db);
        if (!db_folders) {
            ret = "Error: DbListAllFolders() failed";
            return false;
        }
        discovered_folders.erase(
            std::remove_if(discovered_folders.begin(), discovered_folders.end(),
                           [&db_folders](const FolderData &d) {
                               return db_folders->contains(d.folder.absolutePath());
                           }),
            discovered_folders.end());

        // process files -> (folder_path, img_path, bas64)
        progress.setMaximum(discovered_folders.size());
        progress.setValue(0);
        for (size_t idx = 0; idx < discovered_folders.size(); idx++) {
            if (progress.wasCanceled()) {
                ret = QString("Error: user cancelled");
                return false;
            }
            auto &folder = discovered_folders[idx];
            folder.thumb_base64 =
                GenerateImgThumbnail(folder.folder.filePath(folder.cover_fname));
            progress.setLabelText(
                QString("Generating thumbnail: %1").arg(folder.folder.dirName()));
            progress.setValue(idx);
        }

        // insert to db
        auto count = DataStore::DbMaxFid(*db);
        if (!count) {
            qCritical() << "failed to query max fid from main db";
            ret = "Error: database err";
            return false;
        }

        int64_t next_fid = *count + 1;
        for (const auto &folder : discovered_folders) {
            int64_t fid = next_fid++;

            if (!DataStore::DbInsert(
                    *db, schema::ImageFolders{
                             .fid = fid,
                             .folder_path = folder.folder.absolutePath(),
                             .title = folder.folder.dirName(),
                             .record_time = folder.record_time.currentSecsSinceEpoch(),
                             .eh_gid = ""})) {
                ret = "Error: failed to insert to db";
                return false;
            }

            if (!DataStore::DbInsert(*db,
                                     schema::CoverImages{
                                         .fid = fid,
                                         .cover_fname = folder.cover_fname,
                                         .cover_base64 = (folder.thumb_base64.size() == 0
                                                              ? kNoImageBase64
                                                              : folder.thumb_base64),
                                     })) {
                ret = "Error: failed to insert to db";
                return false;
            }
        }
        ret = QString("Import complete: %1 new folders imported")
                  .arg(discovered_folders.size());
        return true;
    });

    if (transaction_msg) {
        return QString("Error: %1").arg(*transaction_msg);
    } else {
        return ret;
    }
}

void DataImporter::SelectThumbnailInFolder(QString dir, QString *filename_out,
                                           QString *filepath_out) {
    *filename_out = "";
    *filepath_out = "";
    const auto file_infos =
        QDir{dir}.entryInfoList(QDir::NoDotAndDotDot | QDir::AllEntries);
    for (const auto &info : file_infos) {
        if (info.isFile()) {
            auto ext = info.suffix().toLower();
            if (ext == "jpg" || ext == "png" || ext == "jpeg") {
                *filename_out = info.fileName();
                *filepath_out = info.absoluteFilePath();
                return;
            }
        }
    }
}

// Convert EhViewer time value "2008-08-10 03:29" to timestamp
int64_t EhViewerTimeToStamp(QString str) {
    QDateTime time = QDateTime::fromString(str, "yyyy-MM-dd hh:mm");
    if (time.isValid()) {
        return time.toSecsSinceEpoch();
    } else {
        qWarning() << "invalid timestamp in ehviewer db:" << str;
        return -1;
    }
}

QString DataImporter::ImportEhViewerBackup(QStringList db_files, QDir download_dir,
                                           QWidget *parent) {
    // sanity check
    qDebug() << "worker running...";
    if (!download_dir.exists()) {
        return "invalid download dir";
    }

    // load all ehviewer db entries
    QMap<int64_t, schema::EhBackupImport> ehv_entries;
    for (const QString &db_file : qAsConst(db_files)) {
        QString fault_message = "";
        {
            std::optional<QSqlDatabase> ehdb =
                DataStore::OpenDatabase(db_file, "ehviewer-db-import");
            if (!ehdb) {
                return "failed to open eh db file";
            }

            // read from backup
            const auto ehdb_data = DataStore::EhBakDbImport(&*ehdb);
            if (!ehdb_data) {
                fault_message = "Error: failed to read ehviewer database";
            } else {
                for (const schema::EhBackupImport &entry : *ehdb_data) {
                    ehv_entries.insert(entry.gid, entry);
                }
            }
            ehdb->close();
        }
        QSqlDatabase::removeDatabase("ehviewer-db-import");
        if (!fault_message.isEmpty())
            return fault_message;
    }

    QString ret;
    auto transaction_err = DataStore::DbTransaction(
        [parent, &ret, &ehv_entries, download_dir](QSqlDatabase *db) -> bool {
            // progress dialog
            QProgressDialog progress("", "Abort", 0, 0, parent);
            auto label = new QLabel();
            label->setAlignment(Qt::AlignLeft);
            progress.setLabel(label);

            progress.setMinimumDuration(0);
            progress.setMinimumWidth(500);
            progress.setModal(Qt::WindowModal);
            progress.setValue(0);
            progress.setLabelText("Importing backup");
            QApplication::processEvents();

            // filter out records if it's already in db
            auto db_folders = DataStore::DbListAllFolders(*db);
            if (!db_folders) {
                ret = "Error: DbListAllFolders() failed";
                return false;
            }
            std::vector<schema::EhBackupImport> new_entries;
            for (const schema::EhBackupImport &eh_data : qAsConst(ehv_entries)) {
                QDir dir{download_dir.filePath(QString::fromStdString(eh_data.dirname))};
                if (!db_folders->contains(dir.absolutePath()))
                    new_entries.push_back(eh_data);
            }
            progress.setMaximum(new_entries.size());
            progress.setValue(0);

            // insert to db
            auto count = DataStore::DbMaxFid(*db);
            if (!count) {
                qCritical() << "failed to query max fid from main db";
                ret = "Error: database err";
                return false;
            }

            int64_t next_fid = *count + 1;
            int64_t processed = 0;
            for (schema::EhBackupImport &eh_data : new_entries) {
                if (progress.wasCanceled()) {
                    ret = "Error: user cancelled";
                    return false;
                }

                QDir dir{download_dir.filePath(QString::fromStdString(eh_data.dirname))};
                if (!dir.exists()) {
                    ret = QString("Dir %1 not exists")
                              .arg(QString::fromStdString(eh_data.dirname));
                    return false;
                }

                // generate thumbnail
                QString filename;
                QString filepath;
                QString thumb_base64;
                SelectThumbnailInFolder(dir.absolutePath(), &filename, &filepath);
                if (!filename.isEmpty()) {
                    thumb_base64 = DataImporter::GenerateImgThumbnail(filepath);
                }
                if (thumb_base64.isEmpty())
                    thumb_base64 = kNoImageBase64;

                // insert img_folder
                qlonglong this_fid = next_fid++;

                std::string title =
                    eh_data.title_jpn.empty() ? eh_data.title : eh_data.title_jpn;
                if (!DataStore::DbInsert(*db,
                                         schema::ImageFolders{
                                             .fid = this_fid,
                                             .folder_path = dir.absolutePath(),
                                             .title = QString::fromStdString(title),
                                             .record_time = EhViewerTimeToStamp(
                                                 QString::fromStdString(eh_data.posted)),
                                             .eh_gid = QString::number(eh_data.gid),
                                         })) {
                    ret = "failed to insert to img_folders";
                    return false;
                }

                if (!DataStore::DbInsert(
                        *db, schema::CoverImages{.fid = this_fid,
                                                 .cover_fname = filename,
                                                 .cover_base64 = thumb_base64})) {
                    ret = "failed to insert to cover_images";
                    return false;
                }

                if (!DataStore::DbInsert(
                        *db, schema::EhentaiMetadata{
                                 .gid = QString::number(eh_data.gid),
                                 .token = QString::fromStdString(eh_data.token),
                                 .title = QString::fromStdString(eh_data.title),
                                 .title_jpn = QString::fromStdString(eh_data.title_jpn),
                                 .category = eh_data.category,
                                 .thumb = QString::fromStdString(eh_data.thumb),
                                 .uploader = QString::fromStdString(eh_data.uploader),
                                 .posted = EhViewerTimeToStamp(
                                     QString::fromStdString(eh_data.posted)),
                                 .filecount = 0,
                                 .filesize = 0,
                                 .expunged = -1,
                                 .rating = eh_data.rating,
                                 .meta_updated = 0,
                             })) {
                    ret = "failed to insert to ehentai_metadata";
                    return false;
                }

                progress.setLabelText(
                    QString("Importing: %1").arg(QString::fromStdString(title)));
                progress.setValue(++processed);
            }

            ret = QString("Import complete: %1 new folders are imported")
                      .arg(new_entries.size());
            return true;
        });

    if (transaction_err) {
        return QString("Error: %1").arg(*transaction_err);
    } else {
        return ret;
    }
}
