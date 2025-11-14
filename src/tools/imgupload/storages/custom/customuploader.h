// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Flameshot Contributors

#pragma once

#include "src/tools/imgupload/storages/imguploaderbase.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

class CustomUploader : public ImgUploaderBase
{
    Q_OBJECT
public:
    explicit CustomUploader(const QPixmap& capture, QWidget* parent = nullptr);

    void upload() override;
    void deleteImage(const QString& fileName,
                     const QString& deleteToken) override;

private slots:
    void handleUploadResponse();
    void handleError(QNetworkReply::NetworkError error);

private:
    QString parseImageUrl(const QByteArray& response);

    QNetworkAccessManager* m_networkManager;
    QPixmap m_pixmap;
};