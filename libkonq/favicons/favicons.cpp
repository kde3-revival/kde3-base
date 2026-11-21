/* This file is part of the KDE project
   Copyright (C) 2001 Malte Starostik <malte@kde.org>

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public
   License as published by the Free Software Foundation; either
   version 2 of the License, or (at your option) any later version.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public License
   along with this library; see the file COPYING.LIB.  If not, write to
   the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.
*/

#include <string.h>
#include <time.h>

#include <qbuffer.h>
#include <qfile.h>
#include <qcache.h>
#include <qimage.h>
#include <qtimer.h>

#include <kdatastream.h> // DO NOT REMOVE, otherwise bool marshalling breaks
#include <kicontheme.h>
#include <kimageio.h>
#include <ksimpleconfig.h>
#include <kstandarddirs.h>
#include <kio/job.h>

#include "favicons.moc"

struct FaviconsModulePrivate
{
    virtual ~FaviconsModulePrivate() { delete config; }

    struct DownloadInfo
    {
        QString hostOrURL;
        bool isHost;
        KURL iconURL;   // as asked for (the job's URL changes on redirects)
        QString mimetype;
        QByteArray iconData;
    };
    QMap<KIO::Job *, DownloadInfo> downloads;
    QMap<QString, time_t> failedDownloads;   // icon URL -> when it failed
    KSimpleConfig *config;
    QPtrList<KIO::Job> killJobs;
    KIO::MetaData metaData;
    QString faviconsDir;
    QCache<QString> faviconsCache;
};

FaviconsModule::FaviconsModule(const QCString &obj)
    : KDEDModule(obj)
{
    // create our favicons folder so that KIconLoader knows about it
    d = new FaviconsModulePrivate;
    d->faviconsDir = KGlobal::dirs()->saveLocation( "cache", "favicons/" );
    d->faviconsDir.truncate(d->faviconsDir.length()-9); // Strip off "favicons/"
    d->metaData.insert("ssl_no_client_cert", "TRUE");
    d->metaData.insert("ssl_militant", "TRUE");
    d->metaData.insert("UseCache", "false");
    d->metaData.insert("cookies", "none");
    d->metaData.insert("no-auth", "true");
    d->config = new KSimpleConfig(locateLocal("data", "konqueror/faviconrc"));
    d->faviconsCache.setAutoDelete(true);
}

FaviconsModule::~FaviconsModule()
{
    delete d;
}

QString removeSlash(QString result) 
{
    for (unsigned int i = result.length() - 1; i > 0; --i)
        if (result[i] != '/')
        {
            result.truncate(i + 1);
            break;
        }

    return result;
}


QString FaviconsModule::iconForURL(const KURL &url)
{
    if (url.host().isEmpty())
        return QString::null;

    QString icon;
    QString simplifiedURL = simplifyURL(url);

    QString *iconURL = d->faviconsCache.find( removeSlash(simplifiedURL) );
    if (iconURL)
        icon = *iconURL;
    else
        icon = d->config->readEntry( removeSlash(simplifiedURL) );

    if (!icon.isEmpty())
        icon = iconNameFromURL(KURL( icon ));
    else 
        icon = url.host();
        
    icon = "favicons/" + icon;

    if (QFile::exists(d->faviconsDir+icon+".png"))
        return icon;

    return QString::null;
}

QString FaviconsModule::simplifyURL(const KURL &url)
{
    // splat any = in the URL so it can be safely used as a config key
    QString result = url.host() + url.path();
    for (unsigned int i = 0; i < result.length(); ++i)
        if (result[i] == '=')
            result[i] = '_';
    return result;
}

QString FaviconsModule::iconNameFromURL(const KURL &iconURL)
{
    if (iconURL.path() == "/favicon.ico")
       return iconURL.host();

    QString result = simplifyURL(iconURL);
    // splat / so it can be safely used as a file name
    for (unsigned int i = 0; i < result.length(); ++i)
        if (result[i] == '/')
            result[i] = '_';

    QString ext = result.right(4);
    if (ext == ".ico" || ext == ".png" || ext == ".xpm")
        result.remove(result.length() - 4, 4);

    return result;
}

bool FaviconsModule::isIconOld(const QString &icon)
{
    struct stat st;
    if (stat(QFile::encodeName(icon), &st) != 0)
        return true; // Trigger a new download on error

    return (time(0) - st.st_mtime) > 604800; // arbitrary value (one week)
}

void FaviconsModule::setIconForURL(const KURL &url, const KURL &iconURL)
{
    QString simplifiedURL = simplifyURL(url);

    d->faviconsCache.insert(removeSlash(simplifiedURL), new QString(iconURL.url()) );

    QString iconName = "favicons/" + iconNameFromURL(iconURL);
    QString iconFile = d->faviconsDir + iconName + ".png";

    if (!isIconOld(iconFile)) {
        emit iconChanged(false, simplifiedURL, iconName);
        return;
    }

    startDownload(simplifiedURL, false, iconURL);
}

void FaviconsModule::downloadHostIcon(const KURL &url)
{
    QString iconFile = d->faviconsDir + "favicons/" + url.host() + ".png";
    if (!isIconOld(iconFile))
        return;

    startDownload(url.host(), true, KURL(url, "/favicon.ico"));
}

void FaviconsModule::startDownload(const QString &hostOrURL, bool isHost, const KURL &iconURL)
{
    // A failed icon is not asked for again for a while (every page of a
    // site would), but not for good: networks fail, sites get fixed.
    QMap<QString, time_t>::Iterator failed = d->failedDownloads.find(iconURL.url());
    if (failed != d->failedDownloads.end())
    {
        if (time(0) - failed.data() < 3600)
            return;
        d->failedDownloads.remove(failed);
    }

    KIO::Job *job = KIO::get(iconURL, false, false);
    job->addMetaData(d->metaData);
    connect(job, SIGNAL(data(KIO::Job *, const QByteArray &)), SLOT(slotData(KIO::Job *, const QByteArray &)));
    connect(job, SIGNAL(mimetype(KIO::Job *, const QString &)), SLOT(slotMimetype(KIO::Job *, const QString &)));
    connect(job, SIGNAL(result(KIO::Job *)), SLOT(slotResult(KIO::Job *)));
    connect(job, SIGNAL(infoMessage(KIO::Job *, const QString &)), SLOT(slotInfoMessage(KIO::Job *, const QString &)));
    FaviconsModulePrivate::DownloadInfo download;
    download.hostOrURL = hostOrURL;
    download.isHost = isHost;
    download.iconURL = iconURL;
    d->downloads.insert(job, download);
}

void FaviconsModule::slotData(KIO::Job *job, const QByteArray &data)
{
    FaviconsModulePrivate::DownloadInfo &download = d->downloads[job];
    if (d->killJobs.containsRef(job))
        return;
    unsigned int oldSize = download.iconData.size();
    // Icons with large sizes (256 pixel PNG entries, SVG) are bigger than
    // 64 KiB; a megabyte is enough for any of them.  A larger one is
    // killed once (not from inside the job's signal), which reports it as
    // failed.  (It used to be deleted as often as data came after the
    // limit, and with every big icon kded crashed.)
    if (oldSize + data.size() > 0x100000)
    {
        d->killJobs.append(job);
        QTimer::singleShot(0, this, SLOT(slotKill()));
        return;
    }
    download.iconData.resize(oldSize + data.size());
    memcpy(download.iconData.data() + oldSize, data.data(), data.size());
}

void FaviconsModule::slotMimetype(KIO::Job *job, const QString &type)
{
    d->downloads[job].mimetype = type;
}

void FaviconsModule::slotResult(KIO::Job *job)
{
    FaviconsModulePrivate::DownloadInfo download = d->downloads[job];
    d->downloads.remove(job);
    // A new job could get this address before slotKill() runs.
    d->killJobs.removeRef(job);
    KIO::TransferJob *transfer = static_cast<KIO::TransferJob *>(job);
    KURL iconURL = download.iconURL;
    QString iconName;
    // An error page (404 or 403 with HTML) is no icon; neither is any
    // other text: Qt would read it as a TGA image (TGA has no signature).
    if (!job->error() && !transfer->isErrorPage() && !download.mimetype.startsWith("text/"))
    {
        QBuffer buffer(download.iconData);
        buffer.open(IO_ReadOnly);
        QImageIO io;
        io.setIODevice(&buffer);
        io.setParameters("size=16");
        // An icon file: Qt's guess from the first bytes takes one whose
        // first entry is 256 pixels (00 in the directory) for a TGA image.
        const char *data = download.iconData.data();
        if (download.iconData.size() >= 6 && data[0] == 0 && data[1] == 0 &&
            (data[2] == 1 || data[2] == 2) && data[3] == 0 && (data[4] || data[5]))
            io.setFormat("ICO");
        // A small PNG may say it is 30000 x 30000 pixels: gigabytes for
        // kded.  No icon is larger than 1024 pixels.
        static const char pngSignature[] = "\x89PNG\r\n\x1a\n";
        bool tooLarge = false;
        if (download.iconData.size() >= 24 && memcmp(data, pngSignature, 8) == 0)
        {
            const unsigned char *u = reinterpret_cast<const unsigned char *>(data);
            unsigned long w = (u[16] << 24) | (u[17] << 16) | (u[18] << 8) | u[19];
            unsigned long h = (u[20] << 24) | (u[21] << 16) | (u[22] << 8) | u[23];
            tooLarge = w > 1024 || h > 1024;
        }
        // Check here too, the job might have had no error, but the downloaded
        // file contains just a 404 message sent with a 200 status.
        // microsoft.com does that... (malte)
        if (!tooLarge && io.read() && qstrcmp(io.format(), "TGA") != 0 && !io.image().isNull())
        {
            // Some sites have nasty 32x32 icons, according to the MS docs
            // IE ignores them, well, we scale them, otherwise the location
            // combo / menu will look quite ugly
            if (io.image().width() != KIcon::SizeSmall || io.image().height() != KIcon::SizeSmall)
                io.setImage(io.image().smoothScale(KIcon::SizeSmall, KIcon::SizeSmall));

            if (download.isHost)
                iconName = download.hostOrURL;
            else
                iconName = iconNameFromURL(iconURL);

            iconName = "favicons/" + iconName;

            io.setIODevice(0);
            io.setFileName(d->faviconsDir + iconName + ".png");
            io.setFormat("PNG");
            if (!io.write())
                iconName = QString::null;
            else if (!download.isHost)
            {
                // At once: kded is often ended by a signal (logout), and
                // pages whose icon is no /favicon.ico would have none.
                d->config->writeEntry( removeSlash(download.hostOrURL), iconURL.url());
                d->config->sync();
            }
        }
    }
    if (iconName.isEmpty())
        d->failedDownloads.insert(iconURL.url(), time(0));

    emit iconChanged(download.isHost, download.hostOrURL, iconName);
}

void FaviconsModule::slotInfoMessage(KIO::Job *job, const QString &msg)
{
    emit infoMessage(static_cast<KIO::TransferJob *>( job )->url(), msg);
}

void FaviconsModule::slotKill()
{
    QPtrList<KIO::Job> jobs = d->killJobs;
    d->killJobs.clear();
    for (KIO::Job *job = jobs.first(); job; job = jobs.next())
        if (d->downloads.contains(job))
            job->kill(false);   // slotResult(): an error, recorded as failed
}

extern "C" {
    KDE_EXPORT KDEDModule *create_favicons(const QCString &obj)
    {
        KImageIO::registerFormats();
        return new FaviconsModule(obj);
    }
}

// vim: ts=4 sw=4 et
