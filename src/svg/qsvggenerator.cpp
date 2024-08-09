/****************************************************************************
**
** Copyright (C) 2016 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of the Qt SVG module of the Qt Toolkit.
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

#include "qsvggenerator.h"

#ifndef QT_NO_SVGGENERATOR

#include "qpainterpath.h"

#include "private/qpaintengine_p.h"
#include "private/qtextengine_p.h"
#include "private/qdrawhelper_p.h"

#include "qfile.h"
#include "qtextcodec.h"
#include "qtextstream.h"
#include "qbuffer.h"
#include "qmath.h"
#include "qbitmap.h"
#include "qcryptographichash.h"

#include "qdebug.h"

QT_BEGIN_NAMESPACE

static void translate_color(const QColor &color, QString *color_string,
                            QString *opacity_string)
{
    Q_ASSERT(color_string);
    Q_ASSERT(opacity_string);

    *color_string =
        QString::fromLatin1("#%1%2%3")
        .arg(color.red(), 2, 16, QLatin1Char('0'))
        .arg(color.green(), 2, 16, QLatin1Char('0'))
        .arg(color.blue(), 2, 16, QLatin1Char('0'));
    *opacity_string = QString::number(color.alphaF());
}

static void translate_dashPattern(const QVector<qreal> &pattern, qreal width, QString *pattern_string)
{
    Q_ASSERT(pattern_string);

    // Note that SVG operates in absolute lengths, whereas Qt uses a length/width ratio.
    for (qreal entry : pattern)
        *pattern_string += QString::fromLatin1("%1,").arg(entry * width);

    pattern_string->chop(1);
}

class QSvgPaintEnginePrivate : public QPaintEnginePrivate
{
public:
    QSvgPaintEnginePrivate()
    {
        size = QSize();
        viewBox = QRectF();
        outputDevice = 0;
        resolution = 72;
        clipPathEnabled = false;
        textCoordEnabled = false;
        trimEmptyGroupEnabled = false;
        dataPrefix = "";

        attributes.document_title = QLatin1String("Qt SVG Document");
        attributes.document_description = QLatin1String("Generated with Qt");
        attributes.font_family = QLatin1String("serif");
        attributes.font_size = QLatin1String("10pt");
        attributes.font_style = QLatin1String("normal");
        attributes.font_weight = QLatin1String("normal");

        afterFirstUpdate = false;
        discardStashStream = false;
        numGradients = 0;
        numClipPaths = 0;
    }

    QSize size;
    QRectF viewBox;
    QIODevice *outputDevice;
    QTextStream *stream;
    QScopedPointer<QTextStream> stashStream;
    int resolution;
    bool clipPathEnabled;
    bool textCoordEnabled;
    bool trimEmptyGroupEnabled;
    QString dataPrefix;

    QString header;
    QString defs;
    QString body;
    bool    afterFirstUpdate;
    bool    discardStashStream;
    QString stashContent;

    QBrush brush;
    QPen pen;
    QMatrix matrix;
    QFont font;
    QPainterPath clipPath;
    QMatrix matrixClipPath; // the matrix when calc clipPath.

    QString generateGradientName(const QString &prefix) {
        ++numGradients;
        currentGradientName = prefix + QString::fromLatin1("gradient%1").arg(numGradients);
        return currentGradientName;
    }

    QString currentGradientName;
    int numGradients;

    bool getClipPathId(QString &id) {
        QRectF rect = clipPath.boundingRect();
        QVector<qreal> hashKey = {rect.x(), rect.y(), rect.width(), rect.height()};
        auto &vector = clipPathCache[hashKey];
        for (const auto p : vector) {
            if (clipPath == p.first) {
                id = p.second;
                return false;
            }
        }
        id = QString::fromLatin1("clipPath%1_%2").arg(dataPrefix).arg(++numClipPaths);
        vector.push_back(qMakePair(clipPath, id));
        return true;
    }

    int numClipPaths;
    QHash<QVector<qreal>, QVector<QPair<QPainterPath, QString>>> clipPathCache;

    QStringList savedPatternBrushes;
    QStringList savedPatternMasks;

    struct _attributes {
        QString document_title;
        QString document_description;
        QString font_weight;
        QString font_size;
        QString font_family;
        QString font_style;
        QString stroke, strokeOpacity;
        QString dashPattern, dashOffset;
        QString fill, fillOpacity;
    } attributes;
};

static inline QPaintEngine::PaintEngineFeatures svgEngineFeatures()
{
    return QPaintEngine::PaintEngineFeatures(
        QPaintEngine::AllFeatures
        & ~QPaintEngine::PerspectiveTransform
        & ~QPaintEngine::ConicalGradientFill
        & ~QPaintEngine::PorterDuff);
}

Q_GUI_EXPORT QImage qt_imageForBrush(int brushStyle, bool invert);

class QSvgPaintEngine : public QPaintEngine
{
    Q_DECLARE_PRIVATE(QSvgPaintEngine)
public:

    QSvgPaintEngine()
        : QPaintEngine(*new QSvgPaintEnginePrivate,
                       svgEngineFeatures())
    {
    }

    bool begin(QPaintDevice *device) override;
    bool end() override;

    void updateState(const QPaintEngineState &state) override;
    void popGroup();

    void drawEllipse(const QRectF &r) override;
    void drawPath(const QPainterPath &path) override;
    void drawPixmap(const QRectF &r, const QPixmap &pm, const QRectF &sr) override;
    void drawPolygon(const QPointF *points, int pointCount, PolygonDrawMode mode) override;
    void drawRects(const QRectF *rects, int rectCount) override;
    void drawTextItem(const QPointF &pt, const QTextItem &item) override;
    void drawImage(const QRectF &r, const QImage &pm, const QRectF &sr,
                   Qt::ImageConversionFlags flags = Qt::AutoColor) override;

    QPaintEngine::Type type() const override { return QPaintEngine::SVG; }

    QSize size() const { return d_func()->size; }
    void setSize(const QSize &size) {
        Q_ASSERT(!isActive());
        d_func()->size = size;
    }

    QRectF viewBox() const { return d_func()->viewBox; }
    void setViewBox(const QRectF &viewBox) {
        Q_ASSERT(!isActive());
        d_func()->viewBox = viewBox;
    }

    QString documentTitle() const { return d_func()->attributes.document_title; }
    void setDocumentTitle(const QString &title) {
        d_func()->attributes.document_title = title;
    }

    QString documentDescription() const { return d_func()->attributes.document_description; }
    void setDocumentDescription(const QString &description) {
        d_func()->attributes.document_description = description;
    }

    QIODevice *outputDevice() const { return d_func()->outputDevice; }
    void setOutputDevice(QIODevice *device) {
        Q_ASSERT(!isActive());
        d_func()->outputDevice = device;
    }

    int resolution() { return d_func()->resolution; }
    void setResolution(int resolution) {
        Q_ASSERT(!isActive());
        d_func()->resolution = resolution;
    }

    bool clipPathEnabled() { return d_func()->clipPathEnabled; }
    void setClipPathEnabled(bool enable) {
        Q_ASSERT(!isActive());
        d_func()->clipPathEnabled = enable;
    }

    bool textCoordEnabled() { return d_func()->textCoordEnabled; }
    void setTextCoordEnabled(bool enable) {
        Q_ASSERT(!isActive());
        d_func()->textCoordEnabled = enable;
    }

    bool trimEmptyGroupEnabled() { return d_func()->trimEmptyGroupEnabled; }
    void setTrimEmptyGroupEnabled(bool enable) {
        Q_ASSERT(!isActive());
        d_func()->trimEmptyGroupEnabled = enable;
    }

    QString dataPrefix() { return d_func()->dataPrefix; }
    void setDataPrefix(const QString prefix) {
        Q_ASSERT(!isActive());
        d_func()->dataPrefix = prefix;
    }

    void beginGroup(const QMap<QString, QString> *attrs)
    {
        Q_D(QSvgPaintEngine);
        if (d->afterFirstUpdate && !d->discardStashStream)
        {
            *d->stream << "</g>\n\n";
            d->afterFirstUpdate = false;
        }
        *d->stream << "<g ";
        if (attrs)
        {
            for (auto it = attrs->begin(); it != attrs->end(); ++it)
            {
                *d->stream << it.key() << '=' << it.value() << ' ';
            }
        }
        *d->stream << '>' << endl;
    }

    void endGroup()
    {
        Q_D(QSvgPaintEngine);
        if (d->afterFirstUpdate && !d->discardStashStream)
        {
            *d->stream << "</g>\n\n";
            d->afterFirstUpdate = false;
        }

        *d->stream << "</g>" << endl;
    }

    QString savePatternMask(Qt::BrushStyle style)
    {
        QString maskId = QString(QStringLiteral("patternmask%1")).arg(style);
        if (!d_func()->savedPatternMasks.contains(maskId)) {
            QImage img = qt_imageForBrush(style, true);
            QRegion reg(QBitmap::fromData(img.size(), img.constBits()));
            QString rct(QStringLiteral("<rect x=\"%1\" y=\"%2\" width=\"%3\" height=\"%4\" />"));
            QTextStream str(&d_func()->defs, QIODevice::Append);
            str << "<mask id=\"" << maskId << "\" x=\"0\" y=\"0\" width=\"8\" height=\"8\" "
                << "stroke=\"none\" fill=\"#ffffff\" patternUnits=\"userSpaceOnUse\" >" << endl;
            for (QRect r : reg)
                str << rct.arg(r.x()).arg(r.y()).arg(r.width()).arg(r.height()) << endl;
            str << QStringLiteral("</mask>") << endl << endl;
            d_func()->savedPatternMasks.append(maskId);
        }
        return maskId;
    }

    QString savePatternBrush(const QString &color, const QBrush &brush)
    {
        QString patternId = QString(QStringLiteral("fillpattern%1_")).arg(brush.style()) + color.midRef(1);
        if (!d_func()->savedPatternBrushes.contains(patternId)) {
            QString maskId = savePatternMask(brush.style());
            QString geo(QStringLiteral("x=\"0\" y=\"0\" width=\"8\" height=\"8\""));
            QTextStream str(&d_func()->defs, QIODevice::Append);
            str << QString(QStringLiteral("<pattern id=\"%1\" %2 patternUnits=\"userSpaceOnUse\" >")).arg(patternId, geo) << endl;
            str << QString(QStringLiteral("<rect %1 stroke=\"none\" fill=\"%2\" mask=\"url(#%3);\" />")).arg(geo, color, maskId) << endl;
            str << QStringLiteral("</pattern>") << endl << endl;
            d_func()->savedPatternBrushes.append(patternId);
        }
        return patternId;
    }

    QString saveTexturePatternBrush(int x, int y, const QBrush &brush)
    {
        QImage img = brush.textureImage();
        if (img.isNull())
            return QString();

        QSize szImg = img.size();
        if (szImg.width() <= 0 || szImg.height() <= 0)
            return QString();

        QTransform trans = brush.transform();
        if (trans.isScaling()) {
            szImg.setWidth(qRound(szImg.width() * trans.m11()));
            szImg.setHeight(qRound(szImg.height() * trans.m22()));
        }

        if (x < 0 || x >= szImg.width()) {
            x = (x - x / szImg.width() * szImg.width() + szImg.width()) % szImg.width();
        }
        if (y < 0 || y >= szImg.height()) {
            y = (y - y / szImg.height() * szImg.height() + szImg.height()) % szImg.height();
        }

        QByteArray data;
        QBuffer buffer(&data);
        buffer.open(QBuffer::ReadWrite);
        img.save(&buffer, "PNG");
        buffer.close();

        QCryptographicHash hash(QCryptographicHash::Md5);
        hash.addData(data.toBase64());
        QString imgMd5 = hash.result().toHex();

        QString patternId = QString(QStringLiteral("filltexturepattern%1_%2_%3_%4_%5"))
                                    .arg(imgMd5)
                                    .arg(x)
                                    .arg(y)
                                    .arg(szImg.width())
                                    .arg(szImg.height());
        if (!d_func()->savedPatternBrushes.contains(patternId)) {
            QTextStream str(&d_func()->defs, QIODevice::Append);
            str << QString(QStringLiteral("<pattern id=\"%1\" x=\"%2\" y=\"%3\" width=\"%4\" height=\"%5\" patternUnits=\"userSpaceOnUse\">"))
                            .arg(patternId)
                            .arg(x)
                            .arg(y)
                            .arg(szImg.width())
                            .arg(szImg.height())
                << endl;
            str << QString(QStringLiteral("<image "));
            str << QString(QStringLiteral("width=\"%1\" height=\"%2\" preserveAspectRatio=\"none\" "))
                            .arg(szImg.width())
                            .arg(szImg.height());
            str << QString(QStringLiteral("xlink:href=\"data:image/png;base64,")) << data.toBase64()
                << QString(QStringLiteral("\"/>")) << endl;
            str << QStringLiteral("</pattern>") << endl << endl;
            d_func()->savedPatternBrushes.append(patternId);
        }

        return patternId;
    }

    void saveLinearGradientBrush(const QGradient *g)
    {
        QTextStream str(&d_func()->defs, QIODevice::Append);
        const QLinearGradient *grad = static_cast<const QLinearGradient*>(g);
        str << QLatin1String("<linearGradient ");
        saveGradientUnits(str, g);
        if (grad) {
            str << QLatin1String("x1=\"") <<grad->start().x()<< QLatin1String("\" ")
                << QLatin1String("y1=\"") <<grad->start().y()<< QLatin1String("\" ")
                << QLatin1String("x2=\"") <<grad->finalStop().x() << QLatin1String("\" ")
                << QLatin1String("y2=\"") <<grad->finalStop().y() << QLatin1String("\" ");
        }

        str << QLatin1String("id=\"") << d_func()->generateGradientName(dataPrefix())<< QLatin1String("\">\n");
        saveGradientStops(str, g);
        str << QLatin1String("</linearGradient>") <<endl;
    }
    void saveRadialGradientBrush(const QGradient *g)
    {
        QTextStream str(&d_func()->defs, QIODevice::Append);
        const QRadialGradient *grad = static_cast<const QRadialGradient*>(g);
        str << QLatin1String("<radialGradient ");
        saveGradientUnits(str, g);
        if (grad) {
            str << QLatin1String("cx=\"") <<grad->center().x()<< QLatin1String("\" ")
                << QLatin1String("cy=\"") <<grad->center().y()<< QLatin1String("\" ")
                << QLatin1String("r=\"") <<grad->radius() << QLatin1String("\" ")
                << QLatin1String("fx=\"") <<grad->focalPoint().x() << QLatin1String("\" ")
                << QLatin1String("fy=\"") <<grad->focalPoint().y() << QLatin1String("\" ");
        }
        str << QLatin1String("id=\"") <<d_func()->generateGradientName(dataPrefix())<< QLatin1String("\">\n");
        saveGradientStops(str, g);
        str << QLatin1String("</radialGradient>") << endl;
    }
    void saveConicalGradientBrush(const QGradient *)
    {
        qWarning("svg's don't support conical gradients!");
    }

    void saveGradientStops(QTextStream &str, const QGradient *g) {
        QGradientStops stops = g->stops();

        if (g->interpolationMode() == QGradient::ColorInterpolation) {
            bool constantAlpha = true;
            int alpha = stops.at(0).second.alpha();
            for (int i = 1; i < stops.size(); ++i)
                constantAlpha &= (stops.at(i).second.alpha() == alpha);

            if (!constantAlpha) {
                const qreal spacing = qreal(0.02);
                QGradientStops newStops;
                QRgb fromColor = qPremultiply(stops.at(0).second.rgba());
                QRgb toColor;
                for (int i = 0; i + 1 < stops.size(); ++i) {
                    int parts = qCeil((stops.at(i + 1).first - stops.at(i).first) / spacing);
                    newStops.append(stops.at(i));
                    toColor = qPremultiply(stops.at(i + 1).second.rgba());

                    if (parts > 1) {
                        qreal step = (stops.at(i + 1).first - stops.at(i).first) / parts;
                        for (int j = 1; j < parts; ++j) {
                            QRgb color = qUnpremultiply(INTERPOLATE_PIXEL_256(fromColor, 256 - 256 * j / parts, toColor, 256 * j / parts));
                            newStops.append(QGradientStop(stops.at(i).first + j * step, QColor::fromRgba(color)));
                        }
                    }
                    fromColor = toColor;
                }
                newStops.append(stops.back());
                stops = newStops;
            }
        }

        for (const QGradientStop &stop : qAsConst(stops)) {
            const QString color = stop.second.name(QColor::HexRgb);
            str << QLatin1String("    <stop offset=\"")<< stop.first << QLatin1String("\" ")
                << QLatin1String("stop-color=\"") << color << QLatin1String("\" ")
                << QLatin1String("stop-opacity=\"") << stop.second.alphaF() <<QLatin1String("\" />\n");
        }
    }

    void saveGradientUnits(QTextStream &str, const QGradient *gradient)
    {
        str << QLatin1String("gradientUnits=\"");
        if (gradient && (gradient->coordinateMode() == QGradient::ObjectBoundingMode || gradient->coordinateMode() == QGradient::ObjectMode))
            str << QLatin1String("objectBoundingBox");
        else
            str << QLatin1String("userSpaceOnUse");
        str << QLatin1String("\" ");
    }

    QString getClipPathId(const QPaintEngineState &state)
    {
        QString id;
        bool bClipEnabled = state.isClipEnabled() && state.painter()->hasClipping();
        if (bClipEnabled && state.clipOperation() == Qt::NoClip)
            bClipEnabled = false;
        if (!bClipEnabled)
            return id;

        bool bUnsavedClipPath = d_func()->getClipPathId(id);
        if (bUnsavedClipPath)
            saveClipPath(id);
        return id;
    }

    void saveClipPath(QString &id)
    {
        QTextStream str(&d_func()->defs, QIODevice::Append);
        str << QLatin1String("<clipPath ");
        str << QLatin1String("id=\"") << id << QLatin1String("\">\n");
        const QPainterPath &p = d_func()->clipPath;
        str << "<path d=\"";

        for (int i=0; i<p.elementCount(); ++i) {
            const QPainterPath::Element &e = p.elementAt(i);
            switch (e.type) {
            case QPainterPath::MoveToElement:
                str << 'M' << e.x << ',' << e.y;
                break;
            case QPainterPath::LineToElement:
                str << 'L' << e.x << ',' << e.y;
                break;
            case QPainterPath::CurveToElement:
                str << 'C' << e.x << ',' << e.y;
                ++i;
                while (i < p.elementCount()) {
                    const QPainterPath::Element &e = p.elementAt(i);
                    if (e.type != QPainterPath::CurveToDataElement) {
                        --i;
                        break;
                    } else
                        str << ' ';
                    str << e.x << ',' << e.y;
                    ++i;
                }
                break;
            default:
                break;
            }
            if (i != p.elementCount() - 1) {
                str << ' ';
            }
        }

        str << "\"/>" << endl;
        str << QLatin1String("</clipPath>") << endl;
    }

    void generateQtDefaults()
    {
        *d_func()->stream << QLatin1String("fill=\"none\" ");
        *d_func()->stream << QLatin1String("stroke=\"black\" ");
        *d_func()->stream << QLatin1String("stroke-width=\"1\" ");
        *d_func()->stream << QLatin1String("fill-rule=\"evenodd\" ");
        *d_func()->stream << QLatin1String("stroke-linecap=\"square\" ");
        *d_func()->stream << QLatin1String("stroke-linejoin=\"bevel\" ");
        *d_func()->stream << QLatin1String(">\n");
    }

    inline QTextStream &stream()
    {
        if (trimEmptyGroupEnabled() && d_func()->discardStashStream)
            return stashStream();

        return *d_func()->stream;
    }

    inline QTextStream &stashStream()
    {
        return *d_func()->stashStream.get();
    }

    inline void holdStashStream()
    {
        if (!trimEmptyGroupEnabled())
            return;

        *d_func()->stream << stashStream().readAll();
        d_func()->discardStashStream = false;
    }

    void qpenToSvg(const QPen &spen)
    {
        d_func()->pen = spen;

        switch (spen.style()) {
        case Qt::NoPen:
            stream() << QLatin1String("stroke=\"none\" ");

            d_func()->attributes.stroke = QLatin1String("none");
            d_func()->attributes.strokeOpacity = QString();
            return;
            break;
        case Qt::SolidLine: {
            QString color, colorOpacity;

            translate_color(spen.color(), &color,
                            &colorOpacity);
            d_func()->attributes.stroke = color;
            d_func()->attributes.strokeOpacity = colorOpacity;

            stream() << QLatin1String("stroke=\"")<<color<< QLatin1String("\" ");
            stream() << QLatin1String("stroke-opacity=\"")<<colorOpacity<< QLatin1String("\" ");
        }
            break;
        case Qt::DashLine:
        case Qt::DotLine:
        case Qt::DashDotLine:
        case Qt::DashDotDotLine:
        case Qt::CustomDashLine: {
            QString color, colorOpacity, dashPattern, dashOffset;

            qreal penWidth = spen.width() == 0 ? qreal(1) : spen.widthF();

            translate_color(spen.color(), &color, &colorOpacity);
            translate_dashPattern(spen.dashPattern(), penWidth, &dashPattern);

            // SVG uses absolute offset
            dashOffset = QString::number(spen.dashOffset() * penWidth);

            d_func()->attributes.stroke = color;
            d_func()->attributes.strokeOpacity = colorOpacity;
            d_func()->attributes.dashPattern = dashPattern;
            d_func()->attributes.dashOffset = dashOffset;

            stream() << QLatin1String("stroke=\"")<<color<< QLatin1String("\" ");
            stream() << QLatin1String("stroke-opacity=\"")<<colorOpacity<< QLatin1String("\" ");
            stream() << QLatin1String("stroke-dasharray=\"")<<dashPattern<< QLatin1String("\" ");
            stream() << QLatin1String("stroke-dashoffset=\"")<<dashOffset<< QLatin1String("\" ");
            break;
        }
        default:
            qWarning("Unsupported pen style");
            break;
        }

        if (spen.widthF() == 0)
            stream() <<"stroke-width=\"1\" ";
        else
            stream() <<"stroke-width=\"" << spen.widthF() << "\" ";

        switch (spen.capStyle()) {
        case Qt::FlatCap:
            stream() << "stroke-linecap=\"butt\" ";
            break;
        case Qt::SquareCap:
            stream() << "stroke-linecap=\"square\" ";
            break;
        case Qt::RoundCap:
            stream() << "stroke-linecap=\"round\" ";
            break;
        default:
            qWarning("Unhandled cap style");
        }
        switch (spen.joinStyle()) {
        case Qt::SvgMiterJoin:
        case Qt::MiterJoin:
            stream() << "stroke-linejoin=\"miter\" "
                        "stroke-miterlimit=\""<<spen.miterLimit()<<"\" ";
            break;
        case Qt::BevelJoin:
            stream() << "stroke-linejoin=\"bevel\" ";
            break;
        case Qt::RoundJoin:
            stream() << "stroke-linejoin=\"round\" ";
            break;
        default:
            qWarning("Unhandled join style");
        }
    }
    void qbrushToSvg(const QBrush &sbrush)
    {
        d_func()->brush = sbrush;
        switch (sbrush.style()) {
        case Qt::SolidPattern: {
            QString color, colorOpacity;
            translate_color(sbrush.color(), &color, &colorOpacity);
            stream() << "fill=\"" << color << "\" "
                        "fill-opacity=\""
                     << colorOpacity << "\" ";
            d_func()->attributes.fill = color;
            d_func()->attributes.fillOpacity = colorOpacity;
        }
            break;
        case Qt::Dense1Pattern:
        case Qt::Dense2Pattern:
        case Qt::Dense3Pattern:
        case Qt::Dense4Pattern:
        case Qt::Dense5Pattern:
        case Qt::Dense6Pattern:
        case Qt::Dense7Pattern:
        case Qt::HorPattern:
        case Qt::VerPattern:
        case Qt::CrossPattern:
        case Qt::BDiagPattern:
        case Qt::FDiagPattern:
        case Qt::DiagCrossPattern: {
            QString color, colorOpacity;
            translate_color(sbrush.color(), &color, &colorOpacity);
            QString patternId = savePatternBrush(color, sbrush);
            QString patternRef = QString(QStringLiteral("url(#%1)")).arg(patternId);
            stream() << "fill=\"" << patternRef << "\" fill-opacity=\"" << colorOpacity << "\" ";
            d_func()->attributes.fill = patternRef;
            d_func()->attributes.fillOpacity = colorOpacity;
            break;
        }
        case Qt::LinearGradientPattern:
            saveLinearGradientBrush(sbrush.gradient());
            d_func()->attributes.fill = QString::fromLatin1("url(#%1)").arg(d_func()->currentGradientName);
            d_func()->attributes.fillOpacity = QString();
            stream() << QLatin1String("fill=\"url(#") << d_func()->currentGradientName << QLatin1String(")\" ");
            break;
        case Qt::RadialGradientPattern:
            saveRadialGradientBrush(sbrush.gradient());
            d_func()->attributes.fill = QString::fromLatin1("url(#%1)").arg(d_func()->currentGradientName);
            d_func()->attributes.fillOpacity = QString();
            stream() << QLatin1String("fill=\"url(#") << d_func()->currentGradientName << QLatin1String(")\" ");
            break;
        case Qt::ConicalGradientPattern:
            saveConicalGradientBrush(sbrush.gradient());
            d_func()->attributes.fill = QString::fromLatin1("url(#%1)").arg(d_func()->currentGradientName);
            d_func()->attributes.fillOpacity = QString();
            stream() << QLatin1String("fill=\"url(#") << d_func()->currentGradientName << QLatin1String(")\" ");
            break;
        case Qt::NoBrush:
            stream() << QLatin1String("fill=\"none\" ");
            d_func()->attributes.fill = QLatin1String("none");
            d_func()->attributes.fillOpacity = QString();
            return;
            break;
        default:
           break;
        }
    }
    QString qFontNameTranslate(QString fontName)
    {
        static QHash<QString, QString> s_FontFamilyMap = {
            std::make_pair(QString::fromWCharArray(L"宋体"), QLatin1String("SimSun")),
            std::make_pair(QString::fromWCharArray(L"黑体"), QLatin1String("SimHei")),
            std::make_pair(QString::fromWCharArray(L"微软雅黑"), QLatin1String("Microsoft Yahei")),
            std::make_pair(QString::fromWCharArray(L"微软正黑体"), QLatin1String("Microsoft JhengHei")),
            std::make_pair(QString::fromWCharArray(L"楷体"), QLatin1String("KaiTi")),
            std::make_pair(QString::fromWCharArray(L"新宋体"), QLatin1String("NSimSun")),
            std::make_pair(QString::fromWCharArray(L"仿宋"), QLatin1String("FangSong")),
            std::make_pair(QString::fromWCharArray(L"苹方"), QLatin1String("PingFang SC")),
            std::make_pair(QString::fromWCharArray(L"华文黑体"), QLatin1String("STHeiti")),
            std::make_pair(QString::fromWCharArray(L"华文楷体"), QLatin1String("STKaiti")),
            std::make_pair(QString::fromWCharArray(L"华文宋体"), QLatin1String("STSong")),
            std::make_pair(QString::fromWCharArray(L"华文仿宋"), QLatin1String("STFangsong")),
            std::make_pair(QString::fromWCharArray(L"华文中宋"), QLatin1String("STZhongsong")),
            std::make_pair(QString::fromWCharArray(L"华文琥珀"), QLatin1String("STHupo")),
            std::make_pair(QString::fromWCharArray(L"华文新魏"), QLatin1String("STXinwei")),
            std::make_pair(QString::fromWCharArray(L"华文隶书"), QLatin1String("STLiti")),
            std::make_pair(QString::fromWCharArray(L"华文行楷"), QLatin1String("STXingkai")),
            std::make_pair(QString::fromWCharArray(L"冬青黑体简"), QLatin1String("Hiragino Sans GB")),
            std::make_pair(QString::fromWCharArray(L"兰亭黑-简"), QLatin1String("Lantinghei SC")),
            std::make_pair(QString::fromWCharArray(L"翩翩体-简"), QLatin1String("Hanzipen SC")),
            std::make_pair(QString::fromWCharArray(L"手札体-简"), QLatin1String("Hannotate SC")),
            std::make_pair(QString::fromWCharArray(L"宋体-简"), QLatin1String("Songti SC")),
            std::make_pair(QString::fromWCharArray(L"娃娃体-简"), QLatin1String("Wawati SC")),
            std::make_pair(QString::fromWCharArray(L"魏碑-简"), QLatin1String("Weibei SC")),
            std::make_pair(QString::fromWCharArray(L"行楷-简"), QLatin1String("Xingkai SC")),
            std::make_pair(QString::fromWCharArray(L"雅痞-简"), QLatin1String("Yapi SC")),
            std::make_pair(QString::fromWCharArray(L"圆体-简"), QLatin1String("Yuanti SC")),
            std::make_pair(QString::fromWCharArray(L"幼圆"), QLatin1String("YouYuan")),
            std::make_pair(QString::fromWCharArray(L"隶书"), QLatin1String("LiSu")),
            std::make_pair(QString::fromWCharArray(L"华文细黑"), QLatin1String("STXihei")),
            std::make_pair(QString::fromWCharArray(L"华文彩云"), QLatin1String("STCaiyun")),
            std::make_pair(QString::fromWCharArray(L"方正舒体"), QLatin1String("FZShuTi")),
            std::make_pair(QString::fromWCharArray(L"方正姚体"), QLatin1String("FZYaoti")),
            std::make_pair(QString::fromWCharArray(L"思源黑体"), QLatin1String("Source Han Sans CN")),
            std::make_pair(QString::fromWCharArray(L"思源宋体"), QLatin1String("Source Han Serif SC")),
            std::make_pair(QString::fromWCharArray(L"文泉驿微米黑"), QLatin1String("WenQuanYi Micro Hei")),
            std::make_pair(QString::fromWCharArray(L"汉仪旗黑"), QLatin1String("HYQihei 40S")),
            std::make_pair(QString::fromWCharArray(L"汉仪大宋简"), QLatin1String("HYDaSongJ")),
            std::make_pair(QString::fromWCharArray(L"汉仪楷体"), QLatin1String("HYKaiti")),
            std::make_pair(QString::fromWCharArray(L"汉仪家书简"), QLatin1String("HYJiaShuJ")),
            std::make_pair(QString::fromWCharArray(L"汉仪PP体简"), QLatin1String("HYPPTiJ")),
            std::make_pair(QString::fromWCharArray(L"汉仪乐喵体简"), QLatin1String("HYLeMiaoTi")),
            std::make_pair(QString::fromWCharArray(L"汉仪小麦体"), QLatin1String("HYXiaoMaiTiJ")),
            std::make_pair(QString::fromWCharArray(L"汉仪程行体"), QLatin1String("HYChengXingJ")),
            std::make_pair(QString::fromWCharArray(L"汉仪黑荔枝"), QLatin1String("HYHeiLiZhiTiJ")),
            std::make_pair(QString::fromWCharArray(L"汉仪雅酷黑W"), QLatin1String("HYYaKuHeiW")),
            std::make_pair(QString::fromWCharArray(L"汉仪大黑简"), QLatin1String("HYDaHeiJ")),
            std::make_pair(QString::fromWCharArray(L"汉仪尚魏手书W"), QLatin1String("HYShangWeiShouShuW")),
            std::make_pair(QString::fromWCharArray(L"方正粗雅宋简体"), QLatin1String("FZYaSongS-B-GB")),
            std::make_pair(QString::fromWCharArray(L"方正报宋简体"), QLatin1String("FZBaoSong-Z04S")),
            std::make_pair(QString::fromWCharArray(L"方正粗圆简体"), QLatin1String("FZCuYuan-M03S")),
            std::make_pair(QString::fromWCharArray(L"方正大标宋简体"), QLatin1String("FZDaBiaoSong-B06S")),
            std::make_pair(QString::fromWCharArray(L"方正大黑简体"), QLatin1String("FZDaHei-B02S")),
            std::make_pair(QString::fromWCharArray(L"方正仿宋简体"), QLatin1String("FZFangSong-Z02S")),
            std::make_pair(QString::fromWCharArray(L"方正黑体简体"), QLatin1String("FZHei-B01S")),
            std::make_pair(QString::fromWCharArray(L"方正琥珀简体"), QLatin1String("FZHuPo-M04S")),
            std::make_pair(QString::fromWCharArray(L"方正楷体简体"), QLatin1String("FZKai-Z03S")),
            std::make_pair(QString::fromWCharArray(L"方正隶变简体"), QLatin1String("FZLiBian-S02S")),
            std::make_pair(QString::fromWCharArray(L"方正隶书简体"), QLatin1String("FZLiShu-S01S")),
            std::make_pair(QString::fromWCharArray(L"方正美黑简体"), QLatin1String("FZMeiHei-M07S")),
            std::make_pair(QString::fromWCharArray(L"方正书宋简体"), QLatin1String("FZShuSong-Z01S")),
            std::make_pair(QString::fromWCharArray(L"方正舒体简体"), QLatin1String("FZShuTi-S05S")),
            std::make_pair(QString::fromWCharArray(L"方正水柱简体"), QLatin1String("FZShuiZhu-M08S")),
            std::make_pair(QString::fromWCharArray(L"方正宋黑简体"), QLatin1String("FZSongHei-B07S")),
            std::make_pair(QString::fromWCharArray(L"方正宋三简体"), QLatin1String("FZSong")),
            std::make_pair(QString::fromWCharArray(L"方正魏碑简体"), QLatin1String("FZWeiBei-S03S")),
            std::make_pair(QString::fromWCharArray(L"方正细等线简体"), QLatin1String("FZXiDengXian-Z06S")),
            std::make_pair(QString::fromWCharArray(L"方正细黑一简体"), QLatin1String("FZXiHei I-Z08S")),
            std::make_pair(QString::fromWCharArray(L"方正细圆简体"), QLatin1String("FZXiYuan-M01S")),
            std::make_pair(QString::fromWCharArray(L"方正小标宋简体"), QLatin1String("FZXiaoBiaoSong-B05S")),
            std::make_pair(QString::fromWCharArray(L"方正行楷简体"), QLatin1String("FZXingKai-S04S")),
            std::make_pair(QString::fromWCharArray(L"方正姚体简体"), QLatin1String("FZYaoTi-M06S")),
            std::make_pair(QString::fromWCharArray(L"方正中等线简体"), QLatin1String("FZZhongDengXian-Z07S")),
            std::make_pair(QString::fromWCharArray(L"方正准圆简体"), QLatin1String("FZZhunYuan-M02S")),
            std::make_pair(QString::fromWCharArray(L"方正综艺简体"), QLatin1String("FZZongYi-M05S")),
            std::make_pair(QString::fromWCharArray(L"方正彩云简体"), QLatin1String("FZCaiYun-M09S")),
            std::make_pair(QString::fromWCharArray(L"方正隶二简体"), QLatin1String("FZLiShu II-S06S")),
            std::make_pair(QString::fromWCharArray(L"方正康体简体"), QLatin1String("FZKangTi-S07S")),
            std::make_pair(QString::fromWCharArray(L"方正超粗黑简体"), QLatin1String("FZChaoCuHei-M10S")),
            std::make_pair(QString::fromWCharArray(L"方正新报宋简体"), QLatin1String("FZNew BaoSong-Z12S")),
            std::make_pair(QString::fromWCharArray(L"方正新舒体简体"), QLatin1String("FZNew ShuTi-S08S")),
            std::make_pair(QString::fromWCharArray(L"方正黄草简体"), QLatin1String("FZHuangCao-S09S")),
            std::make_pair(QString::fromWCharArray(L"方正少儿简体"), QLatin1String("FZShaoEr-M11S")),
            std::make_pair(QString::fromWCharArray(L"方正稚艺简体"), QLatin1String("FZZhiYi-M12S")),
            std::make_pair(QString::fromWCharArray(L"方正细珊瑚简体"), QLatin1String("FZXiShanHu-M13S")),
            std::make_pair(QString::fromWCharArray(L"方正粗宋简体"), QLatin1String("FZCuSong-B09S")),
            std::make_pair(QString::fromWCharArray(L"方正平和简体"), QLatin1String("FZPingHe-S11S")),
            std::make_pair(QString::fromWCharArray(L"方正华隶简体"), QLatin1String("FZHuaLi-M14S")),
            std::make_pair(QString::fromWCharArray(L"方正瘦金书简体"), QLatin1String("FZShouJinShu-S10S")),
            std::make_pair(QString::fromWCharArray(L"方正细倩简体"), QLatin1String("FZXiQian-M15S")),
            std::make_pair(QString::fromWCharArray(L"方正中倩简体"), QLatin1String("FZZhongQian-M16S")),
            std::make_pair(QString::fromWCharArray(L"方正粗倩简体"), QLatin1String("FZCuQian-M17S")),
            std::make_pair(QString::fromWCharArray(L"方正胖娃简体"), QLatin1String("FZPangWa-M18S")),
            std::make_pair(QString::fromWCharArray(L"方正宋一简体"), QLatin1String("FZSongYi-Z13S")),
            std::make_pair(QString::fromWCharArray(L"方正剪纸简体"), QLatin1String("FZJianZhi-M23S")),
            std::make_pair(QString::fromWCharArray(L"方正流行体简体"), QLatin1String("FZLiuXingTi-M26S")),
            std::make_pair(QString::fromWCharArray(L"方正祥隶简体"), QLatin1String("FZXiangLi-S17S")),
            std::make_pair(QString::fromWCharArray(L"方正粗活意简体"), QLatin1String("FZCuHuoYi-M25S")),
            std::make_pair(QString::fromWCharArray(L"方正胖头鱼简体"), QLatin1String("FZPangTouYu-M24S")),
            std::make_pair(QString::fromWCharArray(L"方正卡通简体"), QLatin1String("FZKaTong-M19S")),
            std::make_pair(QString::fromWCharArray(L"方正艺黑简体"), QLatin1String("FZYiHei-M20S")),
            std::make_pair(QString::fromWCharArray(L"方正水黑简体"), QLatin1String("FZShuiHei-M21S")),
            std::make_pair(QString::fromWCharArray(L"方正古隶简体"), QLatin1String("FZGuLi-S12S")),
            std::make_pair(QString::fromWCharArray(L"方正幼线简体"), QLatin1String("FZYouXian-Z09S")),
            std::make_pair(QString::fromWCharArray(L"方正启体简体"), QLatin1String("FZQiTi-S14S")),
            std::make_pair(QString::fromWCharArray(L"方正小篆体"), QLatin1String("FZXiaoZhuanTi-S13T")),
            std::make_pair(QString::fromWCharArray(L"方正硬笔楷书简体"), QLatin1String("FZYingBiKaiShu-S15S")),
            std::make_pair(QString::fromWCharArray(L"方正毡笔黑简体"), QLatin1String("FZZhanBiHei-M22S")),
            std::make_pair(QString::fromWCharArray(L"方正硬笔行书简体"), QLatin1String("FZYingBiXingShu-S16S")),
        };

        if (s_FontFamilyMap.contains(fontName))
            return s_FontFamilyMap[fontName];

        return QString();
    }
    void qfontToSvg(const QFont &sfont)
    {
        Q_D(QSvgPaintEngine);

        d->font = sfont;

        if (d->font.pixelSize() == -1)
            d->attributes.font_size = QString::number(d->font.pointSizeF() * d->resolution / 72);
        else
            d->attributes.font_size = QString::number(d->font.pixelSize());

        int svgWeight = d->font.weight();
        switch (svgWeight) {
        case QFont::Light:
            svgWeight = 100;
            break;
        case QFont::Normal:
            svgWeight = 400;
            break;
        case QFont::Bold:
            svgWeight = 700;
            break;
        default:
            svgWeight *= 10;
        }

        d->attributes.font_weight = QString::number(svgWeight);
        d->attributes.font_style = d->font.italic() ? QLatin1String("italic") : QLatin1String("normal");

        QString fontName = d->font.family();
        QString translatedFontName = qFontNameTranslate(fontName);
        if (!translatedFontName.isEmpty())
            fontName += "," + translatedFontName;
        d->attributes.font_family = fontName.toHtmlEscaped();

        stream() << "font-family=\"" << d->attributes.font_family << "\" "
                      "font-size=\"" << d->attributes.font_size << "\" "
                      "font-weight=\"" << d->attributes.font_weight << "\" "
                      "font-style=\"" << d->attributes.font_style << "\" "
                   << endl;
    }
    void appendTransformToSvg(qreal x, qreal y, qreal w, qreal h, qreal angle)
    {
        qreal midx = qRound(x + w / 2);
        qreal midy = qRound(y - h / 2);
        stream() << QString(QStringLiteral("transform=\"translate(%1, %2) rotate(%3) translate(-%1, -%2)\"")).arg(midx).arg(midy).arg(angle) << endl;
    }
};

class QSvgGeneratorPrivate
{
public:
    QSvgPaintEngine *engine;

    uint owns_iodevice : 1;
    QString fileName;
};

/*!
    \class QSvgGenerator
    \ingroup painting
    \inmodule QtSvg
    \since 4.3
    \brief The QSvgGenerator class provides a paint device that is used to create SVG drawings.
    \reentrant

    This paint device represents a Scalable Vector Graphics (SVG) drawing. Like QPrinter, it is
    designed as a write-only device that generates output in a specific format.

    To write an SVG file, you first need to configure the output by setting the \l fileName
    or \l outputDevice properties. It is usually necessary to specify the size of the drawing
    by setting the \l size property, and in some cases where the drawing will be included in
    another, the \l viewBox property also needs to be set.

    \snippet svggenerator/window.cpp configure SVG generator

    Other meta-data can be specified by setting the \a title, \a description and \a resolution
    properties.

    As with other QPaintDevice subclasses, a QPainter object is used to paint onto an instance
    of this class:

    \snippet svggenerator/window.cpp begin painting
    \dots
    \snippet svggenerator/window.cpp end painting

    Painting is performed in the same way as for any other paint device. However,
    it is necessary to use the QPainter::begin() and \l{QPainter::}{end()} to
    explicitly begin and end painting on the device.

    The \l{SVG Generator Example} shows how the same painting commands can be used
    for painting a widget and writing an SVG file.

    \sa QSvgRenderer, QSvgWidget, {Qt SVG C++ Classes}
*/

/*!
    Constructs a new generator.
*/
QSvgGenerator::QSvgGenerator()
    : d_ptr(new QSvgGeneratorPrivate)
{
    Q_D(QSvgGenerator);

    d->engine = new QSvgPaintEngine;
    d->owns_iodevice = false;
}

/*!
    Destroys the generator.
*/
QSvgGenerator::~QSvgGenerator()
{
    Q_D(QSvgGenerator);
    if (d->owns_iodevice)
        delete d->engine->outputDevice();
    delete d->engine;
}

/*!
    \property QSvgGenerator::title
    \brief the title of the generated SVG drawing
    \since 4.5
    \sa description
*/
QString QSvgGenerator::title() const
{
    Q_D(const QSvgGenerator);

    return d->engine->documentTitle();
}

void QSvgGenerator::setTitle(const QString &title)
{
    Q_D(QSvgGenerator);

    d->engine->setDocumentTitle(title);
}

/*!
    \property QSvgGenerator::description
    \brief the description of the generated SVG drawing
    \since 4.5
    \sa title
*/
QString QSvgGenerator::description() const
{
    Q_D(const QSvgGenerator);

    return d->engine->documentDescription();
}

void QSvgGenerator::setDescription(const QString &description)
{
    Q_D(QSvgGenerator);

    d->engine->setDocumentDescription(description);
}

/*!
    \property QSvgGenerator::size
    \brief the size of the generated SVG drawing
    \since 4.5

    By default this property is set to \c{QSize(-1, -1)}, which
    indicates that the generator should not output the width and
    height attributes of the \c<svg> element.

    \note It is not possible to change this property while a
    QPainter is active on the generator.

    \sa viewBox, resolution
*/
QSize QSvgGenerator::size() const
{
    Q_D(const QSvgGenerator);
    return d->engine->size();
}

void QSvgGenerator::setSize(const QSize &size)
{
    Q_D(QSvgGenerator);
    if (d->engine->isActive()) {
        qWarning("QSvgGenerator::setSize(), cannot set size while SVG is being generated");
        return;
    }
    d->engine->setSize(size);
}

/*!
    \property QSvgGenerator::viewBox
    \brief the viewBox of the generated SVG drawing
    \since 4.5

    By default this property is set to \c{QRect(0, 0, -1, -1)}, which
    indicates that the generator should not output the viewBox attribute
    of the \c<svg> element.

    \note It is not possible to change this property while a
    QPainter is active on the generator.

    \sa viewBox(), size, resolution
*/
QRectF QSvgGenerator::viewBoxF() const
{
    Q_D(const QSvgGenerator);
    return d->engine->viewBox();
}

/*!
    \since 4.5

    Returns viewBoxF().toRect().

    \sa viewBoxF()
*/
QRect QSvgGenerator::viewBox() const
{
    Q_D(const QSvgGenerator);
    return d->engine->viewBox().toRect();
}

void QSvgGenerator::setViewBox(const QRectF &viewBox)
{
    Q_D(QSvgGenerator);
    if (d->engine->isActive()) {
        qWarning("QSvgGenerator::setViewBox(), cannot set viewBox while SVG is being generated");
        return;
    }
    d->engine->setViewBox(viewBox);
}

void QSvgGenerator::setViewBox(const QRect &viewBox)
{
    setViewBox(QRectF(viewBox));
}

/*!
    \property QSvgGenerator::fileName
    \brief the target filename for the generated SVG drawing
    \since 4.5

    \sa outputDevice
*/
QString QSvgGenerator::fileName() const
{
    Q_D(const QSvgGenerator);
    return d->fileName;
}

void QSvgGenerator::setFileName(const QString &fileName)
{
    Q_D(QSvgGenerator);
    if (d->engine->isActive()) {
        qWarning("QSvgGenerator::setFileName(), cannot set file name while SVG is being generated");
        return;
    }

    if (d->owns_iodevice)
        delete d->engine->outputDevice();

    d->owns_iodevice = true;

    d->fileName = fileName;
    QFile *file = new QFile(fileName);
    d->engine->setOutputDevice(file);
}

/*!
    \property QSvgGenerator::outputDevice
    \brief the output device for the generated SVG drawing
    \since 4.5

    If both output device and file name are specified, the output device
    will have precedence.

    \sa fileName
*/
QIODevice *QSvgGenerator::outputDevice() const
{
    Q_D(const QSvgGenerator);
    return d->engine->outputDevice();
}

void QSvgGenerator::setOutputDevice(QIODevice *outputDevice)
{
    Q_D(QSvgGenerator);
    if (d->engine->isActive()) {
        qWarning("QSvgGenerator::setOutputDevice(), cannot set output device while SVG is being generated");
        return;
    }
    d->owns_iodevice = false;
    d->engine->setOutputDevice(outputDevice);
    d->fileName = QString();
}

/*!
    \property QSvgGenerator::resolution
    \brief the resolution of the generated output
    \since 4.5

    The resolution is specified in dots per inch, and is used to
    calculate the physical size of an SVG drawing.

    \sa size, viewBox
*/
int QSvgGenerator::resolution() const
{
    Q_D(const QSvgGenerator);
    return d->engine->resolution();
}

void QSvgGenerator::setResolution(int dpi)
{
    Q_D(QSvgGenerator);
    d->engine->setResolution(dpi);
}

bool QSvgGenerator::clipPathEnabled() const
{
    Q_D(const QSvgGenerator);
    return d->engine->clipPathEnabled();
}

void QSvgGenerator::setClipPathEnabled(bool enable)
{
    Q_D(QSvgGenerator);
    d->engine->setClipPathEnabled(enable);
}

bool QSvgGenerator::textCoordEnabled() const
{
    Q_D(const QSvgGenerator);
    return d->engine->textCoordEnabled();
}

void QSvgGenerator::setTextCoordEnabled(bool enable)
{
    Q_D(QSvgGenerator);
    d->engine->setTextCoordEnabled(enable);
}

bool QSvgGenerator::trimEmptyGroupEnabled() const
{
    Q_D(const QSvgGenerator);
    return d->engine->trimEmptyGroupEnabled();
}

void QSvgGenerator::setTrimEmptyGroupEnabled(bool enable)
{
    Q_D(QSvgGenerator);
    d->engine->setTrimEmptyGroupEnabled(enable);
}

QString QSvgGenerator::dataPrefix() const
{
    Q_D(const QSvgGenerator);
    return d->engine->dataPrefix();
}

void QSvgGenerator::setDataPrefix(const QString& prefix)
{
    Q_D(QSvgGenerator);
    d->engine->setDataPrefix(prefix);
}

void QSvgGenerator::beginGroup(const QMap<QString, QString> *attrs)
{
    Q_D(QSvgGenerator);
    d->engine->beginGroup(attrs);
}

void QSvgGenerator::endGroup()
{
    Q_D(QSvgGenerator);
    d->engine->endGroup();

}

/*!
    Returns the paint engine used to render graphics to be converted to SVG
    format information.
*/
QPaintEngine *QSvgGenerator::paintEngine() const
{
    Q_D(const QSvgGenerator);
    return d->engine;
}

/*!
    \reimp
*/
int QSvgGenerator::metric(QPaintDevice::PaintDeviceMetric metric) const
{
    Q_D(const QSvgGenerator);
    switch (metric) {
    case QPaintDevice::PdmDepth:
        return 32;
    case QPaintDevice::PdmWidth:
        return d->engine->size().width();
    case QPaintDevice::PdmHeight:
        return d->engine->size().height();
    case QPaintDevice::PdmDpiX:
        return d->engine->resolution();
    case QPaintDevice::PdmDpiY:
        return d->engine->resolution();
    case QPaintDevice::PdmHeightMM:
        return qRound(d->engine->size().height() * 25.4 / d->engine->resolution());
    case QPaintDevice::PdmWidthMM:
        return qRound(d->engine->size().width() * 25.4 / d->engine->resolution());
    case QPaintDevice::PdmNumColors:
        return 0xffffffff;
    case QPaintDevice::PdmPhysicalDpiX:
        return d->engine->resolution();
    case QPaintDevice::PdmPhysicalDpiY:
        return d->engine->resolution();
    case QPaintDevice::PdmDevicePixelRatio:
        return 1;
    case QPaintDevice::PdmDevicePixelRatioScaled:
        return 1 * QPaintDevice::devicePixelRatioFScale();
    default:
        qWarning("QSvgGenerator::metric(), unhandled metric %d\n", metric);
        break;
    }
    return 0;
}

/*****************************************************************************
 * class QSvgPaintEngine
 */

bool QSvgPaintEngine::begin(QPaintDevice *)
{
    Q_D(QSvgPaintEngine);
    if (!d->outputDevice) {
        qWarning("QSvgPaintEngine::begin(), no output device");
        return false;
    }

    if (!d->outputDevice->isOpen()) {
        if (!d->outputDevice->open(QIODevice::WriteOnly | QIODevice::Text)) {
            qWarning("QSvgPaintEngine::begin(), could not open output device: '%s'",
                     qPrintable(d->outputDevice->errorString()));
            return false;
        }
    } else if (!d->outputDevice->isWritable()) {
        qWarning("QSvgPaintEngine::begin(), could not write to read-only output device: '%s'",
                 qPrintable(d->outputDevice->errorString()));
        return false;
    }

    d->stream = new QTextStream(&d->header);
    if (trimEmptyGroupEnabled())
        d->stashStream.reset(new QTextStream(&d->stashContent));

    // stream out the header...
    *d->stream << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"no\"?>" << endl << "<svg";

    if (d->size.isValid()) {
        qreal wmm = d->size.width() * 25.4 / d->resolution;
        qreal hmm = d->size.height() * 25.4 / d->resolution;
        *d->stream << " width=\"" << wmm << "mm\" height=\"" << hmm << "mm\"" << endl;
    }

    if (d->viewBox.isValid()) {
        *d->stream << " viewBox=\"" << d->viewBox.left() << ' ' << d->viewBox.top();
        *d->stream << ' ' << d->viewBox.width() << ' ' << d->viewBox.height() << '\"' << endl;
    }

    *d->stream << " xmlns=\"http://www.w3.org/2000/svg\""
                  " xmlns:xlink=\"http://www.w3.org/1999/xlink\" "
                  " version=\"1.2\" baseProfile=\"tiny\">" << endl;

    if (!d->attributes.document_title.isEmpty()) {
        *d->stream << "<title>" << d->attributes.document_title << "</title>" << endl;
    }

    if (!d->attributes.document_description.isEmpty()) {
        *d->stream << "<desc>" << d->attributes.document_description << "</desc>" << endl;
    }

    d->stream->setString(&d->defs);
    *d->stream << "<defs>\n";

    d->stream->setString(&d->body);
    // Start the initial graphics state...
    *d->stream << "<g ";
    generateQtDefaults();
    *d->stream << endl;

    return true;
}

bool QSvgPaintEngine::end()
{
    Q_D(QSvgPaintEngine);

    d->stream->setString(&d->defs);
    *d->stream << "</defs>\n";

    d->stream->setDevice(d->outputDevice);
#ifndef QT_NO_TEXTCODEC
    d->stream->setCodec(QTextCodec::codecForName("UTF-8"));
#endif

    *d->stream << d->header;
    *d->stream << d->defs;
    *d->stream << d->body;
    if (d->afterFirstUpdate && !d->discardStashStream)
        *d->stream << "</g>" << endl; // close the updateState

    *d->stream << "</g>" << endl // close the Qt defaults
               << "</svg>" << endl;

    delete d->stream;

    return true;
}

void QSvgPaintEngine::drawPixmap(const QRectF &r, const QPixmap &pm,
                                 const QRectF &sr)
{
    drawImage(r, pm.toImage(), sr);
}

void QSvgPaintEngine::drawImage(const QRectF &r, const QImage &image,
                                const QRectF &sr,
                                Qt::ImageConversionFlags flags)
{
    //Q_D(QSvgPaintEngine);

    Q_UNUSED(flags);

    holdStashStream();

    QRectF baseSize(0, 0, image.width(), image.height());
    QImage im = image;
    if (baseSize != sr)
        im = im.copy(sr.toAlignedRect());

    if (im.isNull())
        return;

    QString shapeInfo = image.text("shapeInfo");
    if (!shapeInfo.isEmpty())
        stream() << "<g id=\"wo_shape\" data-description=\"shapeInfo_"<<shapeInfo<<"\">";

    stream() << "<image ";
    stream() << "x=\""<<r.x()<<"\" "
                "y=\""<<r.y()<<"\" "
                "width=\""<<r.width()<<"\" "
                "height=\""<<r.height()<<"\" "
                "preserveAspectRatio=\"none\" ";

    QByteArray data;
    QBuffer buffer(&data);
    buffer.open(QBuffer::ReadWrite);
    im.save(&buffer, "PNG");
    buffer.close();
    stream() << "xlink:href=\"data:image/png;base64,"
             << data.toBase64()
             <<"\" />\n";
    
    if (!shapeInfo.isEmpty())
        stream() << "</g>";
}

void QSvgPaintEngine::updateState(const QPaintEngineState &state)
{
    Q_D(QSvgPaintEngine);
    QPaintEngine::DirtyFlags flags = state.state();

    // always stream full gstate, which is not required, but...
    // [clip path support] quick fix: cannot overwrite DirtyClipPath or DirtyClipRegion
    // otherwise only clip path will be considered even if clip region is set (which
    // will output incorrect clip path in this case)
    flags |= QPaintEngine::DirtyBrush |
             QPaintEngine::DirtyPen |
             QPaintEngine::DirtyTransform |
             QPaintEngine::DirtyFont |
             QPaintEngine::DirtyOpacity;

    // close old state and start a new one...
    if (d->afterFirstUpdate)
    {
        stream() << "</g>\n\n";
        if (trimEmptyGroupEnabled())
            stashStream().readAll();
    }

    d->discardStashStream = trimEmptyGroupEnabled();

    stream() << "<g ";

    if (flags & QPaintEngine::DirtyBrush) {
        qbrushToSvg(state.brush());
    }

    if (flags & QPaintEngine::DirtyPen) {
        qpenToSvg(state.pen());
    }

    if (flags & QPaintEngine::DirtyTransform) {
        d->matrix = state.matrix();
        stream() << "transform=\"matrix(" << d->matrix.m11() << ','
                   << d->matrix.m12() << ','
                   << d->matrix.m21() << ',' << d->matrix.m22() << ','
                   << d->matrix.dx() << ',' << d->matrix.dy()
                   << ")\""
                   << endl;
    }

    if (flags & QPaintEngine::DirtyFont) {
        qfontToSvg(state.font());
    }

    if (flags & QPaintEngine::DirtyOpacity) {
        if (!qFuzzyIsNull(state.opacity() - 1))
            stream() << "opacity=\""<<state.opacity()<<"\" ";
    }

    if (clipPathEnabled()) {
        if (state.isClipEnabled() && state.painter()->hasClipping())
        {
            if ((flags & (QPaintEngine::DirtyClipPath | QPaintEngine::DirtyClipRegion))
                || !qFuzzyCompare(d_func()->matrixClipPath, state.matrix())) {
                d_func()->clipPath = state.painter()->clipPath();
                d_func()->matrixClipPath = state.matrix();
            }
            /*else if (!qFuzzyCompare(d_func()->matrixClipPath, state.matrix())) {
                // target clip path = clip path * matrixClipPath = stateMatrix.inverted().map(target clip path) * stateMatrix
                // so the new clip path in stateMatrix = stateMatrix.inverted().map(clip path * matrixClipPath).
                const QMatrix& stateMatrix = state.matrix();
                d_func()->clipPath = stateMatrix.inverted().map(d_func()->matrixClipPath.map(d_func()->clipPath));
                d_func()->matrixClipPath = stateMatrix;
            }*/
        } else {
            d_func()->clipPath = QPainterPath();
        }
        QString id = getClipPathId(state);
        if (!id.isNull())
            stream() << QStringLiteral("clip-path=\"url(#%1)\" ").arg(id);
    }

    stream() << '>' << endl;

    d->afterFirstUpdate = true;
}

void QSvgPaintEngine::drawEllipse(const QRectF &r)
{
    Q_D(QSvgPaintEngine);

    holdStashStream();

    const bool isCircle = r.width() == r.height();
    *d->stream << '<' << (isCircle ? "circle" : "ellipse");
    if (state->pen().isCosmetic())
        *d->stream << " vector-effect=\"non-scaling-stroke\"";
    const QPointF c = r.center();
    *d->stream << " cx=\"" << c.x() << "\" cy=\"" << c.y();
    if (isCircle)
        *d->stream << "\" r=\"" << r.width() / qreal(2.0);
    else
        *d->stream << "\" rx=\"" << r.width() / qreal(2.0) << "\" ry=\"" << r.height() / qreal(2.0);
    *d->stream << "\"/>" << endl;
}

void QSvgPaintEngine::drawPath(const QPainterPath &p)
{
    Q_D(QSvgPaintEngine);

    holdStashStream();

    *d->stream << "<path vector-effect=\""
               << (state->pen().isCosmetic() ? "non-scaling-stroke" : "none")
               << "\" fill-rule=\""
               << (p.fillRule() == Qt::OddEvenFill ? "evenodd" : "nonzero")
               << "\" d=\"";

    for (int i=0; i<p.elementCount(); ++i) {
        const QPainterPath::Element &e = p.elementAt(i);
        switch (e.type) {
        case QPainterPath::MoveToElement:
            *d->stream << 'M' << e.x << ',' << e.y;
            break;
        case QPainterPath::LineToElement:
            *d->stream << 'L' << e.x << ',' << e.y;
            break;
        case QPainterPath::CurveToElement:
            *d->stream << 'C' << e.x << ',' << e.y;
            ++i;
            while (i < p.elementCount()) {
                const QPainterPath::Element &e = p.elementAt(i);
                if (e.type != QPainterPath::CurveToDataElement) {
                    --i;
                    break;
                } else
                    *d->stream << ' ';
                *d->stream << e.x << ',' << e.y;
                ++i;
            }
            break;
        default:
            break;
        }
        if (i != p.elementCount() - 1) {
            *d->stream << ' ';
        }
    }

    *d->stream << "\"/>" << endl;
}

void QSvgPaintEngine::drawPolygon(const QPointF *points, int pointCount,
                                  PolygonDrawMode mode)
{
    Q_ASSERT(pointCount >= 2);

    //Q_D(QSvgPaintEngine);

    holdStashStream();

    QPainterPath path(points[0]);
    for (int i=1; i<pointCount; ++i)
        path.lineTo(points[i]);

    if (mode == PolylineMode) {
        stream() << "<polyline fill=\"none\" vector-effect=\""
                 << (state->pen().isCosmetic() ? "non-scaling-stroke" : "none")
                 << "\" points=\"";
        for (int i = 0; i < pointCount; ++i) {
            const QPointF &pt = points[i];
            stream() << pt.x() << ',' << pt.y() << ' ';
        }
        stream() << "\" />" <<endl;
    } else {
        path.closeSubpath();
        drawPath(path);
    }
}

void QSvgPaintEngine::drawRects(const QRectF *rects, int rectCount)
{
    Q_D(QSvgPaintEngine);

    holdStashStream();

    bool bTexturePattern = false;
    if (state->pen().style() == Qt::NoPen &&
        state->brush().style() == Qt::TexturePattern) {
        bTexturePattern = true;
    }

    for (int i=0; i < rectCount; ++i) {
        const QRectF &rect = rects[i].normalized();
        *d->stream << "<rect";
        if (state->pen().isCosmetic())
            *d->stream << " vector-effect=\"non-scaling-stroke\"";
        *d->stream << " x=\"" << rect.x() << "\" y=\"" << rect.y()
                   << "\" width=\"" << rect.width() << "\" height=\"" << rect.height();

        if (bTexturePattern) {
            QString patternId = saveTexturePatternBrush(qRound(rect.x()), qRound(rect.y()), state->brush());
            if (!patternId.isEmpty()) {
                *d->stream << "\" style=\"fill:url(#" << patternId << ");";
            }
        }

        *d->stream << "\"/>" << endl;
    }
}

void QSvgPaintEngine::drawTextItem(const QPointF &pt, const QTextItem &textItem)
{
    Q_D(QSvgPaintEngine);
    if (d->pen.style() == Qt::NoPen)
        return;

    holdStashStream();

    const QTextItemInt &ti = static_cast<const QTextItemInt &>(textItem);
    // Drawing glyphs draw as path and return after drawing the path.
    if (ti.chars == 0 || (ti.oprFlags & QTextItem::UseDrawGlyphs))
    {
        QPaintEngine::drawTextItem(pt, ti); // Draw as path
        return;
    }
    QString s = QString::fromRawData(ti.chars, ti.num_chars);

    *d->stream << "<text "
                  "fill=\"" << d->attributes.stroke << "\" "
                  "fill-opacity=\"" << d->attributes.strokeOpacity << "\" "
                  "stroke=\"none\" "
                  "xml:space=\"preserve\" ";

    if (!textCoordEnabled() || ti.glyphs.numGlyphs <= 1)
    {
        *d->stream << "x=\"" << pt.x() << "\" y=\"" << pt.y() << "\" ";
        if (d->font.verticalMetrics() && ti.glyphs.numGlyphs == 1)
        {
            QFontMetrics fontMetrics(d->font);
            appendTransformToSvg(pt.x(), pt.y(), fontMetrics.lineSpacing(), fontMetrics.ascent(), -90);
        }
    }
    else
    {
        const QFixedPoint* positions = ti.glyphs.offsets;
        qreal x = pt.x();
        qreal y = pt.y();
        *d->stream << "x=\"";
        for (int i = 0; i < ti.glyphs.numGlyphs; ++i)
        {
            x += (positions + i)->x.toReal();
            *d->stream << x;
            if (i < ti.glyphs.numGlyphs - 1)
                *d->stream << " ";
            x += ti.glyphs.advances[i].toReal();
        }

        *d->stream << "\" y=\"";
        for (int i = 0; i < ti.glyphs.numGlyphs; ++i)
        {
            *d->stream << y + (positions + i)->y.toReal();
            if (i < ti.glyphs.numGlyphs - 1)
                *d->stream << " ";
        }

        *d->stream << "\" ";
    }

    qfontToSvg(textItem.font());
    *d->stream << " >"
               << s.toHtmlEscaped()
               << "</text>"
               << endl;
}

QT_END_NAMESPACE

#endif // QT_NO_SVGGENERATOR
