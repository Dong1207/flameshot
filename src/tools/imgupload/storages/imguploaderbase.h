// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2017-2019 Alejandro Sirgo Rica & Contributors

#pragma once

#include <QElapsedTimer>
#include <QUrl>
#include <QWidget>

class QNetworkReply;
class QNetworkAccessManager;
class QHBoxLayout;
class QVBoxLayout;
class QLabel;
class LoadSpinner;
class QPushButton;
class QUrl;
class NotificationWidget;

class ImgUploaderBase : public QWidget
{
    Q_OBJECT
public:
    explicit ImgUploaderBase(const QPixmap& capture, QWidget* parent = nullptr);
    ~ImgUploaderBase();

    LoadSpinner* spinner();

    const QUrl& imageURL();
    void setImageURL(const QUrl&);
    const QPixmap& pixmap();
    void setPixmap(const QPixmap&);
    void setInfoLabelText(const QString&);

    NotificationWidget* notification();
    virtual void deleteImage(const QString& fileName,
                             const QString& deleteToken) = 0;
    virtual void upload() = 0;

signals:
    void uploadOk(const QUrl& url);
    void deleteOk();

public slots:
    void showPostUploadDialog();

private slots:
    void startDrag();
    void openURL();
    void copyURL();
    void copyImage();
    void deleteCurrentImage();
    void saveScreenshotToFilesystem();

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;

    /// Show @p message to the user. Safe to call before (or without) the
    /// post-upload dialog — falls back to a tray notification.
    void notify(const QString& message);

private:
    /// Is @p w this dialog or something living inside it (child or popup)?
    bool isOwnWidget(const QWidget* w) const;

    QPixmap m_pixmap;

    QVBoxLayout* m_vLayout = nullptr;
    QHBoxLayout* m_hLayout = nullptr;
    // loading
    QLabel* m_infoLabel = nullptr;
    LoadSpinner* m_spinner = nullptr;
    // uploaded
    QPushButton* m_openUrlButton = nullptr;
    QPushButton* m_openDeleteUrlButton = nullptr;
    // Doubles as the "post-upload dialog is up" flag in eventFilter(), so it
    // must start null — reading it uninitialised is what made the filter run
    // during the upload phase.
    QPushButton* m_copyUrlButton = nullptr;
    QPushButton* m_toClipboardButton = nullptr;
    QPushButton* m_saveToFilesystemButton = nullptr;
    QUrl m_imageURL;
    NotificationWidget* m_notification = nullptr;
    // Whether the dialog has ever held focus. Guards the close-on-deactivate
    // path, which fires spuriously when a compositor declines to activate us.
    bool m_wasActivated = false;
    // Started when the Open/Copy row appears, on Wayland only. Deactivations
    // arriving inside the settling window are not the user walking away — see
    // eventFilter().
    QElapsedTimer m_settling;
    // One deferred re-check is enough; a burst of deactivations should not
    // queue up a timer each.
    bool m_settleCheckArmed = false;

public:
    QString m_currentImageName;
};
