#include "TabbedSearchResult.h"

#include <QDesktopServices>
#include <QHeaderView>
#include <QMenu>
#include <QMessageBox>
#include <QModelIndex>
#include <QMouseEvent>
#include <QStandardItemModel>
#include <QTableView>

namespace {
// Wrap FolderPreview into an ListView item.
class SearchResultItem : public QStandardItem {
  public:
    explicit SearchResultItem(schema::FolderPreview data)
        : QStandardItem(), schema_(data) {
        setText(schema_.title);
        QString html =
            QString("<img src=\"data:image/jpeg;base64,%0\">").arg(schema_.cover_base64);
        setToolTip(html);
    }

    const schema::FolderPreview &schema() const { return schema_; }

  private:
    schema::FolderPreview schema_;
};
} // namespace

void MouseHoverAwareTableView::mouseMoveEvent(QMouseEvent *ev) {
    QModelIndex idx = this->indexAt(ev->pos());
    int row = idx.row();
    if (row != previous_hover_row_) {
        emit hoveredIndexChanged(idx);
        previous_hover_row_ = row;
    }
    QTableView::mouseMoveEvent(ev);
}

TabbedSearchResult::TabbedSearchResult(QWidget *parent) : QTabWidget(parent) {
    this->setTabsClosable(true);
    this->setElideMode(Qt::ElideLeft);
    connect(this, &QTabWidget::currentChanged, this, &TabbedSearchResult::onTabChanged);
    connect(this, &QTabWidget::tabCloseRequested, this,
            &TabbedSearchResult::onTabCloseRequested);
}

QString TabbedSearchResult::getSelectedTabQueryString() {
    if (this->count() == 0)
        return QString();
    return this->tabText(this->currentIndex());
}

QList<schema::FolderPreview> TabbedSearchResult::getSelection() {
    if (this->count() == 0) {
        return {};
    }

    QTableView *table = qobject_cast<QTableView *>(this->currentWidget());
    if (table == nullptr) {
        qCritical() << "Invalid table view in TabbedSearchResult:"
                    << this->currentWidget();
        QMessageBox::warning(
            this, "Bug Alert",
            "A bug is detected, please report the console error log to the developer.");
        return {};
    }

    auto *model = qobject_cast<QStandardItemModel *>(table->model());
    if (model == nullptr) {
        qCritical() << "Invalid table model in TabbedSearchResult::getSelection:"
                    << table->model();
        QMessageBox::warning(
            this, "Bug Alert",
            "A bug is detected, please report the console error log to the developer.");
        return {};
    }

    QList<schema::FolderPreview> ret;
    auto *sel = table->selectionModel();
    if (!sel->hasSelection()) {
        return {};
    }
    const auto selected_rows = sel->selectedRows();
    for (const auto &model_index : selected_rows) {
        int row = model_index.row();
        auto *item = dynamic_cast<SearchResultItem *>(model->item(row));
        if (item == nullptr) {
            qCritical() << "Invalid item in TabbedSearchResult::getSelection:"
                        << model->item(row);
            QMessageBox::warning(this, "Bug Alert",
                                 "A bug is detected, please report the console error log "
                                 "to the developer.");
            return {};
        }
        ret << item->schema();
    }
    return ret;
}

void TabbedSearchResult::displaySearchResult(QString query_string,
                                             QList<schema::FolderPreview> results,
                                             bool in_new_tab) {
    auto set_table_model = [&](QTableView *table) {
        QStandardItemModel *model = new QStandardItemModel(table);
        model->setHorizontalHeaderLabels({"Title"});
        for (const schema::FolderPreview &data : results) {
            model->appendRow(new SearchResultItem(data));
        }
        table->setModel(model);
    };

    if (this->count() == 0 || in_new_tab) {
        int insert_index = this->currentIndex() + 1;
        MouseHoverAwareTableView *table = new MouseHoverAwareTableView(this);
        set_table_model(table);

        connect(table, &QTableView::doubleClicked, this,
                &TabbedSearchResult::onTableDoubleClicked);
        connect(table, &QTableView::customContextMenuRequested, this,
                &TabbedSearchResult::onTableContextMenuRequested);
        connect(table->selectionModel(), &QItemSelectionModel::selectionChanged, this,
                &TabbedSearchResult::onTableSelectionChanged);
        connect(table, &MouseHoverAwareTableView::hoveredIndexChanged, this,
                &TabbedSearchResult::onTableHoveredRowChanged);

        table->setContextMenuPolicy(Qt::CustomContextMenu);
        table->verticalHeader()->setVisible(false);
        table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
        table->setEditTriggers(QAbstractItemView::EditTrigger::NoEditTriggers);

        this->setUpdatesEnabled(false);
        int idx = this->insertTab(insert_index, table, query_string);
        this->setTabToolTip(idx, query_string);
        this->setCurrentIndex(idx);
        this->setUpdatesEnabled(true);
    } else {
        int idx = this->currentIndex();
        QTableView *table = qobject_cast<QTableView *>(this->currentWidget());
        if (table == nullptr) {
            qCritical() << "Invalid table view in TabbedSearchResult:"
                        << this->currentWidget();
            QMessageBox::warning(this, "Bug Alert",
                                 "A bug is detected, please report the console error log "
                                 "to the developer.");
            return;
        }
        auto old_model = table->model();
        table->setModel(nullptr);
        old_model->deleteLater();
        set_table_model(table);

        connect(table->selectionModel(), &QItemSelectionModel::selectionChanged, this,
                &TabbedSearchResult::onTableSelectionChanged);

        table->verticalHeader()->setVisible(false);
        table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
        table->setEditTriggers(QAbstractItemView::EditTrigger::NoEditTriggers);

        this->setTabText(idx, query_string);
        this->setTabToolTip(idx, query_string);
        this->setCurrentIndex(idx);
    }
}

void TabbedSearchResult::onTableSelectionChanged(const QItemSelection &,
                                                 const QItemSelection &) {
    auto selected_items = this->getSelection();
    emit selectionChanged(selected_items);
}

void TabbedSearchResult::onTableContextMenuRequested(const QPoint &pos) {
    auto buildContextMenu = [this]() {
        auto selected_items = this->getSelection();

        QMenu *menu = new QMenu(this);
        auto *open_dir_action = new QAction("Open in File Explorer", this);
        auto *search_similar_action = new QAction("Search similar title", this);
        menu->addAction(open_dir_action);
        menu->addAction(search_similar_action);

        if (selected_items.size() > 0) {
            if (selected_items.size() > 100) {
                connect(open_dir_action, &QAction::triggered, this, [this]() {
                    QMessageBox::warning(this, "Not allowed",
                                         "For your property safety, you cannot open more "
                                         "than 100 folders at a time.");
                });
            } else {
                connect(
                    open_dir_action, &QAction::triggered, this, [this, selected_items]() {
                        for (const auto &item : selected_items) {
                            if (!QDesktopServices::openUrl(
                                    QUrl::fromLocalFile(item.folder_path))) {
                                QMessageBox::warning(
                                    this, "Error", "Failed to open " + item.folder_path);
                                break;
                            }
                        }
                    });
            }
        } else {
            open_dir_action->setEnabled(false);
        }

        if (selected_items.size() == 1) {
            const QString &title = selected_items[0].title;
            connect(search_similar_action, &QAction::triggered,
                    [this, title] { emit this->queryRequested("similar_to:" + title); });
        } else {
            search_similar_action->setEnabled(false);
        }
        return menu;
    };

    QTableView *table = qobject_cast<QTableView *>(this->currentWidget());
    if (table == nullptr) {
        qCritical()
            << "Invalid table view in TabbedSearchResult::onTableContextMenuRequested:"
            << this->currentWidget();
        QMessageBox::warning(
            this, "Bug Alert",
            "A bug is detected, please report the console error log to the developer.");
        return;
    }
    buildContextMenu()->popup(table->viewport()->mapToGlobal(pos));
}

void TabbedSearchResult::onTableDoubleClicked(const QModelIndex &) {
    auto selected_items = this->getSelection();

    if (selected_items.size() > 0) {
        if (selected_items.size() > 100) {
            QMessageBox::warning(this, "Not allowed",
                                 "For your property safety, you cannot open more than "
                                 "100 folders at a time.");
        } else {
            for (const auto &item : selected_items) {
                if (!QDesktopServices::openUrl(QUrl::fromLocalFile(item.folder_path))) {
                    QMessageBox::warning(this, "Error",
                                         "Failed to open " + item.folder_path);
                    break;
                }
            }
        }
    }
}

void TabbedSearchResult::onTableHoveredRowChanged(QModelIndex index) {
    if (index.row() < 0) {
        emit hoverChanged({});
        return;
    }
    const auto *model = qobject_cast<const QStandardItemModel *>(index.model());
    if (model == nullptr) {
        emit hoverChanged({});
        return;
    }
    auto *item = dynamic_cast<SearchResultItem *>(model->item(index.row()));
    if (item == nullptr) {
        emit hoverChanged({});
        return;
    }
    emit hoverChanged(item->schema());
}

void TabbedSearchResult::onTabChanged(int) {
    // TODO: which is the "current" index during the currentChanged event?
    // the old one? or the new one?
    if (this->currentIndex() < 0) {
        return;
    }
    emit tabChanged(this->tabText(this->currentIndex()));
    auto selected_items = this->getSelection();
    emit selectionChanged(selected_items);
}

void TabbedSearchResult::onTabCloseRequested(int index) { this->removeTab(index); }
