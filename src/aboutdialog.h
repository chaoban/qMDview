#pragma once

#include <QDialog>

// 說明 → 關於 qMDView：SAISAI.Dev logo、版本、著作權與第三方授權聲明
class AboutDialog : public QDialog
{
    Q_OBJECT

public:
    explicit AboutDialog(QWidget *parent = nullptr);

private:
    void showLicenses();
};
