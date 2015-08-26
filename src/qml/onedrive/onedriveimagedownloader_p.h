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

#ifndef ONEDRIVEIMAGEDOWNLOADER_P_H
#define ONEDRIVEIMAGEDOWNLOADER_P_H

#include <QtCore/QObject>
#include <QtCore/QThread>
#include <QtCore/QMutex>
#include <QtCore/QWaitCondition>
#include <QtCore/QSet>

#include "abstractimagedownloader_p.h"
#include "onedriveimagedownloader.h"
#include "onedriveimagesdatabase.h"

class OneDriveImageCacheModel;
class OneDriveImageDownloaderPrivate: public AbstractImageDownloaderPrivate
{
public:
    explicit OneDriveImageDownloaderPrivate(OneDriveImageDownloader *q);
    virtual ~OneDriveImageDownloaderPrivate();

    OneDriveImagesDatabase database;
    QSet<OneDriveImageCacheModel*> m_connectedModels;
    QMutex m_cacheMutex;
    QMap<QString, OneDriveImageDownloader::UncachedImage> m_uncachedImages;
    QMap<QString, int> m_ongoingAlbumrequests;
    QList<int> m_accountsWaitingForAccessToken;
    int m_optimalThumbnailSize;
    QMap<QString, QMap<QNetworkReply*, QTimer *> > m_networkReplyTimeouts;

private:
    Q_DECLARE_PUBLIC(OneDriveImageDownloader)
};

#endif // ONEDRIVEIMAGEDOWNLOADER_P_H