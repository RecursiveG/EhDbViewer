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
#include <QUrl>
#include <functional>
#include <optional>

#include "EhentaiApi.h"
#include "SearchResultItem.h"
#include "SettingsDialog.h"
#include <variant>

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

    // create database tables
    auto db = EhDbViewerDataStore::OpenDatabase().value();
    EhDbViewerDataStore::DbCreateTables(db);

    // setup search result table
    search_result_model_ = new QStandardItemModel(ui->tableSearchResult);
    ui->history_view->setModel(search_history_model_);
    ui->tableSearchResult->setModel(search_result_model_);
    search_result_model_->setHorizontalHeaderLabels(QStringList() << "Title");
    ui->tableSearchResult->setColumnWidth(0, ui->tableSearchResult->width());

    // setup image preview label
    ui->labelImagePreview->setBackgroundRole(QPalette::Base);
    ui->labelImagePreview->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    ui->labelImagePreview->setScaledContents(true);
    ui->scrollImagePreview->setBackgroundRole(QPalette::Dark);
    ui->scrollImagePreview->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    ui->labelImagePreview->setAlignment(Qt::AlignCenter);

    // TODO
    network_manager_ = new QNetworkAccessManager(this);
    search_history_model_ = new QStandardItemModel(ui->history_view);
    ui->history_view->setModel(search_history_model_);

    // update preview when search result selection changed
    connect(ui->tableSearchResult->selectionModel(), &QItemSelectionModel::selectionChanged,
            [this](const QItemSelection &, const QItemSelection &) { updateDetailsView(); });

    // test
    connect(ui->btnTest1, &QPushButton::clicked, this, &MainWindow::test1);
}

MainWindow::~MainWindow() { delete ui; }

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
    QString db_filepath = QFileDialog::getOpenFileName(this, "Select EhViweer DB file", "Database file (*.db)");
    if (db_filepath.isEmpty()) {
        ui->statusbar->showMessage("Backup file selection cancelled.", 5000);
        return;
    }
    QString download_dir = QFileDialog::getExistingDirectory(this, "Select EhViewer download folder");
    if (download_dir.isEmpty()) {
        ui->statusbar->showMessage("Folder selection cancelled.", 5000);
        return;
    }
    qDebug() << db_filepath;
    qDebug() << download_dir;
    auto msg = DataImporter::ImportEhViewerBackup(db_filepath, download_dir, this);
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

// update image label & metadata textbox according to the tableSearchResult selected index
void MainWindow::updateDetailsView() {
    auto getCurrentItem = [this]() -> SearchResultItem * {
        auto sel = ui->tableSearchResult->selectionModel();
        if (!sel->hasSelection()) {
            // TODO clear
            return nullptr;
        }
        if (sel->selectedRows().size() > 1) {
            // TODO show info
            return nullptr;
        }
        int row = sel->selectedRows()[0].row();
        // qDebug() << "current row is" << row;
        return dynamic_cast<SearchResultItem *>(search_result_model_->item(row));
    };

    auto displayImageLabel = [this](SearchResultItem *item) {
        bool use_thumbnail = ui->cbUseThumbnail->checkState() == Qt::CheckState::Checked;
        if (use_thumbnail) {
            QPixmap pixmap;
            pixmap.loadFromData(QByteArray::fromBase64(item->schema().cover_base64.toUtf8()));
            if (pixmap.isNull()) {
                // TODO error,
                return;
            }
            ui->labelImagePreview->setPixmap(pixmap);
            ui->labelImagePreview->resize(pixmap.size());
        } else {
            ui->labelImagePreview->setText("Loading...");
            ui->labelImagePreview->resize(ui->scrollImagePreview->size());
            QApplication::processEvents();
            // QThread::sleep(1);

            auto path = item->schema().folder_path;
            auto fid = item->schema().fid;
            auto db = EhDbViewerDataStore::OpenDatabase().value();
            auto o_coverimages = EhDbViewerDataStore::DbQueryCoverImages(db, fid);
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
            ui->labelImagePreview->setPixmap(pixmap);
            ui->labelImagePreview->resize(pixmap.size());
        }
    };

    auto updateMetadataDisplay = [this](SearchResultItem *item) {
        auto db = EhDbViewerDataStore::OpenDatabase().value();
        QString title = item->schema().title;
        QString eh_gid;

        if (item->schema().eh_gid.isEmpty()) {
            eh_gid = "N/A";
        } else {
            auto em = EhDbViewerDataStore::DbQueryEhMetaByGid(db, item->schema().eh_gid);
            if (!em) {
                eh_gid = item->schema().eh_gid + " (nodata)";
            } else {
                eh_gid = item->schema().eh_gid + " (ok)";
            }
        }

        QString fmt = "Title: %1\nE-Hentai gid: %2";
        ui->txtMetadataDisplay->setText(fmt.arg(title).arg(eh_gid));
    };

    auto item = getCurrentItem();
    if (!item)
        return;
    displayImageLabel(item);
    updateMetadataDisplay(item);
}

// open the folder
void MainWindow::on_tableSearchResult_doubleClicked(const QModelIndex &index) {
    auto *item = dynamic_cast<SearchResultItem *>(search_result_model_->item(index.row()));
    assert(item);
    auto path = item->schema().folder_path;
    // qDebug() << "selected path: " << path;
    assert(QDesktopServices::openUrl(QUrl::fromLocalFile(path)));
}

// user initiated search. record history, update search results
void MainWindow::on_txtSearchBar_returnPressed() {
    QString query = ui->txtSearchBar->text();
    QStringList inc;
    QStringList exc;
    for (QString s : query.split(" ", Qt::SkipEmptyParts)) {
        if (s.startsWith("-")) {
            if (s.size() > 1)
                exc << s.mid(1);
        } else {
            inc << s;
        }
    }

    auto db = EhDbViewerDataStore::OpenDatabase().value();
    auto data = EhDbViewerDataStore::DbSearch(db, inc, exc);
    if (!data) {
        QMessageBox::warning(this, "EhDbViewer", "Failed to read database");
        return;
    }
    search_result_model_->clear();
    search_result_model_->setHorizontalHeaderLabels({"Title"});
    ui->tableSearchResult->setColumnWidth(0, ui->tableSearchResult->width());
    qInfo() << "Search returned " << data->size() << " results";
    if (data->isEmpty()) {
        QMessageBox::information(this, "EhDbViewer", "No search result");
        return;
    }

    for (const schema::FolderPreview &data : *data) {
        search_result_model_->appendRow(new SearchResultItem(data));
    }

    // search_history_model_->appendRow(new QStandardItem{query});
    // qDebug() << "return pressed";
    // QRegExp regex("abc");
    // qDebug() << regex.indexIn("eabcd");
}

void MainWindow::on_btnOpenSelectedFolder_clicked() {
    auto sel = ui->tableSearchResult->selectionModel();
    if (!sel->hasSelection()) {
        QMessageBox::information(this, "Nothing selected", "Nothing selected");
        return;
    }
    for (const auto &model_index : sel->selectedRows()) {
        int row = model_index.row();
        auto *item = dynamic_cast<SearchResultItem *>(search_result_model_->item(row));
        assert(item);
        auto path = item->schema().folder_path;
        qDebug() << "selected path: " << path;
        assert(QDesktopServices::openUrl(QUrl::fromLocalFile(path)));
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

void MainWindow::on_btnTestListFullDb_clicked() {
    auto db = EhDbViewerDataStore::OpenDatabase().value();
    auto maybe_data = EhDbViewerDataStore::DbListAllFolderPreviews(db);
    if (!maybe_data) {
        QMessageBox::warning(this, "EhDbViewer", "Failed to read database");
        return;
    }
    if (maybe_data->isEmpty()) {
        QMessageBox::information(this, "EhDbViewer", "No data in database");
        return;
    }

    search_result_model_->clear();
    search_result_model_->setHorizontalHeaderLabels({"Title"});
    ui->tableSearchResult->setColumnWidth(0, ui->tableSearchResult->width());
    for (const schema::FolderPreview &data : *maybe_data) {
        search_result_model_->appendRow(new SearchResultItem(data));
    }
}

void MainWindow::test1() {
    // ui->labelImagePreview->setPixmap(QPixmap{});
    // qInfo() << "clicked test1";
    // ui->labelImagePreview->setText("test1");
    // QString s = "{\"abc\":[1,2,3]}";
    QString s = "[1,2,3]";
    auto json_doc = QJsonDocument::fromJson(s.toUtf8());
    qInfo() << json_doc["abc"];
    qInfo() << json_doc["abc"][0];
    qInfo() << json_doc["abc"]["12"];
    qInfo() << json_doc[0];
    qInfo() << json_doc[4];
    qInfo() << json_doc.object()["s"];
    qInfo() << json_doc.array().size();
    qInfo() << json_doc.object().size();
}

// request data online for the selected item
void MainWindow::on_btnTestEhRequest_clicked() {
    auto getCurrentItem = [this]() -> SearchResultItem * {
        auto sel = ui->tableSearchResult->selectionModel();
        if (!sel->hasSelection()) {
            QMessageBox::information(this, "", "Please select at least one");
            return nullptr;
        }
        if (sel->selectedRows().size() > 1) {
            QMessageBox::information(this, "", "Please select at most one");
            return nullptr;
        }
        int row = sel->selectedRows()[0].row();
        auto *item = dynamic_cast<SearchResultItem *>(search_result_model_->item(row));
        if (!item) {
            QMessageBox::warning(this, "", "internal error");
            return nullptr;
        }
        return item;
    };

    qlonglong gid;
    QString token;
    auto fetchToken = [&gid, &token](SearchResultItem *item) {
        gid = 0;
        token = "";
        if (item->schema().eh_gid.isEmpty())
            return;
        auto db = EhDbViewerDataStore::OpenDatabase().value();
        auto em = EhDbViewerDataStore::DbQueryEhMetaByGid(db, item->schema().eh_gid);
        if (em) {
            gid = em->gid.toLongLong();
            token = em->token;
        }
    };

    auto onRequestFinish = [this](std::variant<EhGalleryMetadata, QNetworkReply *> ret) {
        if (ret.index() == 0) {
            QString s = std::get<0>(ret).display();
            ui->txtMetadataDisplay->setText(s);
        } else {
            QMessageBox::warning(this, "", "request failed");
        }
    };

    auto *item = getCurrentItem();
    if (!item)
        return;
    fetchToken(item);
    if (gid == 0 || token == "") {
        QMessageBox::information(this, "", "No gid");
        return;
    }
    EhentaiApi::GalleryMetadata(network_manager_, gid, token, onRequestFinish);
}
