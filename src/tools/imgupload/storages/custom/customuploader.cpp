// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Flameshot Contributors

#include "customuploader.h"
#include "src/utils/confighandler.h"
#include "src/utils/globalvalues.h"
#include "src/utils/history.h"
#include "src/widgets/uploadhistory.h"
#include "src/widgets/loadspinner.h"
#include "src/widgets/notificationwidget.h"
#include <QBuffer>
#include <QClipboard>
#include <QDateTime>
#include <QGuiApplication>
#include <QHttpMultiPart>
#include <QJsonArray>
#include <QMimeDatabase>
#include <QShortcut>
#include <QUrlQuery>

CustomUploader::CustomUploader(const QPixmap& capture, QWidget* parent)
  : ImgUploaderBase(capture, parent)
  , m_networkManager(new QNetworkAccessManager(this))
  , m_pixmap(capture)
{}

void CustomUploader::upload()
{
    ConfigHandler config;
    QString uploadUrl = config.customUploadUrl();

    if (uploadUrl.isEmpty()) {
        // If no URL configured, show error in the dialog
        spinner()->stop();

        setInfoLabelText(tr("Custom upload URL is not configured. Please configure in Settings."));

        // Don't proceed with upload
        return;
    }

    // Convert pixmap to byte array
    QByteArray imageData;
    QBuffer buffer(&imageData);
    buffer.open(QIODevice::WriteOnly);
    m_pixmap.save(&buffer, "PNG");

    QNetworkRequest request;
    request.setUrl(QUrl(uploadUrl));

    // Always use POST with multipart/form-data and field name "image"
    QHttpMultiPart* multipart = new QHttpMultiPart(QHttpMultiPart::FormDataType);

    QHttpPart imagePart;
    imagePart.setHeader(QNetworkRequest::ContentTypeHeader, QVariant("image/png"));
    imagePart.setHeader(QNetworkRequest::ContentDispositionHeader,
                       QVariant("form-data; name=\"image\"; filename=\"screenshot.png\""));
    imagePart.setBody(imageData);

    multipart->append(imagePart);

    QNetworkReply* reply = m_networkManager->post(request, multipart);
    multipart->setParent(reply); // Delete multipart with reply

    connect(reply, &QNetworkReply::finished,
            this, &CustomUploader::handleUploadResponse);
    connect(reply, &QNetworkReply::errorOccurred,
            this, &CustomUploader::handleError);

    spinner()->start();
}

void CustomUploader::deleteImage(const QString& fileName,
                                const QString& deleteToken)
{
    Q_UNUSED(fileName)
    Q_UNUSED(deleteToken)
    // Custom upload doesn't support deletion by default
    // Could be extended to support delete URLs if needed
    notification()->showMessage(tr("Image deletion is not supported for custom uploads"));
}

void CustomUploader::handleUploadResponse()
{
    spinner()->stop();
    QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());

    if (!reply) {
        setInfoLabelText(tr("Upload failed: Invalid response"));
        spinner()->stop();
        // Don't close - let user see error message
        return;
    }

    if (reply->error() != QNetworkReply::NoError) {
        QString errorMsg = tr("Upload failed: %1").arg(reply->errorString());
        setInfoLabelText(errorMsg);
        spinner()->stop();
        reply->deleteLater();
        // Don't close - let user see error message
        return;
    }

    QByteArray response = reply->readAll();

    QString imageUrl = parseImageUrl(response);

    if (imageUrl.isEmpty()) {
        QString debugMsg = tr("Failed to parse URL. Response: %1").arg(QString::fromUtf8(response).left(200));
        setInfoLabelText(debugMsg);
        spinner()->stop();
        reply->deleteLater();
        // Don't close - let user see error message
        return;
    }

    // ALWAYS copy URL to clipboard after successful upload
    QGuiApplication::clipboard()->setText(imageUrl);

    // Save to history
    History history;
    // Extract last part of URL path as filename
    QUrl url(imageUrl);
    QString fileName = url.path().section('/', -1); // Get last segment after /
    if (fileName.isEmpty()) {
        // If no filename in URL, generate one based on timestamp
        fileName = QString("screenshot_%1").arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"));
    }
    // Add .png extension if not present
    if (!fileName.contains('.')) {
        fileName += ".png";
    }
    // Encode the URL to make it filesystem-safe by converting to base64
    QString encodedUrl = QString::fromUtf8(imageUrl.toUtf8().toBase64());
    // Pack the filename with "custom" type and the encoded URL as the "token"
    QString packedName = history.packFileName("custom", encodedUrl, fileName);
    history.save(m_pixmap, packedName);

    // Set URL for the base class to display
    setImageURL(QUrl(imageUrl));

    // Show the post upload dialog with URL and buttons
    showPostUploadDialog();

    reply->deleteLater();

    // Don't call close() here - let the dialog handle it
}

void CustomUploader::handleError(QNetworkReply::NetworkError error)
{
    Q_UNUSED(error)
    spinner()->stop();

    QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());
    if (reply) {
        QString errorMsg = tr("Network error: %1").arg(reply->errorString());
        setInfoLabelText(errorMsg);
        reply->deleteLater();
    }

    // Don't close - let user see error message
}

QString CustomUploader::parseImageUrl(const QByteArray& response)
{
    // Parse JSON response
    QJsonParseError jsonError;
    QJsonDocument doc = QJsonDocument::fromJson(response, &jsonError);

    if (jsonError.error == QJsonParseError::NoError) {
        QJsonObject obj = doc.object();

        // Your server returns: {"success": true, "url": "http://localhost:4000/s/xxx"}
        if (obj.contains("url")) {
            return obj["url"].toString();
        }
    }

    // Fallback: try to find any URL in the response
    QString responseStr = QString::fromUtf8(response);
    QRegularExpression urlRegex(R"((https?://[^\s<>"{}|\\^`\[\]]+))");
    QRegularExpressionMatch match = urlRegex.match(responseStr);

    if (match.hasMatch()) {
        return match.captured(1);
    }

    return QString();
}

