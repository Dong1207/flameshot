#include "uploadlineitem.h"
#include "./ui_uploadlineitem.h"
#include "src/core/flameshotdaemon.h"
#include "src/tools/imgupload/imguploadermanager.h"
#include "src/utils/confighandler.h"
#include "src/utils/history.h"
#include "src/widgets/notificationwidget.h"

#include <QDesktopServices>
#include <QFileInfo>
#include <QMessageBox>
#include <QUrl>
#include <QWidget>

void removeCacheFile(QString const& fullFileName)
{
    QFile file(fullFileName);
    if (file.exists()) {
        file.remove();
    }
}

UploadLineItem::UploadLineItem(QWidget* parent,
                               QPixmap const& preview,
                               QString const& timestamp,
                               QString const& url,
                               QString const& fullFileName,
                               HistoryFileName const& unpackFileName)
  : QWidget(parent)
  , ui(new Ui::UploadLineItem)
{
    ui->setupUi(this);
    setAttribute(Qt::WA_DeleteOnClose);
    ui->imagePreview->setPixmap(preview);
    ui->uploadTimestamp->setText(timestamp);

    connect(ui->copyUrl, &QPushButton::clicked, this, [=, this]() {
        FlameshotDaemon::copyToClipboard(url);
    });

    connect(ui->openBrowser, &QPushButton::clicked, this, [=, this]() {
        QDesktopServices::openUrl(QUrl(url));
    });

    connect(ui->deleteImage, &QPushButton::clicked, this, [=, this]() {
        QString confirmMessage;
        if (unpackFileName.type == "custom") {
            confirmMessage = tr("Are you sure you want to delete this screenshot from "
                               "local history? (Note: Cannot delete from server for custom uploads)");
        } else {
            confirmMessage = tr("Are you sure you want to delete a screenshot from the "
                               "latest uploads and server?");
        }

        if (ConfigHandler().historyConfirmationToDelete() &&
            QMessageBox::No ==
              QMessageBox::question(
                this,
                tr("Confirm to delete"),
                confirmMessage,
                QMessageBox::Yes | QMessageBox::No)) {
            return;
        }

        // Try to delete from server if it's not a custom upload
        if (unpackFileName.type != "custom" && !unpackFileName.type.isEmpty()) {
            try {
                ImgUploaderManager manager(this);
                ImgUploaderBase* imgUploaderBase = manager.uploader(unpackFileName.type);
                if (imgUploaderBase) {
                    imgUploaderBase->deleteImage(unpackFileName.file, unpackFileName.token);
                }
            } catch (...) {
                // Ignore server deletion errors
            }
        }

        // Try to remove the cache file
        try {
            removeCacheFile(fullFileName);
        } catch (...) {
            // Ignore file deletion errors
        }

        // Always emit the deletion signal to trigger reload
        emit requestedDeletion();
    });
}

UploadLineItem::~UploadLineItem()
{
    delete ui;
}