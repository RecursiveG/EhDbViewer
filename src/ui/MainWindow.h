#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "data/DataImporter.h"
#include "data/DataStore.h"
#include "widget/AspectRatioLabel.h"

#include <QMainWindow>
#include <QNetworkAccessManager>
#include <QStandardItemModel>
#include <memory>

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow {
    Q_OBJECT

  public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    void newSearch(QString query);

  private slots:
    void onSearchBarEnterPressed();
    void onSearchResultTabChanged(QString new_tab_query);
    void onSearchResultSelectionChanged(QList<schema::FolderPreview> new_selections);
    void onHoveredItemChanged(std::optional<schema::FolderPreview> item);

  private slots:
    void on_actionImportFolder_triggered();
    void on_actionImportEhViewerBackup_triggered();
    void on_actionSettings_triggered();

  private slots:
    void on_btnTestEhRequest_clicked();
    void on_btnTestProgressDiag_clicked();

  private:
    Ui::MainWindow *ui;
    QNetworkAccessManager *network_manager_;
};
#endif // MAINWINDOW_H
