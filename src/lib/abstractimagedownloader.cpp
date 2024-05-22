/*
 * Copyright (C) 2013 Lucien Xu <sfietkonstantin@free.fr>
 * Copyright (C) 2013 - 2021 Jolla Pty Ltd.
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

#include "abstractimagedownloader.h"

#include <QtCore/QFileInfo>
#include <QtCore/QDir>
#include <QtCore/QCryptographicHash>
#include <QtCore/QStandardPaths>
#include <QtCore/QMimeDatabase>
#include <QtGui/QImage>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

#include <QtDebug>

#include "abstractimagedownloader_p.h"

// The AbstractImageDownloader is a class used to build image downloader objects
//
// An image downloader object is a QObject based object that lives in
// a lower priority thread, downloads images from social networks
// and updates a database.
//
// This object do not expose many methods. Instead, since it lives
// in it's own thread, communications should be done using signals
// and slots.
//
// To download an image, the AbstractImagesDownloader::queue slot
// should be used, and when the download is completed, the
// AbstractImagesDownloaderPrivate::imageDownloaded will be emitted.

static int MAX_SIMULTANEOUS_DOWNLOAD = 5;
static int MAX_BATCH_SAVE = 50;

AbstractImageDownloaderPrivate::AbstractImageDownloaderPrivate(AbstractImageDownloader *q)
    : networkAccessManager(0), q_ptr(q), loadedCount(0)
{
}

AbstractImageDownloaderPrivate::~AbstractImageDownloaderPrivate()
{
}

void AbstractImageDownloaderPrivate::manageStack()
{
    Q_Q(AbstractImageDownloader);
    while (runningReplies.count() < MAX_SIMULTANEOUS_DOWNLOAD && !stack.isEmpty()) {
        // Create a reply to download the image
        ImageInfo *info = stack.takeLast();

        QString url = info->url;
        if (!info->redirectUrl.isEmpty()) {
            url = info->redirectUrl;
        }

        if (QNetworkReply *reply = q->createReply(url, info->requestsData.first())) {
            QTimer *timer = new QTimer(q);
            timer->setInterval(60000);
            timer->setSingleShot(true);
            QObject::connect(timer, &QTimer::timeout,
                    q, &AbstractImageDownloader::timedOut);
            timer->start();
            replyTimeouts.insert(timer, reply);
            reply->setProperty("timeoutTimer", QVariant::fromValue<QTimer*>(timer));
            QObject::connect(reply, SIGNAL(finished()), q, SLOT(slotFinished())); // For some reason, this fixes an issue with oopp sync plugins
            runningReplies.insert(reply, info);
        } else {
            // emit signal.  Empty file signifies error.
            Q_FOREACH (const QVariantMap &metadata, info->requestsData) {
                emit q->imageDownloaded(info->url, QString(), metadata);
            }
            delete info;
        }
    }
}

bool AbstractImageDownloaderPrivate::writeImageData(ImageInfo *info, QNetworkReply *reply, QString *outFileName)
{
    Q_Q(AbstractImageDownloader);
    qint64 bytesAvailable = reply->bytesAvailable();
    if (bytesAvailable == 0) {
        qWarning() << Q_FUNC_INFO << "No image data available";
        return false;
    }

    const QByteArray imageData = reply->readAll();

    static const QMimeDatabase mimeDatabase;
    const QMimeType dataMimeType = mimeDatabase.mimeTypeForData(imageData);
    if (!dataMimeType.name().startsWith(QStringLiteral("image/"))) {
        qWarning() << "Downloaded file is not an image, mime type is" << dataMimeType.name();
        if (dataMimeType.name() == "text/plain") {
            // might be some error explanation
            qDebug() << "Got text instead:" << imageData.left(200);
        }
        return false;
    }

    QString url = info->url;
    if (!info->redirectUrl.isEmpty()) {
        url = info->redirectUrl;
    }

    QString localFilePath = q->outputFile(url, info->requestsData.first(), dataMimeType.name());
    QDir parentDir = QFileInfo(localFilePath).dir();
    if (!parentDir.exists()) {
        parentDir.mkpath(".");
    }

    const QMimeType localFilePathMimeType = mimeDatabase.mimeTypesForFileName(localFilePath).value(0);

    if (localFilePathMimeType != dataMimeType) {
        // The destination file path has a file extension that does not match the mime type of the
        // downloaded content.
        const QFileInfo fileInfo(localFilePath);
        qWarning() << "Downloaded file" << fileInfo.fileName() << "has type" << dataMimeType.name()
                   << "instead of expected" << localFilePathMimeType.name()
                   << ", converting to" << localFilePathMimeType.name();

        QImage image;
        if (!image.loadFromData(imageData)) {
            qWarning() << "Unable to read downloaded image data";
            return false;
        }

        // QImage::save() will convert the image to the correct mime type when writing to file.
        if (!image.save(localFilePath)) {
            qWarning() << "Unable to save downloaded image data to file:" << localFilePath;
            return false;
        }
    } else {
        // The downloaded content matches the mime type of the destination file path, so save the
        // content directly to the file without converting it.
        QFile file(localFilePath);
        if (!file.open(QFile::WriteOnly)) {
            qWarning() << "Unable to write downloaded image data to file:" << localFilePath;
            return false;
        }
        file.write(imageData);
        file.close();
    }

    *outFileName = localFilePath;
    return true;
}

void AbstractImageDownloader::slotFinished()
{
    Q_D(AbstractImageDownloader);

    QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
    if (!reply) {
        qWarning() << Q_FUNC_INFO << "finished signal received with null reply";
        d->manageStack();
        return;
    }

    ImageInfo *info = d->runningReplies.take(reply);
    QTimer *timer = reply->property("timeoutTimer").value<QTimer*>();
    if (timer) {
        d->replyTimeouts.remove(timer);
        timer->stop();
        timer->deleteLater();
    }
    reply->deleteLater();
    if (!info) {
        qWarning() << Q_FUNC_INFO << "No image info associated with reply";
        d->manageStack();
        return;
    }

    QByteArray redirectedUrl = reply->rawHeader("Location");
    if (redirectedUrl.length() > 0) {
        // this is URL redirection
        info->redirectUrl = QString(redirectedUrl);
        d->stack.append(info);
        d->manageStack();
    } else {
        QString fileName;
        if (d->writeImageData(info, reply, &fileName)) {
            dbQueueImage(info->url, info->requestsData.first(), fileName);
            Q_FOREACH (const QVariantMap &metadata, info->requestsData) {
                emit imageDownloaded(info->url, fileName, metadata);
            }
        } else {
            // the file is not in image format.
            Q_FOREACH (const QVariantMap &metadata, info->requestsData) {
                emit imageDownloaded(info->url, QString(), metadata);
            }
        }

        delete info;

        d->loadedCount ++;
        d->manageStack();

        if (d->loadedCount > MAX_BATCH_SAVE
            || (d->runningReplies.isEmpty() && d->stack.isEmpty())) {
            dbWrite();
            d->loadedCount = 0;
        }
    }
}

void AbstractImageDownloader::timedOut()
{
    Q_D(AbstractImageDownloader);

    QTimer *timer = qobject_cast<QTimer*>(sender());
    if (timer) {
        QNetworkReply *reply = d->replyTimeouts.take(timer);
        if (reply) {
            reply->deleteLater();
            timer->deleteLater();
            ImageInfo *info = d->runningReplies.take(reply);
            qWarning() << Q_FUNC_INFO << "Image download request timed out";
            Q_FOREACH (const QVariantMap &metadata, info->requestsData) {
                emit imageDownloaded(info->url, QString(), metadata);
            }
        }
    }

    d->manageStack();
}

AbstractImageDownloader::AbstractImageDownloader(QObject *parent)
    : QObject(parent)
    , d_ptr(new AbstractImageDownloaderPrivate(this))
{
    Q_D(AbstractImageDownloader);
    d->networkAccessManager = new QNetworkAccessManager(this);
}

AbstractImageDownloader::AbstractImageDownloader(AbstractImageDownloaderPrivate &dd, QObject *parent)
    : QObject(parent), d_ptr(&dd)
{
    Q_D(AbstractImageDownloader);
    d->networkAccessManager = new QNetworkAccessManager(this);
}

AbstractImageDownloader::~AbstractImageDownloader()
{
}

void AbstractImageDownloader::queue(const QString &url, const QVariantMap &metadata)
{
    Q_D(AbstractImageDownloader);
    if (!dbInit()) {
        qWarning() << Q_FUNC_INFO << "Cannot perform operation, database is not initialized";
        emit imageDownloaded(url, QString(), metadata); // empty file signifies error.
        return;
    }


    Q_FOREACH (ImageInfo *info, d->runningReplies) {
        if (info->url == url) {
            qWarning() << Q_FUNC_INFO << "duplicate running request, appending metadata.";
            info->requestsData.append(metadata);
            return;
        }
    }

    ImageInfo *info = 0;
    for (int i = 0; i < d->stack.count(); ++i) {
        if (d->stack.at(i)->url == url) {
            qWarning() << Q_FUNC_INFO << "duplicate queued request, appending metadata.";
            info = d->stack.takeAt(i);
            info->requestsData.append(metadata);
            break;
        }
    }

    if (!info) {
        info = new ImageInfo(url, metadata);
    }

    d->stack.append(info);
    d->manageStack();
}

QNetworkReply *AbstractImageDownloader::createReply(const QString &url, const QVariantMap &metadata)
{
    Q_D(AbstractImageDownloader);
    QNetworkRequest request (url);

    for (QVariantMap::const_iterator iter = metadata.begin(); iter != metadata.end(); ++iter) {
        if (iter.key().startsWith("accessToken")) {
            request.setRawHeader(QString(QLatin1String("Authorization")).toUtf8(),
                                 QString(QLatin1String("Bearer ")).toUtf8() + iter.value().toString().toUtf8());
            break;
        }
    }

    qWarning() << "AbstractImageDownloader::about to fetch image:" << url;
    return d->networkAccessManager->get(request);
}

static QString createOutputPath(SocialSyncInterface::DataType dataType,
                                SocialSyncInterface::SocialNetwork socialNetwork,
                                const QString &subdir,
                                const QString &identifier,
                                const QString &mimetype)
{
    QString result = QString(PRIVILEGED_DATA_DIR) + QChar('/') + SocialSyncInterface::dataType(dataType) + QChar('/');

    if (dataType == SocialSyncInterface::Contacts) {
        result += QStringLiteral("avatars") + QChar('/');
    }

    result += SocialSyncInterface::socialNetwork(socialNetwork) + QChar('/')
            + subdir + QChar('/');

    // do we want to support more image types?
    if (mimetype == QStringLiteral("image/png")) {
        result += identifier + QStringLiteral(".png");
    } else {
        result += identifier + QStringLiteral(".jpg");
    }
    return result;
}

QString AbstractImageDownloader::makeOutputFile(SocialSyncInterface::SocialNetwork socialNetwork,
                                                SocialSyncInterface::DataType dataType,
                                                const QString &identifier,
                                                const QString &mimetype)
{
    if (identifier.isEmpty()) {
        return QString();
    }

    QCryptographicHash hash (QCryptographicHash::Md5);
    hash.addData(identifier.toUtf8());
    QByteArray hashedIdentifier = hash.result().toHex();

    QString path = createOutputPath(dataType, socialNetwork, QString(hashedIdentifier.at(0)), identifier, mimetype);
    return path;
}

QString AbstractImageDownloader::makeUrlOutputFile(SocialSyncInterface::SocialNetwork socialNetwork,
                                                   SocialSyncInterface::DataType dataType,
                                                   const QString &identifier,
                                                   const QString &remoteUrl,
                                                   const QString &mimeType)
{
    // this function hashes the remote URL in order to increase the
    // chance that a changed remote url will result in resynchronisation
    // of the image, due to output file path mismatch.

    if (identifier.isEmpty() || remoteUrl.isEmpty()) {
        return QString();
    }

    QCryptographicHash urlHash(QCryptographicHash::Md5);
    urlHash.addData(remoteUrl.toUtf8());
    QString hashedUrl = QString::fromUtf8(urlHash.result().toHex());

    QCryptographicHash idHash(QCryptographicHash::Md5);
    idHash.addData(identifier.toUtf8());
    QByteArray hashedId = idHash.result().toHex();

    QString path = createOutputPath(dataType, socialNetwork, QString(hashedId.at(0)), hashedUrl, mimeType);
    return path;
}

bool AbstractImageDownloader::dbInit()
{
    return true;
}

void AbstractImageDownloader::dbQueueImage(const QString &url, const QVariantMap &metadata,
                                           const QString &file)
{
    Q_UNUSED(url)
    Q_UNUSED(metadata)
    Q_UNUSED(file)
}

void AbstractImageDownloader::dbWrite()
{
}
