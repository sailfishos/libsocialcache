include(../../common.pri)

TEMPLATE = lib
CONFIG += qt create_prl no_install_prl create_pc link_pkgconfig
QT += sql

isEmpty(PREFIX) {
    PREFIX=/usr
}

TARGET = socialcache
target.path = $$[QT_INSTALL_LIBS]

HEADERS = \
    semaphore_p.h \
    socialsyncinterface.h \
    abstractimagedownloader.h \
    abstractimagedownloader_p.h \
    abstractsocialcachedatabase.h \
    abstractsocialcachedatabase_p.h \
    abstractsocialpostcachedatabase.h \
    socialnetworksyncdatabase.h \
    facebookimagesdatabase.h \
    facebookcontactsdatabase.h \
    facebooknotificationsdatabase.h \
    facebookpostsdatabase.h \
    twitterpostsdatabase.h \
    twitternotificationsdatabase.h \
    socialimagesdatabase.h \
    onedriveimagesdatabase.h \
    dropboximagesdatabase.h \
    vkpostsdatabase.h \
    vknotificationsdatabase.h \
    vkimagesdatabase.h

SOURCES = \
    semaphore_p.cpp \
    socialsyncinterface.cpp \
    abstractimagedownloader.cpp \
    abstractsocialcachedatabase.cpp \
    abstractsocialpostcachedatabase.cpp \
    socialnetworksyncdatabase.cpp \
    facebookimagesdatabase.cpp \
    facebookcontactsdatabase.cpp \
    facebooknotificationsdatabase.cpp \
    facebookpostsdatabase.cpp \
    twitterpostsdatabase.cpp \
    twitternotificationsdatabase.cpp \
    socialimagesdatabase.cpp \
    onedriveimagesdatabase.cpp \
    dropboximagesdatabase.cpp \
    vkpostsdatabase.cpp \
    vknotificationsdatabase.cpp \
    vkimagesdatabase.cpp

headers.files = $$HEADERS
headers.path = $$PREFIX/include/socialcache


QMAKE_PKGCONFIG_NAME = lib$$TARGET
QMAKE_PKGCONFIG_DESCRIPTION = Social cache development files
QMAKE_PKGCONFIG_LIBDIR = $$target.path
QMAKE_PKGCONFIG_INCDIR = $$headers.path
QMAKE_PKGCONFIG_DESTDIR = pkgconfig
QMAKE_PKGCONFIG_VERSION = $$VERSION

INSTALLS += target headers

