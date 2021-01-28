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
#include "widget/SearchResultItem.h"

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

    // create database tables
    auto db = DataStore::OpenDatabase().value();
    if (!DataStore::DbCreateTables(db))
        QMessageBox::warning(this, "EhDbViewer Error", "Failed to initialize database.");

    // setup search result table
    search_result_model_ = new QStandardItemModel(ui->tableSearchResult);
    ui->history_view->setModel(search_history_model_);
    ui->tableSearchResult->setModel(search_result_model_);
    search_result_model_->setHorizontalHeaderLabels(QStringList() << "Title");
    ui->tableSearchResult->setColumnWidth(0, ui->tableSearchResult->width());

    // setup image preview label
    //    preview_label_ = new AspectRatioLabel(ui->imagePreviewLayoutWidget);
    //    preview_label_->setScaledContents(true);
    //    preview_label_->setMinimumSize(100, 1);
    //    preview_label_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    //    ui->imagePreviewLayout->addWidget(preview_label_);

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
    QStringList db_filepaths = QFileDialog::getOpenFileNames(this, "Select EhViewer DB files", "Database file (*.db)");
    if (db_filepaths.isEmpty()) {
        ui->statusbar->showMessage("Backup file selection cancelled.", 5000);
        return;
    }
    QString download_dir = QFileDialog::getExistingDirectory(this, "Select EhViewer download folder");
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
            ui->labelPreview->setPixmap(pixmap);
        } else {
            ui->labelPreview->setPixmap({});
            ui->labelPreview->setText("Loading...");
            QApplication::processEvents();
            // QThread::sleep(1);

            auto path = item->schema().folder_path;
            auto fid = item->schema().fid;
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

    auto updateMetadataDisplay = [this](SearchResultItem *item) {
        auto display_timestamp = [](int64_t t_s) {
            QDateTime timestamp;
            timestamp.setSecsSinceEpoch(t_s);
            return timestamp.toString("yyyy-MM-dd hh:mm:ss");
        };

        auto db = DataStore::OpenDatabase().value();
        QString display = "<table>";
        auto appendkv = [&display](QString key, QString value, QString alt = "(nodata)") {
            if (value.isEmpty()) {
                display += QString("<tr><td><b>%1:</b></td><td><i>%2</i></td></tr>").arg(key).arg(alt);
            } else {
                display += QString("<tr><td><b>%1:</b></td><td>%2</td></tr>").arg(key).arg(value);
            }
        };

        appendkv("Title", item->schema().title);
        appendkv("RecordTime", display_timestamp(item->schema().record_time));
        appendkv("Ehentai GID", item->schema().eh_gid);

        if (!item->schema().eh_gid.isEmpty()) {
            auto em = DataStore::DbQueryEhMetaByGid(db, item->schema().eh_gid);
            if (!em) {
                appendkv("EhMetadata", "", "(database error)");
            } else {
                appendkv("EhGid", em->gid);
                appendkv("EhToken", em->token);
                appendkv("EhTitle", em->title);
                appendkv("EhTitleJpn", em->title_jpn);
                appendkv("EhCategory#", QString::number(em->category));
                appendkv("EhCategory", QString::fromStdString(EhentaiApi::CategoryToString(em->category)));
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

    auto item = getCurrentItem();
    if (!item)
        return;
    displayImageLabel(item);
    updateMetadataDisplay(item);
}

void MainWindow::searchSimilar(QString base) {
    auto db = DataStore::OpenDatabase().value();
    auto data = DataStore::DbSearchSimilar(db, base);
    if (!data) {
        QMessageBox::warning(this, "EhDbViewer", "Failed to read database");
        return;
    }
    if (data->isEmpty() || data->size() <= 1) {
        QMessageBox::information(this, "EhDbViewer", "Didn't find other similar titles");
        return;
    }
    qInfo() << "Search returned " << data->size() << " results";
    displaySearchResult(*data, QString("Similar to %1").arg(base), true);
    //    search_result_model_->clear();
    //    search_result_model_->setHorizontalHeaderLabels({"Title"});
    //    ui->tableSearchResult->setColumnWidth(0, ui->tableSearchResult->width());
    //    for (const schema::FolderPreview &data : *data) {
    //        search_result_model_->appendRow(new SearchResultItem(data));
    //    }
}

void MainWindow::displaySearchResult(const QList<schema::FolderPreview> &results, const QString &tab_name,
                                     bool in_new_tab) {
    QTableView *table = nullptr;
    int tab_index = -1;
    if (ui->tabSearchResult->count() == 0 || in_new_tab) {
        // create new list view
        table = new QTableView(ui->tabSearchResult);
        tab_index = ui->tabSearchResult->addTab(table, tab_name);
        ui->tabSearchResult->setCurrentIndex(tab_index);
    } else {
        // get current list view
        tab_index = ui->tabSearchResult->currentIndex();
        table = qobject_cast<QTableView *>(ui->tabSearchResult->currentWidget());
        if (table == nullptr) {
            qDebug() << "search result tab containing non-table widget:" << ui->tabSearchResult->currentWidget();
            return;
        }
        auto old_model = table->model();
        table->setModel(nullptr);
        old_model->deleteLater();
    }
    QStandardItemModel *model = new QStandardItemModel(table);
    model->setHorizontalHeaderLabels({"Title"});
    for (const schema::FolderPreview &data : results) {
        model->appendRow(new SearchResultItem(data));
    }
    table->setModel(model);
    table->setColumnWidth(0, table->width());
    table->verticalHeader()->setVisible(false);
    table->setEditTriggers(QAbstractItemView::EditTrigger::NoEditTriggers);

    ui->tabSearchResult->setTabToolTip(tab_index, tab_name);
    ui->tabSearchResult->setTabText(tab_index, tab_name);
}

// open the folder
void MainWindow::on_tableSearchResult_doubleClicked(const QModelIndex &index) {
    auto *item = dynamic_cast<SearchResultItem *>(search_result_model_->item(index.row()));
    assert(item);
    auto path = item->schema().folder_path;
    // qDebug() << "selected path: " << path;
    assert(QDesktopServices::openUrl(QUrl::fromLocalFile(path)));
}

// open the search result context menu
void MainWindow::on_tableSearchResult_customContextMenuRequested(const QPoint &pos) {
    auto buildContextMenu = [this]() {
        QList<SearchResultItem *> selected_items;
        auto sel = ui->tableSearchResult->selectionModel();
        for (const auto &model_index : sel->selectedRows()) {
            int row = model_index.row();
            auto *item = dynamic_cast<SearchResultItem *>(search_result_model_->item(row));
            assert(item);
            selected_items << item;
            qDebug() << "selected item: " << item->schema().title;
        }

        QMenu *menu = new QMenu(this);
        auto *open_dir_action = new QAction("Open in File Explorer", this);
        auto *search_similar_action = new QAction("Search similar title", this);
        menu->addAction(open_dir_action);
        menu->addAction(search_similar_action);

        if (selected_items.size() > 0) {
            connect(open_dir_action, &QAction::triggered, this, [selected_items]() {
                for (auto item : selected_items) {
                    assert(QDesktopServices::openUrl(QUrl::fromLocalFile(item->schema().folder_path)));
                }
            });

        } else {
            open_dir_action->setEnabled(false);
        }

        if (selected_items.size() == 1) {
            const QString &title = selected_items[0]->schema().title;
            connect(search_similar_action, &QAction::triggered, this, [this, title] { this->searchSimilar(title); });
        } else {
            search_similar_action->setEnabled(false);
        }
        return menu;
    };

    buildContextMenu()->popup(ui->tableSearchResult->viewport()->mapToGlobal(pos));
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

    auto db = DataStore::OpenDatabase().value();
    auto data = DataStore::DbSearch(db, inc, exc);
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
    auto db = DataStore::OpenDatabase().value();
    auto maybe_data = DataStore::DbListAllFolderPreviews(db);
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

void MainWindow::test1() {}

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
        auto db = DataStore::OpenDatabase().value();
        auto em = DataStore::DbQueryEhMetaByGid(db, item->schema().eh_gid);
        if (em) {
            gid = em->gid.toLongLong();
            token = em->token;
        }
    };

    auto onRequestFinish = [this](std::variant<EhGalleryMetadata, QNetworkReply *> ret) {
        if (ret.index() == 0) {
            QString s = std::get<0>(ret).display();
            ui->txtMetadataDisplay->setText(s);

            std::optional<QString> transaction_msg = DataStore::DbTransaction([&ret](QSqlDatabase *db) -> bool {
                if (!DataStore::DbInsertReqTransaction(*db, std::get<0>(ret))) {
                    qCritical() << "failed to update eh metadata in db";
                    return false;
                } else {
                    return true;
                }
            });
            if (transaction_msg)
                qCritical() << "db transaction failure:" << *transaction_msg;
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
