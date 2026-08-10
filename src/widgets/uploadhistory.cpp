#include "uploadhistory.h"
#include "./ui_uploadhistory.h"
#include "tools/imgupload/imguploadermanager.h"
#include "utils/history.h"
#include "widgets/uploadlineitem.h"

#include <QDateTime>
#include <QFileInfo>
#include <QPixmap>
#include <QFile>
#include <QPushButton>

void scaleThumbnail(QPixmap& pixmap)
{
    if (pixmap.height() / HISTORYPIXMAP_MAX_PREVIEW_HEIGHT >=
        pixmap.width() / HISTORYPIXMAP_MAX_PREVIEW_WIDTH) {
        pixmap = pixmap.scaledToHeight(HISTORYPIXMAP_MAX_PREVIEW_HEIGHT,
                                       Qt::SmoothTransformation);
    } else {
        pixmap = pixmap.scaledToWidth(HISTORYPIXMAP_MAX_PREVIEW_WIDTH,
                                      Qt::SmoothTransformation);
    }
}

void clearHistoryLayout(QLayout* layout)
{
    if (!layout) {
        return;
    }

    int maxIterations = 1000; // Safety limit to prevent infinite loops
    int iterations = 0;

    while (layout->count() != 0 && iterations < maxIterations) {
        iterations++;

        QLayoutItem* item = layout->takeAt(0);
        if (item) {
            if (item->widget()) {
                QWidget* widget = item->widget();
                // Disconnect all signals to prevent crashes during deletion
                widget->disconnect();
                // Use deleteLater instead of direct delete to avoid crashes
                widget->deleteLater();
            }
            delete item;
        } else {
            break;
        }
    }
}

UploadHistory::UploadHistory(QWidget* parent)
  : QWidget(parent)
  , ui(new Ui::UploadHistory)
{
    ui->setupUi(this);
    setAttribute(Qt::WA_DeleteOnClose);

    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
}

void UploadHistory::loadHistory()
{
    try {
        clearHistoryLayout(ui->historyContainer);
    } catch (...) {
        // Continue anyway - try to recover
    }

    try {
        History history = History();
        QList<QString> historyFiles = history.history();

        if (historyFiles.isEmpty()) {
            setEmptyMessage();
        } else {
            for (const auto& fileName : historyFiles) {
                try {
                    addLine(history.path(), fileName);
                } catch (...) {
                    // Skip problematic files and continue loading others
                    continue;
                }
            }
        }
    } catch (...) {
        // If history loading completely fails, show empty message
        setEmptyMessage();
    }
}

void UploadHistory::setEmptyMessage()
{
    auto* buttonEmpty = new QPushButton;
    buttonEmpty->setText(tr("Screenshots history is empty"));
    buttonEmpty->setMinimumSize(1, HISTORYPIXMAP_MAX_PREVIEW_HEIGHT);
    connect(
      buttonEmpty, &QPushButton::clicked, this, [=, this]() { this->close(); });
    ui->historyContainer->addWidget(buttonEmpty);
}

void UploadHistory::addLine(const QString& path, const QString& fileName)
{
    QString fullFileName = path + fileName;

    // Check if file exists
    if (!QFile::exists(fullFileName)) {
        // Skip non-existent files
        return;
    }

    History history;
    HistoryFileName unpackFileName;

    try {
        unpackFileName = history.unpackFileName(fileName);
    } catch (...) {
        // Skip invalid file names
        return;
    }

    QString url;
    if (unpackFileName.type == "custom") {
        // For custom uploads, the token contains the base64 encoded URL
        // Decode it back to the original URL
        try {
            url = QString::fromUtf8(QByteArray::fromBase64(unpackFileName.token.toUtf8()));
        } catch (...) {
            url = tr("Invalid URL");
        }
    } else {
        // For other types, try to get URL from manager
        try {
            ImgUploaderManager manager(this);
            QString baseUrl = manager.url();
            if (!baseUrl.isEmpty()) {
                url = baseUrl + unpackFileName.file;
            } else {
                url = unpackFileName.file;  // Just use the file name if no base URL
            }
        } catch (...) {
            url = unpackFileName.file;
        }
    }

    // load pixmap
    QPixmap pixmap;
    if (!pixmap.load(fullFileName, "png")) {
        // If failed to load pixmap, create a placeholder
        pixmap = QPixmap(100, 100);
        pixmap.fill(Qt::gray);
    }
    scaleThumbnail(pixmap);

    // get file info
    auto fileInfo = QFileInfo(fullFileName);
    QString lastModified =
      fileInfo.lastModified().toString("yyyy-MM-dd\nhh:mm:ss");

    auto* line = new UploadLineItem(
      this, pixmap, lastModified, url, fullFileName, unpackFileName);

    connect(line, &UploadLineItem::requestedDeletion, this, [=, this]() {
        // Simply reload the entire history after deletion
        // This is safer and avoids any widget management issues
        loadHistory();
    });

    ui->historyContainer->addWidget(line);
}

UploadHistory::~UploadHistory()
{
    delete ui;
}
