#ifndef TABBEDSEARCHRESULT_H
#define TABBEDSEARCHRESULT_H

#include <QItemSelection>
#include <QTabWidget>

#include "data/DatabaseSchema.h"

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
    void tabChanged(QString current_query_string);
    void queryRequested(QString query);

  private slots:
    void onTableSelectionChanged(const QItemSelection &, const QItemSelection &);
    void onTableContextMenuRequested(const QPoint &pos);
    void onTableDoubleClicked(const QModelIndex &index);
    void onTabChanged(int index);
    void onTabCloseRequested(int index);
};

#endif // TABBEDSEARCHRESULT_H
