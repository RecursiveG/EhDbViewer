#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include <QDialog>

namespace Ui {
class SettingsDialog;
}

class SettingsDialog : public QDialog {
    Q_OBJECT

  public:
    explicit SettingsDialog(QWidget *parent = nullptr);
    ~SettingsDialog();

  private slots:

    void on_button_box_accepted();

    void on_button_box_rejected();

  private:
    Ui::SettingsDialog *ui;

    // QDialog interface
};

#endif // SETTINGSDIALOG_H
