#ifndef TABBEDSEARCHRESULT_H
#define TABBEDSEARCHRESULT_H

#include <optional>

#include <QItemSelection>
#include <QTabWidget>
#include <QTableView>

#include "data/DatabaseSchema.h"

class MouseHoverAwareTableView : public QTableView {
    Q_OBJECT
  public:
    MouseHoverAwareTableView(QWidget *parent) : QTableView(parent) {
        this->setMouseTracking(true);
    }
    void mouseMoveEvent(QMouseEvent *ev) override;
  signals:
    void hoveredIndexChanged(QModelIndex new_index);

  private:
    int previous_hover_row_ = -1;
};

class TabbedSearchResult : public QTabWidget {
    Q_OBJECT
  public:
    TabbedSearchResult(QWidget *parent);

    // returns Null string if there's no tab
    QString getSelectedTabQueryString();
    // maybe empty
    QList<schema::FolderPreview> getSelection();

  public slots:
    void displaySearchResult(QString query_string, QList<schema::FolderPreview> results,
                             bool in_new_tab);

  signals:
    void selectionChanged(QList<schema::FolderPreview> selected);
    void hoverChanged(std::optional<schema::FolderPreview> hovered);
    void tabChanged(QString current_query_string);
    void queryRequested(QString query);

  private slots:
    void onTableSelectionChanged(const QItemSelection &, const QItemSelection &);
    void onTableContextMenuRequested(const QPoint &pos);
    void onTableDoubleClicked(const QModelIndex &index);
    void onTableHoveredRowChanged(QModelIndex idx);
    void onTabChanged(int index);
    void onTabCloseRequested(int index);
};

#endif // TABBEDSEARCHRESULT_H
