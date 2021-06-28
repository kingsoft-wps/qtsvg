/****************************************************************************
**
** Copyright (C) 2016 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of the plugins of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 3 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL3 included in the
** packaging of this file. Please review the following information to
** ensure the GNU Lesser General Public License version 3 requirements
** will be met: https://www.gnu.org/licenses/lgpl-3.0.html.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 2.0 or (at your option) the GNU General
** Public license version 3 or any later version approved by the KDE Free
** Qt Foundation. The licenses are as published by the Free Software
** Foundation and appearing in the file LICENSE.GPL2 and LICENSE.GPL3
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-2.0.html and
** https://www.gnu.org/licenses/gpl-3.0.html.
**
** $QT_END_LICENSE$
**
****************************************************************************/
#include "qsvgiconengine.h"

#ifndef QT_NO_SVGRENDERER

#include "qpainter.h"
#include "qpixmap.h"
#include "qsvgrenderer.h"
#include "qpixmapcache.h"
#include "qfileinfo.h"
#include <qmimedatabase.h>
#include <qmimetype.h>
#include <QAtomicInt>
#include "qdebug.h"
#include <private/qguiapplication_p.h>

QT_BEGIN_NAMESPACE

static inline int pmKey(const QSize &size, QIcon::Mode mode, QIcon::State state)
{
    return ((((((size.width()<<11)|size.height())<<11)|mode)<<4)|state);
} 

class QSvgIconEnginePrivate : public QSharedData
{
public:
    QSvgIconEnginePrivate()
        : svgBuffers(0), addedPixmaps(0)
        , isMultiSize(false), isCacheBuf(true)
        , ratioMode(Qt::KeepAspectRatio)
        , svgClassMap(0)
        { stepSerialNum(); }

    ~QSvgIconEnginePrivate()
        {
            delete addedPixmaps;
            delete svgBuffers;
            delete svgClassMap;
        }

    static int hashKey(QIcon::Mode mode, QIcon::State state)
        { return (((mode)<<4)|state); }

    QString pmcKey(const QSize &size, QIcon::Mode mode, QIcon::State state)
        { return QLatin1String("$qt_svgicon_")
                 + QString::number(serialNum, 16).append(QLatin1Char('_'))
                 + QString::number((((((qint64(size.width()) << 11) | size.height()) << 11) | mode) << 4) | state, 16); }

    int uniqueKey(const QSize &size, QIcon::Mode mode, QIcon::State state) const
        { 
            if (isMultiSize)
                return pmKey(size, mode, state);
            return hashKey(mode, state); }

    void stepSerialNum()
        { serialNum = lastSerialNum.fetchAndAddRelaxed(1); }

    bool tryLoad(QSvgRenderer *renderer, QSize size, QIcon::Mode mode, QIcon::State state, bool bTryMatchSize);
    QIcon::Mode loadDataForModeAndState(QSvgRenderer *renderer, QSize size, QIcon::Mode mode, QIcon::State state);

    QPair<QString, bool> tryMatch(const QSize &size, QIcon::Mode mode, QIcon::State state);
    void addSvgClass(QIcon::Mode mode, QIcon::State state, const QStringList &svgClass);

    QHash<int, QString> svgFiles;
    QHash<int, int> svgFilesMode;
    QHash<int, QByteArray> *svgBuffers;
    QHash<int, QPixmap> *addedPixmaps;
    QMap<int, QString> integerMap;
    QMap<float, QString> nonIntegerMap;
    QMap<int, QStringList> *svgClassMap;
    int serialNum;
    bool isMultiSize;
    bool isCacheBuf;
    Qt::AspectRatioMode ratioMode;
    typedef QMap<QString, QMap<QString, QVariant>> ClassProperties;
    QMap<int, ClassProperties> svgClassProperties;
    QStringList oldCacheKeys;

    static QAtomicInt lastSerialNum;
};

QAtomicInt QSvgIconEnginePrivate::lastSerialNum;

QSvgIconEngine::QSvgIconEngine()
    : d(new QSvgIconEnginePrivate)
{
}

QSvgIconEngine::QSvgIconEngine(const QSvgIconEngine &other)
    : QIconEngine(other), d(new QSvgIconEnginePrivate)
{
    d->svgFiles = other.d->svgFiles;
    d->svgFilesMode = other.d->svgFilesMode;
    d->integerMap = other.d->integerMap;
    d->nonIntegerMap = other.d->nonIntegerMap;
    d->serialNum = other.d->serialNum;
    d->isMultiSize = other.d->isMultiSize;
    d->isCacheBuf = other.d->isCacheBuf;
    d->ratioMode = other.d->ratioMode;
    d->svgClassProperties = other.d->svgClassProperties;
    d->oldCacheKeys = other.d->oldCacheKeys;
    if (other.d->svgBuffers)
        d->svgBuffers = new QHash<int, QByteArray>(*other.d->svgBuffers);
    if (other.d->addedPixmaps)
        d->addedPixmaps = new QHash<int, QPixmap>(*other.d->addedPixmaps);
    if (other.d->svgClassMap)
        d->svgClassMap = new QMap<int, QStringList>(*other.d->svgClassMap);
}


QSvgIconEngine::~QSvgIconEngine()
{
}


QSize QSvgIconEngine::actualSize(const QSize &size, QIcon::Mode mode,
                                 QIcon::State state)
{
    if (d->addedPixmaps) {
        QPixmap pm = d->addedPixmaps->value(d->uniqueKey(size, mode, state));
        if (!pm.isNull() && pm.size() == size)
            return size;
    }

    QPixmap pm = pixmap(size, mode, state);
    if (pm.isNull())
        return QSize();
    return pm.size();
}

static QByteArray maybeUncompress(const QByteArray &ba)
{
#ifndef QT_NO_COMPRESS
    return qUncompress(ba);
#else
    return ba;
#endif
}

bool QSvgIconEngine::isPixmapCache(const QSize& size, QIcon::Mode mode, QIcon::State state) const
{
    if (d->addedPixmaps) {
        QPixmap pm = d->addedPixmaps->value(d->uniqueKey(size, mode, state));
        if (!pm.isNull() && pm.size() == size)
            return true;    
    }

    return false;
}

void QSvgIconEngine::setPolicy(const bool& isMulti, const bool& isCache, const Qt::AspectRatioMode& mode)
{
    d->isMultiSize = isMulti;
    d->isCacheBuf = isCache;
    d->ratioMode = mode;
}

bool QSvgIconEnginePrivate::tryLoad(QSvgRenderer *renderer, QSize size, 
                                    QIcon::Mode mode, QIcon::State state, bool bTryMatchSize)
{
    if (svgBuffers) {
        QByteArray buf = svgBuffers->value(uniqueKey(size, mode, state));
        if (!buf.isEmpty()) {
            buf = maybeUncompress(buf);
            renderer->load(buf);
            return true;
        }
    }

    QString svgFile;
    if (isMultiSize && bTryMatchSize) {
        svgFile = svgFiles.value(pmKey(size, mode, state));
    } else {
        svgFile = svgFiles.value(hashKey(mode, state));
    }

    if (!svgFile.isEmpty()) {
        renderer->load(svgFile, svgClassProperties.value(hashKey(mode, state)));
        return true;
    }
    return false;
}

QIcon::Mode QSvgIconEnginePrivate::loadDataForModeAndState(QSvgRenderer *renderer, QSize size, QIcon::Mode mode, QIcon::State state)
{
    if (tryLoad(renderer, size, mode, state, true))
        return mode;

    if (isMultiSize)
    {
        QPair<QString, bool> result = tryMatch(size, mode, state);
        if (!result.first.isEmpty())
        {
            renderer->load(result.first, svgClassProperties.value(hashKey(mode, state)));
            return result.second ? mode : QIcon::Normal;
        }
    }

    const QIcon::State oppositeState = (state == QIcon::On) ? QIcon::Off : QIcon::On;
    if (mode == QIcon::Disabled || mode == QIcon::Selected) {
        const QIcon::Mode oppositeMode = (mode == QIcon::Disabled) ? QIcon::Selected : QIcon::Disabled;
        if (tryLoad(renderer, size, QIcon::Normal, state, false))
            return QIcon::Normal;
        if (tryLoad(renderer, size, QIcon::Active, state, false))
            return QIcon::Active;
        if (tryLoad(renderer, size, mode, oppositeState, false))
            return mode;
        if (tryLoad(renderer, size, QIcon::Normal, oppositeState, false))
            return QIcon::Normal;
        if (tryLoad(renderer, size, QIcon::Active, oppositeState, false))
            return QIcon::Active;
        if (tryLoad(renderer,size,  oppositeMode, state, false))
            return oppositeMode;
        if (tryLoad(renderer, size, oppositeMode, oppositeState, false))
            return oppositeMode;
    } else {
        const QIcon::Mode oppositeMode = (mode == QIcon::Normal) ? QIcon::Active : QIcon::Normal;
        if (tryLoad(renderer, size, oppositeMode, state, false))
            return oppositeMode;
        if (tryLoad(renderer, size, mode, oppositeState, false))
            return mode;
        if (tryLoad(renderer, size, oppositeMode, oppositeState, false))
            return oppositeMode;
        if (tryLoad(renderer, size, QIcon::Disabled, state, false))
            return QIcon::Disabled;
        if (tryLoad(renderer, size, QIcon::Selected, state, false))
            return QIcon::Selected;
        if (tryLoad(renderer, size, QIcon::Disabled, oppositeState, false))
            return QIcon::Disabled;
        if (tryLoad(renderer, size, QIcon::Selected, oppositeState, false))
            return QIcon::Selected;
    }
    return QIcon::Normal;
}

QPair<QString, bool> QSvgIconEnginePrivate::tryMatch(const QSize &size, QIcon::Mode mode, QIcon::State state)
{
    Q_UNUSED(state);
    QString svgMatchFile;
    bool bSameMode = false;

    QList<int> keys = svgFilesMode.keys();
    for (int key : keys) {
        //find same mode
        if (mode == svgFilesMode.value(key)) {
            bSameMode = true;
            QString svgFile = svgFiles.value(key);
            svgMatchFile = svgFile;
            QPixmap pixmap(svgFile);
            QSize fileSize = pixmap.size();
            float scale = (float)size.width()/(float)fileSize.width();
            //integer multiple
            if (scale == (int)scale) {
                integerMap.insert(scale, svgFile);
            } else {
                nonIntegerMap.insert(scale, svgFile);
            }
        }
    }

    //choose minimum integer or nonInteger multiple file
    if (!integerMap.isEmpty()) {
        svgMatchFile = integerMap.begin().value();
    } else if (!nonIntegerMap.isEmpty()) {
        svgMatchFile = nonIntegerMap.begin().value();
    }

    //no same mode file,use normal file
    if (!bSameMode) {
        svgMatchFile = svgFiles.value(pmKey(size, QIcon::Normal, QIcon::Off));
        if (svgMatchFile.isEmpty() && !svgFiles.isEmpty())
            svgMatchFile = svgFiles.begin().value();
    }

    return qMakePair(svgMatchFile, bSameMode);
}

void QSvgIconEnginePrivate::addSvgClass(QIcon::Mode mode, QIcon::State state,
                                        const QStringList &svgClass)
{
    if (!svgClassMap)
        svgClassMap = new QMap<int, QStringList>();

    int key = hashKey(mode, state);
    QMap<int, QStringList>::iterator it = svgClassMap->find(key);
    if (it != svgClassMap->end()) {
        for (QString className : svgClass) {
            if (!it.value().contains(className))
                it.value().append(className);
        }
    } else {
        svgClassMap->insert(key, svgClass);
    }
}

QPixmap QSvgIconEngine::pixmap(const QSize &size, QIcon::Mode mode,
                               QIcon::State state)
{
    QPixmap pm;

    QString pmckey(d->pmcKey(size, mode, state));
    if (QPixmapCache::find(pmckey, pm))
        return pm;

    if (d->addedPixmaps) {
        pm = d->addedPixmaps->value(d->uniqueKey(size, mode, state));
        if (!pm.isNull() && pm.size() == size)
            return pm;
    }

    QSvgRenderer renderer;
    const QIcon::Mode loadmode = d->loadDataForModeAndState(&renderer, size, mode, state);
    if (!renderer.isValid())
        return pm;

    QSize actualSize = renderer.defaultSize();
    if (!actualSize.isNull())
        actualSize.scale(size, d->ratioMode);

    if (actualSize.isEmpty())
        return QPixmap();

    QImage img(actualSize, QImage::Format_ARGB32_Premultiplied);
    img.fill(0x00000000);
    QPainter p(&img);
    renderer.render(&p);
    p.end();
    pm = QPixmap::fromImage(img);
    if (qobject_cast<QGuiApplication *>(QCoreApplication::instance())) {
        if (loadmode != mode && mode != QIcon::Normal) {
            const QPixmap generated = QGuiApplicationPrivate::instance()->applyQIconStyleHelper(mode, pm);
            if (!generated.isNull())
                pm = generated;
        }
    }

    if (!pm.isNull()) {
        QPixmapCache::insert(pmckey, pm);
        d->oldCacheKeys.append(pmckey);
    }

    return pm;
}


void QSvgIconEngine::addPixmap(const QPixmap &pixmap, QIcon::Mode mode,
                               QIcon::State state)
{
    if (!d->addedPixmaps)
        d->addedPixmaps = new QHash<int, QPixmap>;
    d->stepSerialNum();
    d->addedPixmaps->insert(d->uniqueKey(pixmap.size(), mode, state), pixmap);
}

enum FileType { OtherFile, SvgFile, CompressedSvgFile };

static FileType fileType(const QFileInfo &fi)
{
    const QString &abs = fi.absoluteFilePath();
    if (abs.endsWith(QLatin1String(".svg"), Qt::CaseInsensitive))
        return SvgFile;
    if (abs.endsWith(QLatin1String(".svgz"), Qt::CaseInsensitive)
        || abs.endsWith(QLatin1String(".svg.gz"), Qt::CaseInsensitive)) {
        return CompressedSvgFile;
    }
#ifndef QT_NO_MIMETYPE
    const QString &mimeTypeName = QMimeDatabase().mimeTypeForFile(fi).name();
    if (mimeTypeName == QLatin1String("image/svg+xml"))
        return SvgFile;
    if (mimeTypeName == QLatin1String("image/svg+xml-compressed"))
        return CompressedSvgFile;
#endif // !QT_NO_MIMETYPE
    return OtherFile;
}

void QSvgIconEngine::addFile(const QString &fileName, const QSize & size,
                             QIcon::Mode mode, QIcon::State state)
{
    if (!fileName.isEmpty()) {
         const QFileInfo fi(fileName);
         const QString abs = fi.absoluteFilePath();
         const FileType type = fileType(fi);
#ifndef QT_NO_COMPRESS
         if (type == SvgFile || type == CompressedSvgFile) {
#else
         if (type == SvgFile) {
#endif
             QSvgRenderer renderer(abs);
             if (renderer.isValid()) {
                 d->stepSerialNum();
                 d->addSvgClass(mode, state, renderer.xmlClassList());
                 if (d->isMultiSize) {
                    QSize recordSize = size;
                    if (recordSize == QSize()) {
                        recordSize = QPixmap(abs).size();
                    }
                    d->svgFiles.insert(pmKey(recordSize, mode, state), abs);
                    d->svgFilesMode.insert(pmKey(recordSize, mode, state), mode);
                } else {
                    d->svgFiles.insert(d->hashKey(mode, state), abs);
                }
             }
         } else if (type == OtherFile) {
             QPixmap pm(abs);
             if (!pm.isNull())
                 addPixmap(pm, mode, state);
         }
    }
}

QStringList QSvgIconEngine::styleClassList(QIcon::Mode mode, QIcon::State state) const
{
    return d->svgClassMap ? (*d->svgClassMap).value(d->hashKey(mode, state)) : QStringList();
}

void QSvgIconEngine::setStyleClass(const QString &xmlClass, const QString &property,
                                   const QVariant &value, QIcon::Mode mode, QIcon::State state)
{
    int key = d->hashKey(mode, state);
    QMap<int, QSvgIconEnginePrivate::ClassProperties>::iterator it =
            d->svgClassProperties.find(key);
    if (it == d->svgClassProperties.end()) {
        QSvgIconEnginePrivate::ClassProperties classProperties;
        QMap<QString, QVariant> propertyMap;
        propertyMap.insert(property, value);
        classProperties.insert(xmlClass, propertyMap);
        d->svgClassProperties.insert(key, classProperties);
    } else {
        QSvgIconEnginePrivate::ClassProperties::iterator jt = it.value().find(xmlClass);
        if (jt == it.value().end()) {
            QMap<QString, QVariant> propertyMap;
            propertyMap.insert(property, value);
            it.value().insert(xmlClass, propertyMap);
        } else {
            jt->insert(property, value);
        }
    }

    for (QString oldKey : d->oldCacheKeys) {
        QPixmapCache::remove(oldKey);
    }
    d->oldCacheKeys.clear();
}

void QSvgIconEngine::clearStyleClass(QIcon::Mode mode, QIcon::State state)
{
    d->svgClassProperties.remove(d->hashKey(mode, state));
}

void QSvgIconEngine::paint(QPainter *painter, const QRect &rect,
                           QIcon::Mode mode, QIcon::State state)
{
    QSize pixmapSize = rect.size();
    if (painter->device())
        pixmapSize *= painter->device()->devicePixelRatioF();
    painter->drawPixmap(rect, pixmap(pixmapSize, mode, state));
}

QString QSvgIconEngine::key() const
{
    return QLatin1String("svg");
}

QIconEngine *QSvgIconEngine::clone() const
{
    return new QSvgIconEngine(*this);
}


bool QSvgIconEngine::read(QDataStream &in)
{
    d = new QSvgIconEnginePrivate;
    int tempRatioMode = 0;
    in >> d->isMultiSize >> d->isCacheBuf >> tempRatioMode;
    d->ratioMode = static_cast<Qt::AspectRatioMode>(tempRatioMode);
    if (d->isCacheBuf) {
        d->svgBuffers = new QHash<int, QByteArray>;
    }

    if (in.version() >= QDataStream::Qt_4_4) {
        if (d->isCacheBuf) {
            int isCompressed;
            if (d->isMultiSize) {
                in >> d->svgFiles >> d->svgFilesMode >> isCompressed >> *d->svgBuffers;
            } else {
                QHash<int, QString> fileNames;  // For memoryoptimization later
                in >> fileNames >> isCompressed >> *d->svgBuffers;
            }
#ifndef QT_NO_COMPRESS
            if (!isCompressed) {
                for (auto it = d->svgBuffers->begin(), end = d->svgBuffers->end(); it != end; ++it)
                    it.value() = qCompress(it.value());
            }
#else
            if (isCompressed) {
                qWarning("QSvgIconEngine: Can not decompress SVG data");
                d->svgBuffers->clear();
            }
#endif
        } else {
            if (d->isMultiSize) {
                in >> d->svgFiles >> d->svgFilesMode;
            } else{
                in >> d->svgFiles;
            }
        }
        int hasAddedPixmaps;
        in >> hasAddedPixmaps;
        if (hasAddedPixmaps) {
            d->addedPixmaps = new QHash<int, QPixmap>;
            in >> *d->addedPixmaps;
        }
    }
    else {
        QPixmap pixmap;
        QByteArray data;
        uint mode;
        uint state;
        int num_entries;

        in >> data;
        if (!data.isEmpty()) {
#ifndef QT_NO_COMPRESS
            data = qUncompress(data);
#endif
            if (!data.isEmpty())
                d->svgBuffers->insert(d->hashKey(QIcon::Normal, QIcon::Off), data);
        }
        in >> num_entries;
        for (int i=0; i<num_entries; ++i) {
            if (in.atEnd())
                return false;
            in >> pixmap;
            in >> mode;
            in >> state;
            // The pm list written by 4.3 is buggy and/or useless, so ignore.
            //addPixmap(pixmap, QIcon::Mode(mode), QIcon::State(state));
        }
    }

    return true;
}


bool QSvgIconEngine::write(QDataStream &out) const
{
    if (out.version() >= QDataStream::Qt_4_4) {
        out << d->isMultiSize << d->isCacheBuf << d->ratioMode;
    if (d->isCacheBuf) {
        int isCompressed = 0;
#ifndef QT_NO_COMPRESS
        isCompressed = 1;
#endif
        QHash<int, QByteArray> svgBuffers;
        if (d->svgBuffers)
            svgBuffers = *d->svgBuffers;
        for (auto it = d->svgFiles.cbegin(), end = d->svgFiles.cend(); it != end; ++it) {
            QByteArray buf;
            QFile f(it.value());
            if (f.open(QIODevice::ReadOnly))
                buf = f.readAll();
#ifndef QT_NO_COMPRESS
            buf = qCompress(buf);
#endif
            svgBuffers.insert(it.key(), buf);
        }
        if (d->isMultiSize) {
            out << d->svgFiles << d->svgFilesMode << isCompressed << svgBuffers;
        } else {
            out << d->svgFiles << isCompressed << svgBuffers;
        }

    } else {
        if (d->isMultiSize) {
            out << d->svgFiles << d->svgFilesMode;
        } else {
            out << d->svgFiles;
        }
    }

        if (d->addedPixmaps)
            out << (int)1 << *d->addedPixmaps;
        else
            out << (int)0;
    }
    else {
        QByteArray buf;
        if (d->svgBuffers)
            buf = d->svgBuffers->value(d->hashKey(QIcon::Normal, QIcon::Off));
        if (buf.isEmpty()) {
            QString svgFile = d->svgFiles.value(d->hashKey(QIcon::Normal, QIcon::Off));
            if (!svgFile.isEmpty()) {
                QFile f(svgFile);
                if (f.open(QIODevice::ReadOnly))
                    buf = f.readAll();
            }
        }
#ifndef QT_NO_COMPRESS
        buf = qCompress(buf);
#endif
        out << buf;
        // 4.3 has buggy handling of added pixmaps, so don't write any
        out << (int)0;
    }
    return true;
}

void QSvgIconEngine::virtual_hook(int id, void *data)
{
    if (id == QIconEngine::IsNullHook) {
        *reinterpret_cast<bool*>(data) = !d->isMultiSize && d->svgFiles.isEmpty() && !d->addedPixmaps && (!d->svgBuffers || d->svgBuffers->isEmpty());
    }
    QIconEngine::virtual_hook(id, data);
}

QT_END_NAMESPACE

#endif // QT_NO_SVGRENDERER
