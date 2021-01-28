#ifndef SEARCHRESULTITEM_H
#define SEARCHRESULTITEM_H

#include "data/DataStore.h"
#include <QStandardItem>

class SearchResultItem : public QStandardItem {
  public:
    explicit SearchResultItem(schema::FolderPreview data) : QStandardItem(), schema_(data) {
        setText(schema_.title);
        QString html = QString("<img src=\"data:image/jpeg;base64,%0\">").arg(schema_.cover_base64);
        setToolTip(html);
    }

    const schema::FolderPreview &schema() const { return schema_; }

  private:
    schema::FolderPreview schema_;
};

#endif // SEARCHRESULTITEM_H
