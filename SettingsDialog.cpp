#include "SettingsDialog.h"
#include "EhDbViewerDataStore.h"
#include "ui_SettingsDialog.h"

SettingsDialog::SettingsDialog(QWidget *parent) : QDialog(parent), ui(new Ui::SettingsDialog) {
    ui->setupUi(this);
    auto settings = EhDbViewerDataStore::GetSettings();
    QString mid = settings.value("ehentai/ipb_member_id", "").toString();
    QString phash = settings.value("ehentai/ipb_pass_hash", "").toString();
    ui->eh_member_id->setText(mid);
    ui->eh_pass_hash->setText(phash);
}

SettingsDialog::~SettingsDialog() { delete ui; }

void SettingsDialog::on_button_box_accepted() {
    auto settings = EhDbViewerDataStore::GetSettings();
    settings.setValue("ehentai/ipb_member_id", ui->eh_member_id->text());
    settings.setValue("ehentai/ipb_pass_hash", ui->eh_pass_hash->text());
    accept();
}

void SettingsDialog::on_button_box_rejected() { reject(); }
