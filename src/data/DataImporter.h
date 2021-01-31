#ifndef DATAIMPORTER_H
#define DATAIMPORTER_H

#include <QDateTime>
#include <QDir>
#include <QProgressDialog>
#include <QString>
#include <QStringList>

#include <optional>
#include <vector>

class DataImporter {
  public:
    static const QString kNoImageBase64;
    // return empty string if fail
    static QString GenerateImgThumbnail(const QString &file_path);

    // Import may success or fail. Return the final message
    static QString ImportDir(QDir dir, QWidget *parent);
    static QString ImportEhViewerBackup(QStringList db_files, QDir download_dir,
                                        QWidget *parent);

  protected:
    struct FolderData {
        QDir folder;
        QString cover_fname;
        QString thumb_base64;
        QDateTime record_time;
    };

    // Scan dir, add all image dirs into output_list, also update progress.label
    // Return a string if any errors occur.
    static std::optional<QString> ScanFolder(const QDir &dir,
                                             std::vector<FolderData> *output_list,
                                             QProgressDialog *progress);
    static void SelectThumbnailInFolder(QString dir, QString *filename_out,
                                        QString *filepath_out);
};

#endif // DATAIMPORTER_H
