/*
 * Copyright (C) 2014-2015 Jolla Ltd.
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

#include "vkpostsmodel.h"
#include "abstractsocialcachemodel_p.h"
#include "vkpostsdatabase.h"
#include <QtCore/QDebug>

static const char *POST_LINK_KEY = "post_link_key";

static const char *COPIED_POST_CREATED_TIME_KEY = "copied_post_created_time";
static const char *COPIED_POST_TYPE_KEY = "copied_post_type";
static const char *COPIED_POST_OWNER_NAME_KEY = "copied_post_owner_name";
static const char *COPIED_POST_OWNER_AVATAR_KEY = "copied_post_owner_avatar";
static const char *COPIED_POST_TEXT_KEY = "copied_post_text";
static const char *COPIED_POST_PHOTO_KEY = "copied_post_photo";
static const char *COPIED_POST_VIDEO_KEY = "copied_post_video";
static const char *COPIED_POST_LINK_KEY = "copied_post_link";

class VKPostsModelPrivate: public AbstractSocialCacheModelPrivate
{
public:
    explicit VKPostsModelPrivate(VKPostsModel *q);

    VKPostsDatabase database;

private:
    Q_DECLARE_PUBLIC(VKPostsModel)
};

VKPostsModelPrivate::VKPostsModelPrivate(VKPostsModel *q)
    : AbstractSocialCacheModelPrivate(q)
{
}

VKPostsModel::VKPostsModel(QObject *parent)
    : AbstractSocialCacheModel(*(new VKPostsModelPrivate(this)), parent)
{
    Q_D(VKPostsModel);

    connect(&d->database, &AbstractSocialPostCacheDatabase::postsChanged,
            this, &VKPostsModel::postsChanged);
}

QHash<int, QByteArray> VKPostsModel::roleNames() const
{
    QHash<int, QByteArray> roleNames;
    roleNames.insert(VkId, "vkId");
    roleNames.insert(Name, "name");
    roleNames.insert(Body, "body");
    roleNames.insert(Timestamp, "timestamp");
    roleNames.insert(Icon, "icon");
    roleNames.insert(Images, "images");
    roleNames.insert(Accounts, "accounts");
    roleNames.insert(RepostType, "repostType");
    roleNames.insert(RepostOwnerName, "repostOwnerName");
    roleNames.insert(RepostOwnerAvatar, "repostOwnerAvatar");
    roleNames.insert(RepostText, "repostText");
    roleNames.insert(RepostVideo, "repostVideo");
    roleNames.insert(RepostLink, "repostLink");
    roleNames.insert(RepostTimestamp, "repostTimestamp");
    roleNames.insert(RepostImages, "repostImages");
    roleNames.insert(Link, "link");
    return roleNames;
}

QVariantList VKPostsModel::accountIdFilter() const
{
    Q_D(const VKPostsModel);
    return d->database.accountIdFilter();
}

void VKPostsModel::setAccountIdFilter(const QVariantList &accountIds)
{
    Q_D(VKPostsModel);
    d->database.setAccountIdFilter(accountIds);
}

void VKPostsModel::refresh()
{
    Q_D(VKPostsModel);
    d->database.refresh();
}

void VKPostsModel::remove(const QString &postId)
{
    Q_D(VKPostsModel);

    for (int i=0; i<count(); i++) {
        if (getField(i, VKPostsModel::VkId).toString() == postId) {
            d->removeRange(i, 1);
            d->database.removePost(postId);
            d->database.commit();
            break;
        }
    }
}

void VKPostsModel::clear()
{
    Q_D(VKPostsModel);
    d->clearData();
    d->database.removeAll();
}

void VKPostsModel::postsChanged()
{
    Q_D(VKPostsModel);

    SocialCacheModelData data;
    QList<SocialPost::ConstPtr> postsData = d->database.posts();

    Q_FOREACH (const SocialPost::ConstPtr &post, postsData) {
        QMap<int, QVariant> eventMap;
        appendCommonPostFields(&eventMap, post,
                               VKPostsModel::VkId,
                               VKPostsModel::Name,
                               VKPostsModel::Body,
                               VKPostsModel::Timestamp,
                               VKPostsModel::Icon,
                               VKPostsModel::Images,
                               VKPostsModel::Accounts);
        eventMap.insert(VKPostsModel::Link, post->extra().value(POST_LINK_KEY));

        eventMap.insert(VKPostsModel::RepostOwnerName, post->extra().value(COPIED_POST_OWNER_NAME_KEY));
        eventMap.insert(VKPostsModel::RepostOwnerAvatar, post->extra().value(COPIED_POST_OWNER_AVATAR_KEY));
        eventMap.insert(VKPostsModel::RepostType, post->extra().value(COPIED_POST_TYPE_KEY));
        eventMap.insert(VKPostsModel::RepostText, post->extra().value(COPIED_POST_TEXT_KEY));
        eventMap.insert(VKPostsModel::RepostVideo, post->extra().value(COPIED_POST_VIDEO_KEY));
        eventMap.insert(VKPostsModel::RepostLink, post->extra().value(COPIED_POST_LINK_KEY));
        eventMap.insert(VKPostsModel::RepostTimestamp, post->extra().value(COPIED_POST_CREATED_TIME_KEY));

        QVariantList repostImages;
        QStringList imageUrls = post->extra().value(COPIED_POST_PHOTO_KEY).toString().split(QStringLiteral(","));
        Q_FOREACH (const QString url, imageUrls) {
            if (!url.isEmpty()) {
                QVariantMap tmp;
                tmp.insert(QStringLiteral("url"), url);
                tmp.insert(QStringLiteral("type"), QStringLiteral("photo"));
                repostImages.append(tmp);
            }
        }
        eventMap.insert(VKPostsModel::RepostImages, repostImages);
        data.append(eventMap);
    }

    updateData(data);
}
