// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2017-2019 Alejandro Sirgo Rica & Contributors

#include "imguploaderbase.h"
#include "src/core/flameshotdaemon.h"
#include "src/utils/confighandler.h"
#include "src/utils/globalvalues.h"
#include "src/utils/history.h"
#include "src/utils/screenshotsaver.h"
#include "src/widgets/imagelabel.h"
#include "src/widgets/loadspinner.h"
#include "src/widgets/notificationwidget.h"
#include <QApplication>
// FIXME #include <QBuffer>
#include <QClipboard>
#include <QCursor>
#include <QDesktopServices>
#include <QDrag>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMimeData>
#include <QNetworkAccessManager>
#include <QPushButton>
#include <QRect>
#include <QScreen>
#include <QShortcut>
#include <QTimer>
#include <QUrlQuery>
#include <QVBoxLayout>

ImgUploaderBase::ImgUploaderBase(const QPixmap& capture, QWidget* parent)
  : QWidget(parent)
  , m_pixmap(capture)
{
    setWindowTitle(tr("Upload image"));
    setWindowIcon(QIcon(GlobalValues::iconPath()));

    // Set window flags to stay on top
#ifdef Q_OS_MACOS
    // On macOS, need to use different approach for stay on top
    setWindowFlags(Qt::WindowStaysOnTopHint | Qt::Tool);
    setAttribute(Qt::WA_MacAlwaysShowToolWindow, true);
#else
    setWindowFlags(Qt::WindowStaysOnTopHint | Qt::Window);
#endif

    // Set fixed height and minimum width for horizontal layout
    setMinimumWidth(500);
    setMaximumHeight(80);

    QRect position = frameGeometry();
    QScreen* screen = QGuiApplication::screenAt(QCursor::pos());

    // Position at top-right corner with some margin
    int margin = 20; // margin from screen edges
    QRect screenGeometry = screen->availableGeometry();
    int x = screenGeometry.right() - position.width() - margin;
    int y = screenGeometry.top() + margin;

    move(x, y);

    m_spinner = new LoadSpinner(this);
    m_spinner->setColor(ConfigHandler().uiColor());
    m_spinner->start();

    m_infoLabel = new QLabel(tr("Uploading..."));
    m_infoLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_infoLabel->setCursor(QCursor(Qt::IBeamCursor));

    m_vLayout = new QVBoxLayout();
    setLayout(m_vLayout);
    // Compact spacing and margins for single-line layout
    m_vLayout->setSpacing(0);
    m_vLayout->setContentsMargins(10, 10, 10, 10);
    m_vLayout->addWidget(m_spinner, 0, Qt::AlignHCenter);
    m_vLayout->addWidget(m_infoLabel);

    setAttribute(Qt::WA_DeleteOnClose);
}

LoadSpinner* ImgUploaderBase::spinner()
{
    return m_spinner;
}

const QUrl& ImgUploaderBase::imageURL()
{
    return m_imageURL;
}

void ImgUploaderBase::setImageURL(const QUrl& imageURL)
{
    m_imageURL = imageURL;
}

const QPixmap& ImgUploaderBase::pixmap()
{
    return m_pixmap;
}

void ImgUploaderBase::setPixmap(const QPixmap& pixmap)
{
    m_pixmap = pixmap;
}

NotificationWidget* ImgUploaderBase::notification()
{
    return m_notification;
}

void ImgUploaderBase::setInfoLabelText(const QString& text)
{
    m_infoLabel->setText(text);
}

void ImgUploaderBase::startDrag()
{
    auto* mimeData = new QMimeData;
    mimeData->setUrls(QList<QUrl>{ m_imageURL });
    mimeData->setImageData(m_pixmap);

    auto* dragHandler = new QDrag(this);
    dragHandler->setMimeData(mimeData);
    dragHandler->setPixmap(m_pixmap.scaled(
      256, 256, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
    dragHandler->exec();
}

void ImgUploaderBase::showPostUploadDialog()
{
    m_infoLabel->deleteLater();
    m_spinner->deleteLater();

    // Create notification widget but don't add to layout yet
    m_notification = new NotificationWidget();

    // Create horizontal layout for buttons and URL
    m_hLayout = new QHBoxLayout();
    m_hLayout->setSpacing(8);
    m_vLayout->addLayout(m_hLayout);

    // Add Open button
    auto* openButton = new QPushButton(tr("Open"));
    m_hLayout->addWidget(openButton);

    // Add Copy button
    m_copyUrlButton = new QPushButton(tr("Copy"));
    m_hLayout->addWidget(m_copyUrlButton);

    // Add URL field (read-only)
    auto* urlField = new QLineEdit(m_imageURL.toString());
    urlField->setReadOnly(true);
    urlField->selectAll();
    m_hLayout->addWidget(urlField, 1); // stretch factor 1 to take remaining space

    connect(openButton, &QPushButton::clicked, this, &ImgUploaderBase::openURL);
    connect(m_copyUrlButton, &QPushButton::clicked, this, &ImgUploaderBase::copyURL);

    // Ensure window stays on top and is activated
    show();
    raise();
    activateWindow();
}

void ImgUploaderBase::openURL()
{
    bool successful = QDesktopServices::openUrl(m_imageURL);
    if (!successful) {
        m_notification->showMessage(tr("Unable to open the URL."));
    }
}

void ImgUploaderBase::copyURL()
{
    FlameshotDaemon::copyToClipboard(m_imageURL.toString());
    m_notification->showMessage(tr("URL copied to clipboard."));
}

void ImgUploaderBase::copyImage()
{
    FlameshotDaemon::copyToClipboard(m_pixmap);
    m_notification->showMessage(tr("Screenshot copied to clipboard."));
}

void ImgUploaderBase::deleteCurrentImage()
{
    History history;
    HistoryFileName unpackFileName = history.unpackFileName(m_currentImageName);
    deleteImage(unpackFileName.file, unpackFileName.token);
}

void ImgUploaderBase::saveScreenshotToFilesystem()
{
    if (!saveToFilesystemGUI(m_pixmap)) {
        m_notification->showMessage(
          tr("Unable to save the screenshot to disk."));
        return;
    }
    m_notification->showMessage(tr("Screenshot saved."));
}
