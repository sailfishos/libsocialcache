/*
 * Copyright (C) 2015 Jolla Ltd.
 * Contact: Antti Seppälä <antti.seppala@jollamobile.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "onedriveimagedownloader.h"
#include "onedriveimagedownloader_p.h"
#include "onedriveimagedownloaderconstants_p.h"

#include "onedriveimagecachemodel.h"

#include <QtCore/QStandardPaths>
#include <QtGui/QGuiApplication>

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>

#include <QtDebug>

static const char *MODEL_KEY = "model";
static const char *URL_KEY = "url";
// static const char *TYPE_PHOTO = "photo";

OneDriveImageDownloader::UncachedImage::UncachedImage()
{}

OneDriveImageDownloader::UncachedImage::UncachedImage(const QString &thumbnailUrl,
                                                      const QString &imageId,
                                                      const QString &albumId,
                                                      int accountId,
                                                      QVariantList connectedModels)
    : thumbnailUrl(thumbnailUrl)
    , imageId(imageId)
    , albumId(albumId)
    , accountId(accountId)
    , connectedModels(connectedModels)
{}

OneDriveImageDownloader::UncachedImage::UncachedImage(const UncachedImage &other)
    : thumbnailUrl(other.thumbnailUrl)
    , imageId(other.imageId)
    , albumId(other.albumId)
    , accountId(other.accountId)
    , connectedModels(other.connectedModels)
{}

OneDriveImageDownloaderPrivate::OneDriveImageDownloaderPrivate(OneDriveImageDownloader *q)
    : AbstractImageDownloaderPrivate(q)
    , m_optimalThumbnailSize(180)
{
}

OneDriveImageDownloaderPrivate::~OneDriveImageDownloaderPrivate()
{
}

OneDriveImageDownloader::OneDriveImageDownloader(QObject *parent) :
    AbstractImageDownloader(*new OneDriveImageDownloaderPrivate(this), parent)
{
    connect(this, &AbstractImageDownloader::imageDownloaded,
            this, &OneDriveImageDownloader::invokeSpecificModelCallback);
}

OneDriveImageDownloader::~OneDriveImageDownloader()
{
}

void OneDriveImageDownloader::addModelToHash(OneDriveImageCacheModel *model)
{
    Q_D(OneDriveImageDownloader);
    d->m_connectedModels.insert(model);
}

void OneDriveImageDownloader::removeModelFromHash(OneDriveImageCacheModel *model)
{
    Q_D(OneDriveImageDownloader);
    d->m_connectedModels.remove(model);
}

int OneDriveImageDownloader::optimalThumbnailSize() const
{
    Q_D(const OneDriveImageDownloader);
    return d->m_optimalThumbnailSize;
}

void OneDriveImageDownloader::setOptimalThumbnailSize(int optimalThumbnailSize)
{
    Q_D(OneDriveImageDownloader);

    if (d->m_optimalThumbnailSize != optimalThumbnailSize) {
        d->m_optimalThumbnailSize = optimalThumbnailSize;
        emit optimalThumbnailSizeChanged();
    }
}

/*
 * A OneDriveImageDownloader can be connected to multiple models.
 * Instead of connecting the imageDownloaded signal directly to the
 * model, we connect it to this slot, which retrieves the target model
 * from the metadata map and invokes its callback directly.
 * This avoids a possibly large number of signal connections + invocations.
 */
void OneDriveImageDownloader::invokeSpecificModelCallback(const QString &url, const QString &path, const QVariantMap &metadata)
{
    Q_D(OneDriveImageDownloader);
    OneDriveImageCacheModel *model = static_cast<OneDriveImageCacheModel*>(metadata.value(MODEL_KEY).value<void*>());

    // check to see if the model was destroyed in the meantime.
    // If not, we can directly invoke the callback.
    if (d->m_connectedModels.contains(model)) {
        model->imageDownloaded(url, path, metadata);
    }
}

QString OneDriveImageDownloader::outputFile(const QString &url,
                                            const QVariantMap &data) const
{
    Q_UNUSED(url);

    // We create the identifier by appending the type to the real identifier
    QString identifier = data.value(QLatin1String(IDENTIFIER_KEY)).toString();
    if (identifier.isEmpty()) {
        return QString();
    }

    QString typeString = data.value(QLatin1String(TYPE_KEY)).toString();
    if (typeString.isEmpty()) {
        return QString();
    }

    identifier.append(typeString);

    return makeOutputFile(SocialSyncInterface::OneDrive, SocialSyncInterface::Images, identifier);
}

void OneDriveImageDownloader::dbQueueImage(const QString &url, const QVariantMap &data, const QString &file)
{
    Q_D(OneDriveImageDownloader);
    Q_UNUSED(url);

    QString identifier = data.value(QLatin1String(IDENTIFIER_KEY)).toString();
    if (identifier.isEmpty()) {
        return;
    }
    int type = data.value(QLatin1String(TYPE_KEY)).toInt();

    switch (type) {
    case ThumbnailImage:
        d->database.updateImageThumbnail(identifier, file);
        break;
    }
}

void OneDriveImageDownloader::dbWrite()
{
    Q_D(OneDriveImageDownloader);

    d->database.commit();
}

void OneDriveImageDownloader::cacheImages(QList<OneDriveImageDownloader::UncachedImage> images)
{
    Q_D(OneDriveImageDownloader);

    Q_FOREACH (const UncachedImage& image, images) {
        // no need for access token for thumbnail images, else
        // emit accessTokenRequested(accountId); would be needed.
        Q_FOREACH (QVariant modelPtr, image.connectedModels) {
            QVariantMap metadata;
            metadata.insert(QLatin1String(TYPE_KEY), (int)OneDriveImageDownloader::ThumbnailImage);
            metadata.insert(QLatin1String(IDENTIFIER_KEY), image.imageId);
            metadata.insert(QLatin1String(URL_KEY), image.thumbnailUrl);
            metadata.insert(QLatin1String(MODEL_KEY), modelPtr);
            queue(image.thumbnailUrl, metadata);
        }
    }
}

void OneDriveImageDownloader::accessTokenRetrieved(const QString &accessToken, int accountId)
{
    Q_D(OneDriveImageDownloader);

    // currently unused, but if we want to support full-size images in future...
    QMutexLocker locker(&d->m_cacheMutex);
    d->m_accessTokens.insert(accountId, accessToken);
}

void OneDriveImageDownloader::accessTokenFailed(int accountId)
{
    Q_D(OneDriveImageDownloader);

    qWarning() << "Failed to retrieve access token for account:" << accountId;
}
