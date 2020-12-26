#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "AspectRatioLabel.h"
#include "DataImporter.h"
#include "EhDbViewerDataStore.h"
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

  private slots:
    void on_actionImportFolder_triggered();
    void on_actionImportEhViewerBackup_triggered();
    void on_actionSettings_triggered();

    void on_txtSearchBar_returnPressed();
    void on_tableSearchResult_doubleClicked(const QModelIndex &index);
    void on_tableSearchResult_customContextMenuRequested(const QPoint &pos);
    void on_btnOpenSelectedFolder_clicked();

  private slots:
    void on_btnTestEhRequest_clicked();
    void on_btnTestProgressDiag_clicked();
    void on_btnTestListFullDb_clicked();
    void test1();

  private:
    void updateDetailsView();
    // Search items have title similar to `base`, update list if found.
    void searchSimilar(QString base);

    Ui::MainWindow *ui;
    AspectRatioLabel *preview_label_;
    QStandardItemModel *search_result_model_;
    QStandardItemModel *search_history_model_;
    QNetworkAccessManager *network_manager_;
};
#endif // MAINWINDOW_H
