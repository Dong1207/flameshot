// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2017-2019 Alejandro Sirgo Rica & Contributors

#include "imguploaderbase.h"
#include "core/flameshotdaemon.h"
#include "utils/abstractlogger.h"
#include "utils/confighandler.h"
#include "utils/globalvalues.h"
#include "utils/history.h"
#include "utils/screenshotsaver.h"
#include "widgets/imagelabel.h"
#include "widgets/loadspinner.h"
#include "widgets/notificationwidget.h"

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
#include <QEvent>

ImgUploaderBase::ImgUploaderBase(const QPixmap& capture, QWidget* parent)
  : QWidget(parent)
  , m_pixmap(capture)
{
    setWindowTitle(tr("Upload image"));
    setWindowIcon(QIcon(GlobalValues::iconPath()));

    // Set window flags to stay on top
#ifdef Q_OS_MACOS
    // On macOS, use Dialog instead of Tool to keep app visible in dock/tray
    setWindowFlags(Qt::WindowStaysOnTopHint | Qt::Dialog | Qt::WindowCloseButtonHint);
    // Don't set as modal to allow tray icon interaction
    setWindowModality(Qt::NonModal);
#else
    setWindowFlags(Qt::WindowStaysOnTopHint | Qt::Window | Qt::WindowCloseButtonHint);
#endif

    // Install event filter to detect click outside
    qApp->installEventFilter(this);

    // Set fixed height and minimum width for horizontal layout
    setMinimumWidth(500);
    // Room for the notification strip (a fixed 40px) on top of the button row;
    // it stays hidden until there is something to say, so the resting height
    // is unchanged.
    setMaximumHeight(130);

    QRect position = frameGeometry();
    // Wayland doesn't hand out a global cursor position, so screenAt() can
    // come back empty there — dereferencing it would take the whole app down.
    QScreen* screen = QGuiApplication::screenAt(QCursor::pos());
    if (screen == nullptr) {
        screen = QGuiApplication::primaryScreen();
    }

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

ImgUploaderBase::~ImgUploaderBase()
{
    // Remove event filter when destroying
    qApp->removeEventFilter(this);
}

bool ImgUploaderBase::eventFilter(QObject* obj, QEvent* event)
{
    if (event->type() == QEvent::WindowActivate && obj == this) {
        m_wasActivated = true;
    }

    // Only process events after the upload is complete (when buttons are shown)
    if (m_copyUrlButton != nullptr) {
        // Handle focus loss - close when window loses focus (user clicked on another app)
        // Use ApplicationDeactivate for better cross-platform compatibility
        //
        // Wait for the dialog to have been focused at least once first. A
        // Wayland client cannot focus itself on demand — activateWindow() only
        // *asks*, via xdg-activation, and the compositor may refuse. If the
        // dialog comes up unfocused and a deactivate arrives before the user
        // reaches it, closing here would take the dialog away while they are
        // still moving the mouse towards it.
        if (m_wasActivated && (event->type() == QEvent::ApplicationDeactivate ||
                               event->type() == QEvent::WindowDeactivate)) {
            // For Windows, we need to check if the event is for our window
            if (obj == this || obj == qApp) {
                close();
                return false; // Let the event propagate
            }
        }

        // Also handle mouse clicks outside the dialog (within the same app)
        //
        // Ask *which widget* the press is headed for rather than comparing
        // screen coordinates. Wayland gives a client no global coordinate
        // space: QApplication::widgetAt() returns nullptr there, and a
        // top-level's mapToGlobal() reports the position we asked for in
        // move() — which the compositor is free to ignore — not where the
        // window actually sits. The old geometry test therefore read clicks on
        // our own Open/Copy buttons as "outside", closed the dialog and
        // swallowed the press, so neither button ever emitted clicked().
        if (event->type() == QEvent::MouseButtonPress && isVisible()) {
            // Mouse presses are filtered twice, once for the QWindow and once
            // for the QWidget. Only the widget pass can answer the question.
            auto* target = qobject_cast<QWidget*>(obj);
            if (target != nullptr && !isOwnWidget(target)) {
                close();
                // Don't consume it: the user aimed at another window and
                // should still land there.
                return false;
            }
        }
    }

    // Pass event to parent
    return QWidget::eventFilter(obj, event);
}

void ImgUploaderBase::notify(const QString& message)
{
    // Deleting an entry from the upload history builds an uploader that never
    // shows the post-upload dialog, so there is no notification widget to talk
    // to — that path used to dereference a null pointer and take the app down.
    if (m_notification == nullptr) {
        AbstractLogger::info() << message;
        return;
    }
    m_notification->show();
    m_notification->showMessage(message);
}

bool ImgUploaderBase::isOwnWidget(const QWidget* w) const
{
    // parentWidget() walks through popups too — a context menu opened on the
    // URL field is parented to it, so it counts as inside the dialog.
    for (; w != nullptr; w = w->parentWidget()) {
        if (w == this) {
            return true;
        }
    }
    return false;
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

    // Has to go into the layout: an unparented widget that is never shown
    // draws nothing, so every showMessage() below was silently discarded and
    // the widget leaked on top of that. It stays zero-height until it has a
    // message to animate open.
    m_notification = new NotificationWidget(this);
    m_vLayout->addWidget(m_notification);
    m_notification->hide();

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
    if (!QDesktopServices::openUrl(m_imageURL)) {
        // Stay open on failure, otherwise the message goes with the dialog and
        // a browser that never launched is indistinguishable from a dead
        // button. The URL is still on screen to copy by hand.
        notify(tr("Unable to open the URL."));
        return;
    }
    close();
}

void ImgUploaderBase::copyURL()
{
    FlameshotDaemon::copyToClipboard(m_imageURL.toString());
    notify(tr("URL copied to clipboard."));
    // Close dialog after copying URL
    close();
}

void ImgUploaderBase::copyImage()
{
    FlameshotDaemon::copyToClipboard(m_pixmap);
    notify(tr("Screenshot copied to clipboard."));
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
        notify(
          tr("Unable to save the screenshot to disk."));
        return;
    }
    notify(tr("Screenshot saved."));
}
