// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Flameshot Contributors

#include "customuploader.h"
#include "core/flameshotdaemon.h"
#include "utils/confighandler.h"
#include "utils/globalvalues.h"
#include "utils/history.h"
#include "widgets/uploadhistory.h"
#include "widgets/loadspinner.h"
#include "widgets/notificationwidget.h"
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

    // Scale down to max Full HD (1920x1080) for retina/high-DPI displays
    QPixmap scaledPixmap = m_pixmap;
    int originalWidth = m_pixmap.width();
    int originalHeight = m_pixmap.height();

    const int maxWidth = 1920;
    const int maxHeight = 1080;
    if (originalWidth > maxWidth || originalHeight > maxHeight) {
        double scaleFactor = qMin(
            static_cast<double>(maxWidth) / originalWidth,
            static_cast<double>(maxHeight) / originalHeight
        );

        int newWidth = static_cast<int>(originalWidth * scaleFactor);
        int newHeight = static_cast<int>(originalHeight * scaleFactor);

        scaledPixmap = m_pixmap.scaled(
            newWidth,
            newHeight,
            Qt::KeepAspectRatio,
            Qt::SmoothTransformation
        );
    }

    // Convert pixmap to byte array - try JPEG for large images
    QByteArray imageData;
    QBuffer buffer(&imageData);
    buffer.open(QIODevice::WriteOnly);

    // First try PNG
    scaledPixmap.save(&buffer, "PNG");

    // Track whether we're using JPEG or PNG
    bool usingJpeg = false;

    // If PNG is too large (> 2MB), try JPEG compression
    if (imageData.size() > 2 * 1024 * 1024) {
        imageData.clear();
        buffer.close();
        buffer.open(QIODevice::WriteOnly);
        scaledPixmap.save(&buffer, "JPEG", 85);
        usingJpeg = true;

        // If still too large, try lower quality
        if (imageData.size() > 4 * 1024 * 1024) {
            imageData.clear();
            buffer.close();
            buffer.open(QIODevice::WriteOnly);
            scaledPixmap.save(&buffer, "JPEG", 70);

            // If STILL too large, use even lower quality
            if (imageData.size() > 8 * 1024 * 1024) {
                imageData.clear();
                buffer.close();
                buffer.open(QIODevice::WriteOnly);
                scaledPixmap.save(&buffer, "JPEG", 60);
            }
        }
    }

    // Warn if still too large even after compression
    if (imageData.size() > 15 * 1024 * 1024) {  // > 15MB
        setInfoLabelText(tr("Warning: Image is very large (%1 MB). Upload may fail.\nTrying anyway...")
            .arg(imageData.size() / (1024.0 * 1024.0), 0, 'f', 1));
    }

    QNetworkRequest request;
    request.setUrl(QUrl(uploadUrl));

    // Add headers to help with server compatibility
    request.setRawHeader("User-Agent", "Flameshot/1.0");
    request.setRawHeader("Accept", "*/*");

    // Authentication token, sent verbatim in a user-configurable header
    const QString token = config.customUploadToken();
    const QString tokenHeader = config.customUploadTokenHeader().trimmed();
    if (!token.isEmpty() && !tokenHeader.isEmpty()) {
        request.setRawHeader(tokenHeader.toUtf8(), token.toUtf8());
    }

    // Always use POST with multipart/form-data and field name "image"
    QHttpMultiPart* multipart = new QHttpMultiPart(QHttpMultiPart::FormDataType);

    // Determine content type and filename based on what format we ended up using
    QString contentType = usingJpeg ? "image/jpeg" : "image/png";
    QString filename = usingJpeg ? "screenshot.jpg" : "screenshot.png";

    QHttpPart imagePart;
    imagePart.setHeader(QNetworkRequest::ContentTypeHeader, QVariant(contentType));
    imagePart.setHeader(QNetworkRequest::ContentDispositionHeader,
                       QVariant(QString("form-data; name=\"image\"; filename=\"%1\"").arg(filename)));
    imagePart.setBody(imageData);

    multipart->append(imagePart);

    QNetworkReply* reply = m_networkManager->post(request, multipart);
    multipart->setParent(reply); // Delete multipart with reply

    // Only listen to finished(): it fires for successes and failures alike and
    // already reports errors in detail. Also handling errorOccurred() would
    // consume the reply body first, leaving the server's explanation blank.
    connect(reply, &QNetworkReply::finished,
            this, &CustomUploader::handleUploadResponse);

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
        // Get detailed error information
        int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QString httpReason = reply->attribute(QNetworkRequest::HttpReasonPhraseAttribute).toString();
        QByteArray serverResponse = reply->readAll();

        QString errorMsg;
        if (httpStatus > 0) {
            // Special handling for common HTTP errors
            QString errorDescription;
            switch(httpStatus) {
                case 413:
                    errorDescription = tr("File too large (Server limit exceeded)");
                    break;
                case 401:
                    errorDescription = tr("Unauthorized");
                    break;
                case 403:
                    errorDescription = tr("Forbidden");
                    break;
                case 404:
                    errorDescription = tr("Upload endpoint not found");
                    break;
                case 500:
                    errorDescription = tr("Internal server error");
                    break;
                case 502:
                    errorDescription = tr("Bad gateway");
                    break;
                case 503:
                    errorDescription = tr("Service unavailable");
                    break;
                default:
                    errorDescription = httpReason.isEmpty() ? reply->errorString() : httpReason;
            }

            QString serverResponseStr = QString::fromUtf8(serverResponse).trimmed();
            if (!serverResponseStr.isEmpty()) {
                errorMsg = tr("Upload failed: HTTP %1 - %2\n\nServer response:\n%3")
                    .arg(httpStatus)
                    .arg(errorDescription)
                    .arg(serverResponseStr.left(500));
            } else {
                errorMsg = tr("Upload failed: HTTP %1 - %2")
                    .arg(httpStatus)
                    .arg(errorDescription);
            }
        } else {
            errorMsg = tr("Upload failed: %1\nURL: %2")
                .arg(reply->errorString())
                .arg(reply->url().toString());
        }

        setInfoLabelText(errorMsg);
        spinner()->stop();

        // Log to console for debugging
        qWarning() << "Custom upload error:" << errorMsg;
        qWarning() << "Full server response:" << serverResponse;

        reply->deleteLater();
        // Don't close - let user see error message
        return;
    }

    QByteArray response = reply->readAll();

    QString imageUrl = parseImageUrl(response);

    if (imageUrl.isEmpty()) {
        QString responseStr = QString::fromUtf8(response);
        QString debugMsg = tr("Failed to parse URL from response.\nResponse (first 500 chars):\n%1")
            .arg(responseStr.left(500));
        setInfoLabelText(debugMsg);

        // Log full response for debugging
        qWarning() << "Failed to parse URL from response";
        qWarning() << "Response size:" << response.size() << "bytes";
        qWarning() << "Full response:" << responseStr;

        spinner()->stop();
        reply->deleteLater();
        // Don't close - let user see error message
        return;
    }

    // ALWAYS copy URL to clipboard after successful upload
    // Use FlameshotDaemon for more reliable cross-platform clipboard access
    FlameshotDaemon::copyToClipboard(imageUrl);

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

