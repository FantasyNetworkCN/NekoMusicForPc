#pragma once

#include "ui/externalimportdialog.h"

class QishuiImportDialog : public ExternalImportDialog
{
    Q_OBJECT

public:
    explicit QishuiImportDialog(ApiClient *apiClient, QWidget *parent = nullptr);

protected:
    QString parseInput(const QString &input) const override;
    void fetchPlaylist(const QString &id, FetchCallback cb) override;
};
