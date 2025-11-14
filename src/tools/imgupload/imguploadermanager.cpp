// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Yurii Puchkov & Contributors
//

#include "imguploadermanager.h"
#include <QPixmap>
#include <QWidget>

// Only using custom uploader
#include "storages/custom/customuploader.h"
#include "src/utils/confighandler.h"

ImgUploaderManager::ImgUploaderManager(QObject* parent)
  : QObject(parent)
  , m_imgUploaderBase(nullptr)
{
    // Always use custom uploader
    m_imgUploaderPlugin = "custom";
    init();
}

void ImgUploaderManager::init()
{
    ConfigHandler config;
    m_urlString = config.customUploadUrl();

    // If no URL configured, leave empty (will show error when trying to upload)
    if (m_urlString.isEmpty()) {
        m_urlString = "";
    }
}

ImgUploaderBase* ImgUploaderManager::uploader(const QPixmap& capture,
                                              QWidget* parent)
{
    // Always use CustomUploader
    m_imgUploaderBase = (ImgUploaderBase*)(new CustomUploader(capture, parent));

    if (m_imgUploaderBase && !capture.isNull()) {
        m_imgUploaderBase->upload();
    }
    return m_imgUploaderBase;
}

ImgUploaderBase* ImgUploaderManager::uploader(const QString& imgUploaderPlugin)
{
    m_imgUploaderPlugin = imgUploaderPlugin;
    init();
    return uploader(QPixmap());
}

const QString& ImgUploaderManager::uploaderPlugin()
{
    return m_imgUploaderPlugin;
}

const QString& ImgUploaderManager::url()
{
    return m_urlString;
}
