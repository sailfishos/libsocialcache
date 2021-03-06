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

#include "onedriveimagecachemodel.h"
#include "abstractsocialcachemodel_p.h"
#include "onedriveimagesdatabase.h"

#include "onedriveimagedownloader_p.h"
#include "onedriveimagedownloaderconstants_p.h"

#include <QtCore/QThread>
#include <QtCore/QStandardPaths>

#include <QtDebug>

// Note:
//
// When querying photos, the nodeIdentifier should be either
// - nothing: query all photos
// - user-USER_ID: query all photos for the given user
// - album-ALBUM_ID: query all photos for the given album
//
// When querying albums, the nodeIdentifier should be either
// - nothing: query all albums for all users
// - USER_ID: query all albums for the given user

static const char *PHOTO_USER_PREFIX = "user-";
static const char *PHOTO_ALBUM_PREFIX = "album-";

class OneDriveImageCacheModelPrivate : public AbstractSocialCacheModelPrivate
{
public:
    OneDriveImageCacheModelPrivate(OneDriveImageCacheModel *q);

    OneDriveImageDownloader *downloader;
    OneDriveImagesDatabase database;
    OneDriveImageCacheModel::ModelDataType type;
};

OneDriveImageCacheModelPrivate::OneDriveImageCacheModelPrivate(OneDriveImageCacheModel *q)
    : AbstractSocialCacheModelPrivate(q), downloader(0), type(OneDriveImageCacheModel::Images)
{
}

OneDriveImageCacheModel::OneDriveImageCacheModel(QObject *parent)
    : AbstractSocialCacheModel(*(new OneDriveImageCacheModelPrivate(this)), parent)
{
    Q_D(const OneDriveImageCacheModel);
    connect(&d->database, &OneDriveImagesDatabase::queryFinished,
            this, &OneDriveImageCacheModel::queryFinished);
}

OneDriveImageCacheModel::~OneDriveImageCacheModel()
{
    Q_D(OneDriveImageCacheModel);
    if (d->downloader) {
        d->downloader->removeModelFromHash(this);
    }
}

QHash<int, QByteArray> OneDriveImageCacheModel::roleNames() const
{
    QHash<int, QByteArray> roleNames;
    roleNames.insert(OneDriveId, "id");
    roleNames.insert(AlbumId, "albumId");
    roleNames.insert(UserId, "userId");
    roleNames.insert(AccountId, "accountId");
    roleNames.insert(Thumbnail, "thumbnail");
    roleNames.insert(ThumbnailUrl, "thumbnailUrl");
    roleNames.insert(Image, "image");
    roleNames.insert(ImageUrl, "imageUrl");
    roleNames.insert(Title, "title");
    roleNames.insert(DateTaken, "dateTaken");
    roleNames.insert(Width, "photoWidth");
    roleNames.insert(Height, "photoHeight");
    roleNames.insert(Count, "dataCount");
    roleNames.insert(MimeType, "mimeType");
    roleNames.insert(Description, "description");
    return roleNames;
}

OneDriveImageCacheModel::ModelDataType OneDriveImageCacheModel::type() const
{
    Q_D(const OneDriveImageCacheModel);
    return d->type;
}

void OneDriveImageCacheModel::setType(OneDriveImageCacheModel::ModelDataType type)
{
    Q_D(OneDriveImageCacheModel);
    if (d->type != type) {
        d->type = type;
        emit typeChanged();
    }
}

OneDriveImageDownloader * OneDriveImageCacheModel::downloader() const
{
    Q_D(const OneDriveImageCacheModel);
    return d->downloader;
}

void OneDriveImageCacheModel::setDownloader(OneDriveImageDownloader *downloader)
{
    Q_D(OneDriveImageCacheModel);
    if (d->downloader != downloader) {
        if (d->downloader) {
            // Disconnect worker object
            disconnect(d->downloader);
            d->downloader->removeModelFromHash(this);
        }

        d->downloader = downloader;
        d->downloader->addModelToHash(this);
        emit downloaderChanged();
    }
}

void OneDriveImageCacheModel::removeImage(const QString &imageId)
{
    Q_D(OneDriveImageCacheModel);

    int row = -1;
    for (int i = 0; i < count(); ++i) {
        QString dbId = data(index(i), OneDriveImageCacheModel::OneDriveId).toString();
        if (dbId == imageId) {
            row = i;
            break;
        }
    }

    if (row >= 0) {
        beginRemoveRows(QModelIndex(), row, row);
        d->m_data.removeAt(row);
        endRemoveRows();

        // Update album image count
        OneDriveImage::ConstPtr image = d->database.image(imageId);
        if (image) {
            OneDriveAlbum::ConstPtr album = d->database.album(image->albumId());
            if (album) {
                d->database.addAlbum(album->albumId(), album->userId(), album->createdTime(),
                                     album->updatedTime(), album->albumName(), album->imageCount()-1);
            }
        }

        d->database.removeImage(imageId);
        d->database.commit();
    }
}

QVariant OneDriveImageCacheModel::data(const QModelIndex &index, int role) const
{
    Q_D(const OneDriveImageCacheModel);
    int row = index.row();
    if (row < 0 || row >= d->m_data.count()) {
        return QVariant();
    }

    const QVariant value = d->m_data.at(row).value(role);

    switch (role) {
        case Thumbnail: {
            const QString thumbnailUrl = d->m_data.at(row).value(ThumbnailUrl).toString();
            if (value.toString().isEmpty() && !thumbnailUrl.isEmpty()) {
                QList<OneDriveImageDownloader::UncachedImage> missingThumbnails;
                QVariantList modelPtrList;
                modelPtrList.append(QVariant::fromValue<void*>((void*)this));
                missingThumbnails.append(OneDriveImageDownloader::UncachedImage(
                                                thumbnailUrl,
                                                d->m_data.at(row).value(OneDriveId).toString(),
                                                d->m_data.at(row).value(AlbumId).toString(),
                                                d->m_data.at(row).value(AccountId).toInt(),
                                                modelPtrList));
                d->downloader->cacheImages(missingThumbnails);
            }
            break;
        }
        case Image: {
            // this should never be hit.  we should always use the "cache" database
            // which has "expiresIn" handling for automatically deleting cloud content
            // after a certain amount of time.
            break;
        }
        default: break;
    }

    return value;
}

void OneDriveImageCacheModel::loadImages()
{
    refresh();
}

void OneDriveImageCacheModel::refresh()
{
    Q_D(OneDriveImageCacheModel);

    const QString userPrefix = QLatin1String(PHOTO_USER_PREFIX);
    const QString albumPrefix = QLatin1String(PHOTO_ALBUM_PREFIX);

    switch (d->type) {
    case OneDriveImageCacheModel::Users:
        d->database.queryUsers();
        break;
    case OneDriveImageCacheModel::Albums:
        d->database.queryAlbums(d->nodeIdentifier);
        break;
    case OneDriveImageCacheModel::Images:
        if (d->nodeIdentifier.startsWith(userPrefix)) {
            d->database.queryUserImages(d->nodeIdentifier.mid(userPrefix.size()));
        } else if (d->nodeIdentifier.startsWith(albumPrefix)) {
            d->database.queryAlbumImages(d->nodeIdentifier.mid(albumPrefix.size()));
        } else {
            d->database.queryUserImages();
        }
        break;
    default:
        break;
    }
}

// NOTE: this is now called directly by OneDriveImageDownloader
// rather than connected to the imageDownloaded signal, for
// performance reasons.
void OneDriveImageCacheModel::imageDownloaded(
        const QString &, const QString &path, const QVariantMap &imageData)
{
    Q_D(OneDriveImageCacheModel);

    if (path.isEmpty()) {
        // empty path signifies an error, which we don't handle here at the moment.
        // Return, otherwise dataChanged signal would cause UI to read back
        // related value, which, being empty, would trigger another download request,
        // potentially causing never ending loop.
        return;
    }

    int row = -1;
    QString id = imageData.value(IDENTIFIER_KEY).toString();

    for (int i = 0; i < count(); ++i) {
        QString dbId = data(index(i), OneDriveImageCacheModel::OneDriveId).toString();
        if (dbId == id) {
            row = i;
            break;
        }
    }

    if (row >= 0) {
        int type = imageData.value(TYPE_KEY).toInt();
        switch (type) {
        case OneDriveImageDownloader::ThumbnailImage:
            d->m_data[row].insert(OneDriveImageCacheModel::Thumbnail, path);
            break;
        default:
            qWarning() << Q_FUNC_INFO << "invalid downloader type: " << type;
            break;
        }

        emit dataChanged(index(row), index(row));
    }
}

void OneDriveImageCacheModel::queryFinished()
{
    Q_D(OneDriveImageCacheModel);

    SocialCacheModelData data;
    switch (d->type) {
    case Users: {
        QList<OneDriveUser::ConstPtr> usersData = d->database.users();
        int count = 0;
        Q_FOREACH (const OneDriveUser::ConstPtr &userData, usersData) {
            QMap<int, QVariant> userMap;
            userMap.insert(OneDriveImageCacheModel::OneDriveId, userData->userId());
            userMap.insert(OneDriveImageCacheModel::Title, userData->userName());
            userMap.insert(OneDriveImageCacheModel::Count, userData->count());
            userMap.insert(OneDriveImageCacheModel::AccountId, userData->accountId());
            count += userData->count();
            data.append(userMap);
        }

        if (data.count() > 1) {
            QMap<int, QVariant> userMap;
            userMap.insert(OneDriveImageCacheModel::OneDriveId, QString());
            userMap.insert(OneDriveImageCacheModel::Thumbnail, QString());
            //: Label for the "show all users from all OneDrive accounts" option
            //% "All"
            userMap.insert(OneDriveImageCacheModel::Title, qtTrId("nemo_socialcache_onedrive_images_model-all-users"));
            userMap.insert(OneDriveImageCacheModel::AccountId, -1);
            userMap.insert(OneDriveImageCacheModel::Count, count);
            data.prepend(userMap);
        }
        break;
    }
    case Albums: {
        QList<OneDriveAlbum::ConstPtr> albumsData = d->database.albums();

        int count = 0;
        Q_FOREACH (const OneDriveAlbum::ConstPtr &albumData, albumsData) {
            QMap<int, QVariant> albumMap;
            albumMap.insert(OneDriveImageCacheModel::OneDriveId, albumData->albumId());
            albumMap.insert(OneDriveImageCacheModel::Title, albumData->albumName());
            albumMap.insert(OneDriveImageCacheModel::Count, albumData->imageCount());
            albumMap.insert(OneDriveImageCacheModel::UserId, albumData->userId());
            count += albumData->imageCount();
            data.append(albumMap);
        }

        if (data.count() > 1) {
            QMap<int, QVariant> albumMap;
            albumMap.insert(OneDriveImageCacheModel::OneDriveId, QString());
            // albumMap.insert(OneDriveImageCacheModel::Icon, QString());
            //:  Label for the "show all photos from all albums by this user" option
            //% "All"
            albumMap.insert(OneDriveImageCacheModel::Title, qtTrId("nemo_socialcache_onedrive_images_model-all-albums"));
            albumMap.insert(OneDriveImageCacheModel::Count, count);
            if (d->nodeIdentifier.isEmpty()) {
                albumMap.insert(OneDriveImageCacheModel::UserId, QString());
            } else {
                albumMap.insert(OneDriveImageCacheModel::UserId, data.first().value(OneDriveImageCacheModel::UserId));
            }
            data.prepend(albumMap);
        }
        break;
    }
    case Images: {
        QList<OneDriveImage::ConstPtr> imagesData = d->database.images();
        for (int i = 0; i < imagesData.count(); i ++) {
            const OneDriveImage::ConstPtr & imageData = imagesData.at(i);
            QMap<int, QVariant> imageMap;
            imageMap.insert(OneDriveImageCacheModel::OneDriveId, imageData->imageId());
            imageMap.insert(OneDriveImageCacheModel::AlbumId, imageData->albumId());
            imageMap.insert(OneDriveImageCacheModel::UserId, imageData->userId());
            imageMap.insert(OneDriveImageCacheModel::AccountId, imageData->accountId());
            imageMap.insert(OneDriveImageCacheModel::Thumbnail, imageData->thumbnailFile());
            imageMap.insert(OneDriveImageCacheModel::ThumbnailUrl, imageData->thumbnailUrl());
            imageMap.insert(OneDriveImageCacheModel::Image, imageData->imageFile());
            imageMap.insert(OneDriveImageCacheModel::ImageUrl, imageData->imageUrl());
            imageMap.insert(OneDriveImageCacheModel::Title, imageData->imageName());
            imageMap.insert(OneDriveImageCacheModel::DateTaken, imageData->createdTime());
            imageMap.insert(OneDriveImageCacheModel::Width, imageData->width());
            imageMap.insert(OneDriveImageCacheModel::Height, imageData->height());
            imageMap.insert(OneDriveImageCacheModel::Description, imageData->description());
            data.append(imageMap);
        }
        break;
    }
    default:
        return;
    }

    updateData(data);
}
