#include "MainWindow.h"
#include "ui_MainWindow.h"
#include <QDebug>
#include <QDesktopServices>
#include <QDialog>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonDocument>
#include <QList>
#include <QMessageBox>
#include <QNetworkCookie>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPixmap>
#include <QProgressDialog>
#include <QRegExp>
#include <QTableWidget>
#include <QUrl>
#include <functional>
#include <optional>
#include <variant>

#include "FuzzSearcher.h"
#include "SettingsDialog.h"
#include "data/EhentaiApi.h"
#include "widget/TabbedSearchResult.h"

namespace {
std::optional<QDir> selectDirectory(QWidget *parent, const QString &title) {
    QFileDialog dialog(parent);
    dialog.setOption(QFileDialog::Option::ShowDirsOnly);
    dialog.setFileMode(QFileDialog::FileMode::Directory);
    dialog.setViewMode(QFileDialog::ViewMode::List);
    dialog.setLabelText(QFileDialog::DialogLabel::LookIn, title);
    if (dialog.exec() == QDialog::DialogCode::Accepted) {
        if (dialog.selectedFiles().size() > 0) {
            QDir dir{dialog.selectedFiles()[0]};
            if (dir.exists())
                return dir;
        }
    }
    return {};
}

std::optional<QString> selectSingleFile(QWidget *parent, const QString &title) {
    QFileDialog dialog(parent);
    dialog.setFileMode(QFileDialog::FileMode::ExistingFile);
    dialog.setLabelText(QFileDialog::DialogLabel::LookIn, title);
    if (dialog.exec() == QDialog::DialogCode::Accepted) {
        if (dialog.selectedFiles().size() > 0) {
            return dialog.selectedFiles()[0];
        }
    }
    return {};
}
} // namespace

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), ui(new Ui::MainWindow) {
    ui->setupUi(this);
    // resize splitters, it's magic!
    ui->splitter_main->setSizes({5000, 10000, 1, 10000});
    ui->splitter_result_column->setSizes({5000, 1, 2000});

    network_manager_ = new QNetworkAccessManager(this);

    // create database tables
    auto db = DataStore::OpenDatabase().value();
    if (!DataStore::DbCreateTables(db))
        QMessageBox::warning(this, "EhDbViewer Error", "Failed to initialize database.");

    connect(ui->txtSearchBar, &QLineEdit::returnPressed, this,
            &MainWindow::onSearchBarEnterPressed);
    connect(ui->tabSearchResult, &TabbedSearchResult::tabChanged, this,
            &MainWindow::onSearchResultTabChanged);
    connect(ui->tabSearchResult, &TabbedSearchResult::selectionChanged, this,
            &MainWindow::onSearchResultSelectionChanged);
    connect(ui->tabSearchResult, &TabbedSearchResult::hoverChanged, this,
            &MainWindow::onHoveredItemChanged);

    connect(ui->btnListFullDatabase, &QPushButton::clicked,
            [this] { newSearch("all:"); });
    connect(ui->tabSearchResult, &TabbedSearchResult::queryRequested,
            [this](QString query) {
                if (!query.isEmpty())
                    newSearch(query);
            });
}

MainWindow::~MainWindow() { delete ui; }

void MainWindow::newSearch(QString query) {
    qDebug() << "newSearch():" << query;
    query = query.trimmed();
    if (query.isEmpty()) {
        return;
    }

    auto db = DataStore::OpenDatabase().value();
    std::optional<QList<schema::FolderPreview>> data = {};
    if (query.startsWith("all:", Qt::CaseInsensitive)) {
        // List whole database.
        data = DataStore::DbListAllFolderPreviews(db);
    } else if (query.startsWith("similar_to:", Qt::CaseInsensitive)) {
        // Find similar titles.
        QString base_title = query.mid(QString("similar_to:").size());
        data = DataStore::DbSearchSimilar(db, base_title);
    } else {
        // Search by regex inclusion/exclusion.
        QStringList inc; // requires all include patterns to be matched
        QStringList exc; // no exclude pattern match allowed
        // If not include pattern specified, an implicit ".*" is assumed.

        const auto tmp =
            query.split(" ", Qt::SkipEmptyParts); // You have to write it this way to make
                                                  // clazy happy. Stupid.
        for (const QString &s : tmp) {
            if (s.startsWith("-")) {
                if (s.size() > 1)
                    exc << s.mid(1);
            } else {
                inc << s;
            }
        }
        data = DataStore::DbSearch(db, inc, exc);
    }

    if (!data) {
        QMessageBox::warning(this, "EhDbViewer", "Failed to read database");
        return;
    }
    if (data->isEmpty()) {
        QMessageBox::information(this, "EhDbViewer", "No search result");
        return;
    }
    ui->tabSearchResult->displaySearchResult(query, *data, true);
}

// user initiated search
void MainWindow::onSearchBarEnterPressed() {
    QString query = ui->txtSearchBar->text();
    this->newSearch(query);
}

void MainWindow::onSearchResultTabChanged(QString new_tab_query) {
    if (new_tab_query.isEmpty()) {
        ui->txtSearchBar->setText("");
    } else {
        ui->txtSearchBar->setText(new_tab_query);
    }
}

// update image label & metadata textbox according to "new_selections"
void MainWindow::onSearchResultSelectionChanged(
    QList<schema::FolderPreview> new_selections) {

    auto displayImageLabel = [this](const schema::FolderPreview *item) {
        bool use_thumbnail = ui->cbUseThumbnail->checkState() == Qt::CheckState::Checked;
        if (use_thumbnail) {
            QPixmap pixmap;
            pixmap.loadFromData(QByteArray::fromBase64(item->cover_base64.toUtf8()));
            if (pixmap.isNull()) {
                // TODO error,
                return;
            }
            ui->labelPreview->setPixmap(pixmap);
        } else {
            ui->labelPreview->setPixmap({});
            ui->labelPreview->setText("Loading...");
            QApplication::processEvents();
            // QThread::sleep(1);

            auto path = item->folder_path;
            auto fid = item->fid;
            auto db = DataStore::OpenDatabase().value();
            auto o_coverimages = DataStore::DbQueryCoverImages(db, fid);
            if (!o_coverimages) {
                // TODO error message
                return;
            }
            QString cover_full_path = QDir{path}.filePath(o_coverimages->cover_fname);
            QPixmap pixmap{cover_full_path};
            if (pixmap.isNull()) {
                // TODO error message
                return;
            }
            ui->labelPreview->setPixmap(pixmap);
        }
    };

    auto updateMetadataDisplay = [this](const schema::FolderPreview *item) {
        auto display_timestamp = [](int64_t t_s) {
            QDateTime timestamp;
            timestamp.setSecsSinceEpoch(t_s);
            return timestamp.toString("yyyy-MM-dd hh:mm:ss");
        };

        auto db = DataStore::OpenDatabase().value();
        QString display = "<table>";
        auto appendkv = [&display](QString key, QString value, QString alt = "(nodata)") {
            if (value.isEmpty()) {
                display += QString("<tr><td><b>%1:</b></td><td><i>%2</i></td></tr>")
                               .arg(key)
                               .arg(alt);
            } else {
                display += QString("<tr><td><b>%1:</b></td><td>%2</td></tr>")
                               .arg(key)
                               .arg(value);
            }
        };

        appendkv("Title", item->title);
        appendkv("RecordTime", display_timestamp(item->record_time));
        appendkv("Ehentai GID", item->eh_gid);

        if (!item->eh_gid.isEmpty()) {
            auto em = DataStore::DbQueryEhMetaByGid(db, item->eh_gid);
            if (!em) {
                appendkv("EhMetadata", "", "(database error)");
            } else {
                appendkv("EhGid", em->gid);
                appendkv("EhToken", em->token);
                appendkv("EhTitle", em->title);
                appendkv("EhTitleJpn", em->title_jpn);
                appendkv("EhCategory#", QString::number(em->category));
                appendkv("EhCategory", QString::fromStdString(
                                           EhentaiApi::CategoryToString(em->category)));
                appendkv("EhThumb", em->thumb);
                appendkv("EhUploader", em->uploader);
                appendkv("EhPostedDate", display_timestamp(em->posted));
                appendkv("EhFileCount", QString::number(em->filecount));
                appendkv("EhFilesize", QString::number(em->filesize));
                appendkv("EhExpunged", QString::number(em->expunged));
                appendkv("EhRating", QString::number(em->rating));
                appendkv("MetaUpdated", display_timestamp(em->meta_updated));

                // list tags
                auto tags = DataStore::DbQueryEhTagsByGid(db, em->gid);
                if (!tags) {
                    appendkv("EhTags", "", "(database error)");
                } else {
                    appendkv("EhTags", tags->join(", "), "(no tag)");
                }
            }
        }

        display += "</table>";
        ui->txtMetadataDisplay->setText(display);
    };

    if (new_selections.isEmpty()) {
        ui->txtMetadataDisplay->setText("");
        ui->labelPreview->setText("No image");
    } else if (new_selections.size() == 1) {
        const auto *item = &new_selections.at(0);
        displayImageLabel(item);
        updateMetadataDisplay(item);
    } else {
        ui->txtMetadataDisplay->setText("Err: More than one selected");
        ui->labelPreview->setText("Err: More than one selected");
    }
}

void MainWindow::onHoveredItemChanged(std::optional<schema::FolderPreview> item) {
    if (item) {
        QPixmap pixmap;
        pixmap.loadFromData(QByteArray::fromBase64(item->cover_base64.toUtf8()));
        if (pixmap.isNull()) {
            // TODO error,
            return;
        }
        ui->labelHoverPreview->setPixmap(pixmap);
    } else {
        ui->labelHoverPreview->setText("No preview");
    }
}

void MainWindow::on_actionImportFolder_triggered() {
    auto dir = selectDirectory(this, "Select the folder to import");
    if (dir) {
        auto msg = DataImporter::ImportDir(*dir, this);
        QMessageBox::information(this, "Import directory", msg);
    } else {
        ui->statusbar->showMessage("Folder selection cancelled.", 5000);
    }
}

void MainWindow::on_actionImportEhViewerBackup_triggered() {
    auto settings = DataStore::GetSettings();
    QString db_file_folder = settings.value("history/eh_db_import_folder", "").toString();
    QStringList db_filepaths = QFileDialog::getOpenFileNames(
        this, "Select EhViewer DB files", db_file_folder, "Database file (*.db)");
    if (db_filepaths.isEmpty()) {
        ui->statusbar->showMessage("Backup file selection cancelled.", 5000);
        return;
    }
    db_file_folder = QFileInfo(db_filepaths.at(0)).dir().absolutePath();
    settings.setValue("history/eh_db_import_folder", db_file_folder);

    QString download_dir = QFileDialog::getExistingDirectory(
        this, "Select EhViewer download folder", db_file_folder);
    if (download_dir.isEmpty()) {
        ui->statusbar->showMessage("Folder selection cancelled.", 5000);
        return;
    }
    qDebug() << db_filepaths;
    qDebug() << download_dir;
    auto msg = DataImporter::ImportEhViewerBackup(db_filepaths, download_dir, this);
    QMessageBox::information(this, "Import EhViewer backup", msg);
}

void MainWindow::on_actionSettings_triggered() {
    SettingsDialog settings_dialog{};
    int code = settings_dialog.exec();
    if (code == QDialog::DialogCode::Accepted) {
        qDebug() << "config saved";
    } else {
        qDebug() << "config not saved";
    }
}

//
// TESTING
//

void MainWindow::on_btnTestProgressDiag_clicked() {
    int total = 833;
    QProgressDialog d("Progress...", "Abort", 0, 0, this);
    d.setMinimumDuration(0);
    d.setMinimumWidth(500);
    d.setModal(Qt::WindowModal);

    for (int i = 0; i < total; i++) {
        d.setLabelText(QString("scan folder %1").arg(i));
        d.setValue(0);
        QApplication::processEvents();
        if (d.wasCanceled())
            goto cancelled;
        qInfo() << QString("scan folder %1").arg(i);
        QThread::msleep(10);
    }
    d.setMaximum(total);
    d.setValue(0);
    for (int i = 0; i < total; i++) {
        d.setLabelText(QString("process folder %1").arg(i));
        d.setValue(i + 1);
        if (d.wasCanceled())
            goto cancelled;
        qInfo() << QString("process folder %1").arg(i);
        QThread::msleep(10);
    }
    QMessageBox::information(this, "success", "Success");
    return;
cancelled:
    QMessageBox::information(this, "cancelled", "Cancelled");
}

// request data online for the selected item
void MainWindow::on_btnTestEhRequest_clicked() {
    //    auto getCurrentItem = [this]() -> SearchResultItem * {
    //        auto sel = ui->tableSearchResult->selectionModel();
    //        if (!sel->hasSelection()) {
    //            QMessageBox::information(this, "", "Please select at least one");
    //            return nullptr;
    //        }
    //        if (sel->selectedRows().size() > 1) {
    //            QMessageBox::information(this, "", "Please select at most one");
    //            return nullptr;
    //        }
    //        int row = sel->selectedRows()[0].row();
    //        auto *item = dynamic_cast<SearchResultItem
    //        *>(search_result_model_->item(row)); if (!item) {
    //            QMessageBox::warning(this, "", "internal error");
    //            return nullptr;
    //        }
    //        return item;
    //    };

    //    qlonglong gid;
    //    QString token;
    //    auto fetchToken = [&gid, &token](SearchResultItem *item) {
    //        gid = 0;
    //        token = "";
    //        if (item->schema().eh_gid.isEmpty())
    //            return;
    //        auto db = DataStore::OpenDatabase().value();
    //        auto em = DataStore::DbQueryEhMetaByGid(db, item->schema().eh_gid);
    //        if (em) {
    //            gid = em->gid.toLongLong();
    //            token = em->token;
    //        }
    //    };

    //    auto onRequestFinish = [this](std::variant<EhGalleryMetadata, QNetworkReply *>
    //    ret) {
    //        if (ret.index() == 0) {
    //            QString s = std::get<0>(ret).display();
    //            ui->txtMetadataDisplay->setText(s);

    //            std::optional<QString> transaction_msg =
    //            DataStore::DbTransaction([&ret](QSqlDatabase *db) -> bool {
    //                if (!DataStore::DbInsertReqTransaction(*db, std::get<0>(ret))) {
    //                    qCritical() << "failed to update eh metadata in db";
    //                    return false;
    //                } else {
    //                    return true;
    //                }
    //            });
    //            if (transaction_msg)
    //                qCritical() << "db transaction failure:" << *transaction_msg;
    //        } else {
    //            QMessageBox::warning(this, "", "request failed");
    //        }
    //    };

    //    auto *item = getCurrentItem();
    //    if (!item)
    //        return;
    //    fetchToken(item);
    //    if (gid == 0 || token == "") {
    //        QMessageBox::information(this, "", "No gid");
    //        return;
    //    }
    //    EhentaiApi::GalleryMetadata(network_manager_, gid, token, onRequestFinish);
}
