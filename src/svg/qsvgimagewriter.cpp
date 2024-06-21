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

#include "qsvgimagewriter.h"

#ifndef QT_NO_SVG
#include <QtCore/QXmlStreamWriter>
#include <QtCore/QBuffer>
#include "private/qdrawhelper_p.h"

#include "qsvggraphics_p.h"
#include "qsvgtinydocument_p.h"

QT_BEGIN_NAMESPACE

//-----------------------------------------------------------------------------
#define QTSVG_INV_PREMUL(p)                                   \
    (qAlpha(p) == 0 ? 0 :                               \
    ((qAlpha(p) << 24)                                  \
     | (((255*qRed(p))/ qAlpha(p)) << 16)               \
     | (((255*qGreen(p)) / qAlpha(p))  << 8)            \
     | ((255*qBlue(p)) / qAlpha(p))))

#if QT_POINTER_SIZE == 8 // 64-bit versions

Q_ALWAYS_INLINE uint QTSVG_PREMUL(uint x) {
    uint a = x >> 24;
    quint64 t = (((quint64(x)) | ((quint64(x)) << 24)) & 0x00ff00ff00ff00ff) * a;
    t = (t + ((t >> 8) & 0xff00ff00ff00ff) + 0x80008000800080) >> 8;
    t &= 0x000000ff00ff00ff;
    return (uint(t)) | (uint(t >> 24)) | (a << 24);
}

#else // 32-bit versions

Q_ALWAYS_INLINE uint QTSVG_PREMUL(uint x)
{
    uint a = x >> 24;
    uint t = (x & 0xff00ff) * a;
    t = (t + ((t >> 8) & 0xff00ff) + 0x800080) >> 8;
    t &= 0xff00ff;

    x = ((x >> 8) & 0xff) * a;
    x = (x + ((x >> 8) & 0xff) + 0x80);
    x &= 0xff00;
    x |= t | (a << 24);
    return x;
}
#endif

QString translate_color(const QColor &color)
{
    if (color.alpha() != 0xff) {
        return QString::fromLatin1("rgba(%1,%2,%3,%4)")
                .arg(color.red())
                .arg(color.green())
                .arg(color.blue())
                .arg(color.alphaF());
    }
    return QString::fromLatin1("#%1%2%3")
            .arg(color.red(), 2, 16, QLatin1Char('0'))
            .arg(color.green(), 2, 16, QLatin1Char('0'))
            .arg(color.blue(), 2, 16, QLatin1Char('0'));
}

QString translate_dashPattern(const QVector<qreal>& pattern, const qreal& width)
{
    QString patten;
    // Note that SVG operates in absolute lengths, whereas Qt uses a length/width ratio.
    for (qreal entry : pattern)
        patten += QString::fromLatin1("%1,").arg(entry * width);

    patten.chop(1);
    return patten;
}

QString translate_matrix(const QMatrix& matrix)
{
    return QString::fromLatin1("matrix(%1,%2,%3,%4,%5,%6)")
        .arg(matrix.m11()).arg(matrix.m12())
        .arg(matrix.m21()).arg(matrix.m22())
        .arg(matrix.dx()).arg(matrix.dy());
}

QString translate_path(const QPainterPath& path)
{
    QString strPath;
    int count = path.elementCount();
    for (int i = 0; i < count; i++)
    {
        const QPainterPath::Element& element = path.elementAt(i);
        switch (element.type)
        {
        case QPainterPath::MoveToElement:
            strPath.append(QString::fromLatin1("M %1 %2 ").arg(element.x).arg(element.y));
            break;
        case QPainterPath::LineToElement:
            strPath.append(QString::fromLatin1("L %1 %2 ").arg(element.x).arg(element.y));
            break;
        case QPainterPath::CurveToElement:
            strPath.append(QString::fromLatin1("C %1 %2 ").arg(element.x).arg(element.y));
            ++i;
            while (i < path.elementCount())
            {
                const QPainterPath::Element& curEle = path.elementAt(i);
                if (curEle.type != QPainterPath::CurveToDataElement)
                {
                    --i;
                    break;
                }
                else
                {
                    strPath.append(QString::fromLatin1("%1 %2 ").arg(curEle.x).arg(curEle.y));
                    ++i;
                }
            }
            break;
        default:
            Q_ASSERT(false);
            break;
        }
    }

    if (count >= 2 && path.elementAt(0).operator QPointF() == path.elementAt(count - 1).operator QPointF())
        strPath.push_back(QChar::fromLatin1('Z'));
    else
        strPath.chop(1);
    return strPath;
}

QString translate_node(QSvgNode::Type type)
{
    static const char* QSvgNode2Str[] = {
        "svg",
        "g",
        "defs",
        "switch",
        "marker",
        "clipPath",
        "animation",
        "arc",
        "circle",
        "ellipse",
        "image",
        "line",
        "path",
        "polygon",
        "polyline",
        "rect",
        "text",
        "textarea",
        "tspan",
        "use",
        "video",
        "pattern"
    };
    return QLatin1String(QSvgNode2Str[type]);
}

QString translate_displaymode(QSvgNode::DisplayMode type)
{
    static const char* DisplayMode2Str[] = {
        "inline",
        "block",
        "list-item",
        "run-in",
        "compact",
        "marker",
        "table",
        "inline-table",
        "table-row",
        "table-header-group",
        "table-footer-group",
        "table-row",
        "table-column-group",
        "table-column",
        "table-cell",
        "table-caption",
        "none",
        "inherit",
    };
    return QLatin1String(DisplayMode2Str[type]);
}

QString translate_length(qreal len, bool bPercent)
{
    if(!bPercent)
        return QString::number(len);
    else
        return QString::fromLatin1("%1%").arg(len);
}


QString translateCoordList(const QVector<qreal> &list)
{
    QString str;
    for (const qreal coord : list) {
        str.append(QString::fromLatin1("%1 ").arg(QString::number(coord)));
    }
    return str;
}

void writeSolidColor(QXmlStreamWriter* pWriter, const QString& id, const QSvgSolidColorStyle* pStyle)
{
    if (!pWriter || !pStyle)
        return;

    pWriter->writeStartElement(QLatin1String("solidcolor"));

    const QColor& color = pStyle->qcolor();
    pWriter->writeAttribute(QLatin1String("id"), id);
    pWriter->writeAttribute(QLatin1String("solid-color"), translate_color(color));
    pWriter->writeAttribute(QLatin1String("solid-opacity"), QString::number(color.alphaF()));

    pWriter->writeEndElement();
}

void writeGradientBase(QXmlStreamWriter* pWriter, const QSvgGradientStyle* pStyle)
{
    if (!pStyle->stopLink().isEmpty())
        pWriter->writeAttribute(QLatin1String("xlink:href"), pStyle->stopLink());

    if (pStyle->qgradient()->spread() == QGradient::PadSpread)
        pWriter->writeAttribute(QLatin1String("spreadMethod"), QLatin1String("pad"));
    else if (pStyle->qgradient()->spread() == QGradient::ReflectSpread)
        pWriter->writeAttribute(QLatin1String("spreadMethod"), QLatin1String("reflect"));
    else if (pStyle->qgradient()->spread() == QGradient::RepeatSpread)
        pWriter->writeAttribute(QLatin1String("spreadMethod"), QLatin1String("repeat"));

    QGradient::CoordinateMode eCoordinateMode = pStyle->qgradient()->coordinateMode();
    if (QGradient::ObjectBoundingMode == eCoordinateMode || QGradient::ObjectMode == eCoordinateMode)
        pWriter->writeAttribute(QLatin1String("gradientUnits"), QLatin1String("objectBoundingBox"));
    else
        pWriter->writeAttribute(QLatin1String("gradientUnits"), QLatin1String("userSpaceOnUse"));

    if (pStyle->matrixSet())
    {
        pWriter->writeAttribute(QLatin1String("gradientTransform"),
            translate_matrix(pStyle->qmatrix()));
    }

    if (pStyle->gradientStopsSet())
    {
        QGradientStops stops = pStyle->qgradient()->stops();
        if (pStyle->qgradient()->interpolationMode() == QGradient::ColorInterpolation)
        {
            //copy from qsvggenerator.cpp
            bool constantAlpha = true;
            int alpha = stops.at(0).second.alpha();
            for (int i = 1; i < stops.size(); ++i)
                constantAlpha &= (stops.at(i).second.alpha() == alpha);

            if (!constantAlpha)
            {
                const qreal spacing = qreal(0.02);
                QGradientStops newStops;
                QRgb fromColor = QTSVG_PREMUL(stops.at(0).second.rgba());
                QRgb toColor;
                for (int i = 0; i + 1 < stops.size(); ++i)
                {
                    int parts = qCeil((stops.at(i + 1).first - stops.at(i).first) / spacing);
                    newStops.append(stops.at(i));
                    toColor = QTSVG_PREMUL(stops.at(i + 1).second.rgba());

                    if (parts > 1)
                    {
                        qreal step = (stops.at(i + 1).first - stops.at(i).first) / parts;
                        for (int j = 1; j < parts; ++j)
                        {
                            QRgb color = QTSVG_INV_PREMUL(INTERPOLATE_PIXEL_256(fromColor, 256 - 256 * j / parts, toColor, 256 * j / parts));
                            newStops.append(QGradientStop(stops.at(i).first + j * step, QColor::fromRgba(color)));
                        }
                    }
                    fromColor = toColor;
                }
                newStops.append(stops.back());
                stops = newStops;
            }
        }

        for (int i = 0; i < stops.size(); ++i)
        {
            pWriter->writeStartElement(QLatin1String("stop"));
            pWriter->writeAttribute(QLatin1String("offset"), QString::number(stops[i].first));
            pWriter->writeAttribute(QLatin1String("stop-color"), translate_color(stops[i].second));
            pWriter->writeAttribute(QLatin1String("stop-opacity"), QString::number(stops[i].second.alphaF()));
            pWriter->writeEndElement();
        }
    }
}

void writeGradient(QXmlStreamWriter* pWriter, const QString& id, const QSvgGradientStyle* pStyle)
{
    if (!pWriter || !pStyle)
        return;

    const QGradient* gra = pStyle->qgradient();
    if (gra->type() == QGradient::LinearGradient)
    {
        pWriter->writeStartElement(QLatin1String("linearGradient"));
        pWriter->writeAttribute(QLatin1String("id"), id);

        const QLinearGradient* lineGra = static_cast<const QLinearGradient*>(gra);
        pWriter->writeAttribute(QLatin1String("x1"), QString::number(lineGra->start().x()));
        pWriter->writeAttribute(QLatin1String("y1"), QString::number(lineGra->start().y()));
        pWriter->writeAttribute(QLatin1String("x2"), QString::number(lineGra->finalStop().x()));
        pWriter->writeAttribute(QLatin1String("y2"), QString::number(lineGra->finalStop().y()));

        writeGradientBase(pWriter, pStyle);
        pWriter->writeEndElement();
    }
    else if (gra->type() == QGradient::RadialGradient)
    {
        pWriter->writeStartElement(QLatin1String("radialGradient"));
        pWriter->writeAttribute(QLatin1String("id"), id);

        const QRadialGradient* radioGra = static_cast<const QRadialGradient*>(gra);
        pWriter->writeAttribute(QLatin1String("cx"), QString::number(radioGra->center().x()));
        pWriter->writeAttribute(QLatin1String("cy"), QString::number(radioGra->center().y()));
        pWriter->writeAttribute(QLatin1String("r"), QString::number(radioGra->radius()));
        pWriter->writeAttribute(QLatin1String("fx"), QString::number(radioGra->focalPoint().x()));
        pWriter->writeAttribute(QLatin1String("fy"), QString::number(radioGra->focalPoint().y()));

        writeGradientBase(pWriter, pStyle);
        pWriter->writeEndElement();
    }
}

//-----------------------------------------------------------------------------
class QSvgStyleWriter
{
public:
    QSvgStyleWriter(QXmlStreamWriter* writer, const QSvgNode* node)
        : m_pWriter(writer), m_pNode(node), m_style(node->style())
    {
    }
    void write()
    {
        writeVisible();
        writeFill();
        writeFont();
        writeStroke();
        writeTransform();
        writerOpacity();
        writeCompositionMode();
        wirteClipPathStyle();
        wirteClipRuleMode();
        writeDisplayMode();
    }

private:
    void writeVisible();
    void writeFill();
    void writeFont();
    void writeStroke();
    void writeTransform();
    void writerOpacity();
    void writeCompositionMode();
    void wirteClipPathStyle();
    void wirteClipRuleMode();
    void writeDisplayMode();

private:
    QXmlStreamWriter* m_pWriter;
    const QSvgNode*   m_pNode;
    const QSvgStyle&  m_style;
};

void QSvgStyleWriter::writeFill()
{
    if (!m_style.fill)
        return;

    const QSvgFillStyle* fill = m_style.fill;
    //fill
    if (fill->isFillSet())
    {
        if (fill->style()) 
        {
            if (fill->style()->type() != QSvgStyleProperty::INNER_GRADIENT)
                m_pWriter->writeAttribute(QLatin1String("fill"), QString::fromLatin1("url(#%1)").arg(fill->gradientId()));
        }
        else if (fill->qbrush().style() != Qt::NoBrush)
            m_pWriter->writeAttribute(QLatin1String("fill"), translate_color(fill->qbrush().color()));
        else
            m_pWriter->writeAttribute(QLatin1String("fill"), QLatin1String("none"));
    }

    if (!fill->patternId().isEmpty())
        m_pWriter->writeAttribute(QLatin1String("fill"), QString::fromLatin1("url(#%1)").arg(fill->patternId()));

    //fill-opacity
    if (fill->isFillOpacitySet())
        m_pWriter->writeAttribute(QLatin1String("fill-opacity"), QString::number(fill->fillOpacity()));

    //fill-rule
    if (fill->isFillRuleSet())
    {
        if (fill->fillRule() == Qt::OddEvenFill)
            m_pWriter->writeAttribute(QLatin1String("fill-rule"), QLatin1String("evenodd"));
        else if (fill->fillRule() == Qt::WindingFill)
            m_pWriter->writeAttribute(QLatin1String("fill-rule"), QLatin1String("nonzero"));
    }
}

void QSvgStyleWriter::wirteClipPathStyle() 
{
    const QSvgClipPathStyle* clip = m_style.clipPath;
    if (clip != nullptr && !clip->clipPathId().isEmpty())
        m_pWriter->writeAttribute(QLatin1String("clip-path"), QString("url(#%1)").arg(clip->clipPathId()));
}

void QSvgStyleWriter::writeFont()
{
    if (!m_style.font)
        return;

    const QSvgFontStyle* font = m_style.font;
    if (font->isFamilySet())
        m_pWriter->writeAttribute(QLatin1String("font-family"), font->qfont().family());

    if (font->isSizeSet())
        m_pWriter->writeAttribute(QLatin1String("font-size"), QString::number(font->qfont().pointSizeF()));

    if (font->isStyleSet())
    {
        const QFont::Style& style = font->qfont().style();
        if (style == QFont::StyleNormal)
            m_pWriter->writeAttribute(QLatin1String("font-style"), QLatin1String("normal"));
        else if (style == QFont::StyleItalic)
            m_pWriter->writeAttribute(QLatin1String("font-style"), QLatin1String("italic"));
        else if (style == QFont::StyleOblique)
            m_pWriter->writeAttribute(QLatin1String("font-style"), QLatin1String("oblique"));
    }

    if (font->isWeightSet())
    {
        int weight = font->weight();
        if (weight == QSvgFontStyle::BOLDER)
            m_pWriter->writeAttribute(QLatin1String("font-weight"), QLatin1String("bolder"));
        else if (weight == QSvgFontStyle::LIGHTER)
            m_pWriter->writeAttribute(QLatin1String("font-weight"), QLatin1String("lighter"));
        else
            m_pWriter->writeAttribute(QLatin1String("font-weight"), QString::number(weight));
    }

    if (font->isVariantSet())
    {
        if (font->qfont().capitalization() == QFont::MixedCase)
            m_pWriter->writeAttribute(QLatin1String("font-variant"), QLatin1String("normal"));
        else if (font->qfont().capitalization() == QFont::SmallCaps)
            m_pWriter->writeAttribute(QLatin1String("font-variant"), QLatin1String("small-caps"));
    }

    if (font->isTextAnchorSet())
    {
        const Qt::Alignment& align = font->textAnchor();
        if (align == Qt::AlignLeft)
            m_pWriter->writeAttribute(QLatin1String("text-anchor"), QLatin1String("start"));
        else if (align == Qt::AlignHCenter)
            m_pWriter->writeAttribute(QLatin1String("text-anchor"), QLatin1String("middle"));
        else if (align == Qt::AlignRight)
            m_pWriter->writeAttribute(QLatin1String("text-anchor"), QLatin1String("end"));
    }
}

void QSvgStyleWriter::writeStroke()
{
    if (!m_style.stroke)
        return;

    const QSvgStrokeStyle* strokeStyle = m_style.stroke;
    const QPen& stroke = strokeStyle->stroke();
    //stroke
    if (strokeStyle->isStrokeSet())
    {
        if (strokeStyle->style())
        {
            Q_ASSERT(!strokeStyle->gradientId().isEmpty());
            m_pWriter->writeAttribute(QLatin1String("stroke"), QString::fromLatin1("url(#%1)").arg(strokeStyle->gradientId()));
        }
        else
        {
            const QBrush& brush = stroke.brush();
            if (brush.style() != Qt::NoBrush)
                m_pWriter->writeAttribute(QLatin1String("stroke"), translate_color(brush.color()));
            else
                m_pWriter->writeAttribute(QLatin1String("stroke"), QLatin1String("none"));
        }
    }

    //stroke-width
    if (strokeStyle->isStrokeWidthSet())
        m_pWriter->writeAttribute(QLatin1String("stroke-width"), QString::number(strokeStyle->width()));

    //stroke-dasharray
    if (strokeStyle->isStrokeDashArraySet())
    {
        qreal width = strokeStyle->isStrokeWidthSet() ? strokeStyle->width() : 1.0;
        const QVector<qreal>& dashs = stroke.dashPattern();
        if (!dashs.empty())
            m_pWriter->writeAttribute(QLatin1String("stroke-dasharray"), translate_dashPattern(dashs, width));
    }

    //stroke-linejoin
    if (strokeStyle->isStrokeLineJoinSet())
    {
        switch (stroke.joinStyle())
        {
        case Qt::SvgMiterJoin:
            m_pWriter->writeAttribute(QLatin1String("stroke-linejoin"), QLatin1String("miter"));
            break;
        case Qt::RoundJoin:
            m_pWriter->writeAttribute(QLatin1String("stroke-linejoin"), QLatin1String("round"));
            break;
        case Qt::BevelJoin:
            m_pWriter->writeAttribute(QLatin1String("stroke-linejoin"), QLatin1String("bevel"));
            break;
        default:
            break;
        }
    }

    //stroke-linecap
    if (strokeStyle->isStrokeLineCapSet())
    {
        switch (stroke.capStyle())
        {
        case Qt::FlatCap:
            m_pWriter->writeAttribute(QLatin1String("stroke-linecap"), QLatin1String("butt"));
            break;
        case Qt::RoundCap:
            m_pWriter->writeAttribute(QLatin1String("stroke-linecap"), QLatin1String("round"));
            break;
        case Qt::SquareCap:
            m_pWriter->writeAttribute(QLatin1String("stroke-linecap"), QLatin1String("square"));
            break;
        default:
            break;
        }
    }

    //stroke-dashoffset
    if (strokeStyle->isStrokeDashOffsetSet())
        m_pWriter->writeAttribute(QLatin1String("stroke-dashoffset"), QString::number(strokeStyle->strokeDashOffset()));

    //vector-effect
    if (strokeStyle->isVectorEffectSet())
    {
        m_pWriter->writeAttribute(QLatin1String("vector-effect"),
            QLatin1String(strokeStyle->vectorEffect() ? ("non-scaling-stroke") : "none"));
    }

    //stroke-miterlimit
    if (strokeStyle->isStrokeMiterLimitSet())
        m_pWriter->writeAttribute(QLatin1String("stroke-miterlimit"), QString::number(stroke.miterLimit()));

    //stroke-opacity
    if (strokeStyle->isStrokeOpacitySet())
        m_pWriter->writeAttribute(QLatin1String("stroke-opacity"), QString::number(strokeStyle->strokeOpacity()));
}

void QSvgStyleWriter::writeTransform()
{
    if (!m_style.transform)
        return;

    const QTransform& trans = m_style.transform->qtransform();
    m_pWriter->writeAttribute(QLatin1String("transform"), translate_matrix(trans.toAffine()));
}

void QSvgStyleWriter::writeVisible()
{
    if(!m_pNode->isVisible())
        m_pWriter->writeAttribute(QLatin1String("visibility"), QLatin1String("hidden"));
}

void QSvgStyleWriter::writerOpacity()
{
    if (m_style.opacity)
        m_pWriter->writeAttribute(QLatin1String("opacity"), QString::number(m_style.opacity->opacity()));
}

void QSvgStyleWriter::writeCompositionMode()
{
    if (!m_style.compop)
        return;

    switch (m_style.compop->compOp())
    {
    case QPainter::CompositionMode_Clear:
        m_pWriter->writeAttribute(QLatin1String("comp-op"), QLatin1String("clear"));
        break;
    case QPainter::CompositionMode_Source:
        m_pWriter->writeAttribute(QLatin1String("comp-op"), QLatin1String("src"));
        break;
    case QPainter::CompositionMode_Destination:
        m_pWriter->writeAttribute(QLatin1String("comp-op"), QLatin1String("dst"));
        break;
    case QPainter::CompositionMode_SourceOver:
        m_pWriter->writeAttribute(QLatin1String("comp-op"), QLatin1String("src-over"));
        break;
    case QPainter::CompositionMode_DestinationOver:
        m_pWriter->writeAttribute(QLatin1String("comp-op"), QLatin1String("dst-out"));
        break;
    case QPainter::CompositionMode_SourceIn:
        m_pWriter->writeAttribute(QLatin1String("comp-op"), QLatin1String("src-in"));
        break;
    case QPainter::CompositionMode_DestinationIn:
        m_pWriter->writeAttribute(QLatin1String("comp-op"), QLatin1String("dst-in"));
        break;
    case QPainter::CompositionMode_SourceOut:
        m_pWriter->writeAttribute(QLatin1String("comp-op"), QLatin1String("src-out"));
        break;
    case QPainter::CompositionMode_DestinationOut:
        m_pWriter->writeAttribute(QLatin1String("comp-op"), QLatin1String("dst-out"));
        break;
    case QPainter::CompositionMode_SourceAtop:
        m_pWriter->writeAttribute(QLatin1String("comp-op"), QLatin1String("src-atop"));
        break;
    case QPainter::CompositionMode_DestinationAtop:
        m_pWriter->writeAttribute(QLatin1String("comp-op"), QLatin1String("dst-atop"));
        break;
    case QPainter::CompositionMode_Xor:
        m_pWriter->writeAttribute(QLatin1String("comp-op"), QLatin1String("xor"));
        break;
    case QPainter::CompositionMode_Plus:
        m_pWriter->writeAttribute(QLatin1String("comp-op"), QLatin1String("plus"));
        break;
    case QPainter::CompositionMode_Multiply:
        m_pWriter->writeAttribute(QLatin1String("comp-op"), QLatin1String("multiply"));
        break;
    case QPainter::CompositionMode_Screen:
        m_pWriter->writeAttribute(QLatin1String("comp-op"), QLatin1String("screen"));
        break;
    case QPainter::CompositionMode_Overlay:
        m_pWriter->writeAttribute(QLatin1String("comp-op"), QLatin1String("overlay"));
        break;
    case QPainter::CompositionMode_Darken:
        m_pWriter->writeAttribute(QLatin1String("comp-op"), QLatin1String("darken"));
        break;
    case QPainter::CompositionMode_Lighten:
        m_pWriter->writeAttribute(QLatin1String("comp-op"), QLatin1String("lighten"));
        break;
    case QPainter::CompositionMode_ColorDodge:
        m_pWriter->writeAttribute(QLatin1String("comp-op"), QLatin1String("color-dodge"));
        break;
    case QPainter::CompositionMode_ColorBurn:
        m_pWriter->writeAttribute(QLatin1String("comp-op"), QLatin1String("color-burn"));
        break;
    case QPainter::CompositionMode_HardLight:
        m_pWriter->writeAttribute(QLatin1String("comp-op"), QLatin1String("hard-light"));
        break;
    case QPainter::CompositionMode_SoftLight:
        m_pWriter->writeAttribute(QLatin1String("comp-op"), QLatin1String("soft-light"));
        break;
    case QPainter::CompositionMode_Difference:
        m_pWriter->writeAttribute(QLatin1String("comp-op"), QLatin1String("difference"));
        break;
    case QPainter::CompositionMode_Exclusion:
        m_pWriter->writeAttribute(QLatin1String("comp-op"), QLatin1String("exclusion"));
        break;
    default:
        break;
    }
}

void QSvgStyleWriter::wirteClipRuleMode()
{
    if (!m_pNode->isClipRuleSet())
        return;
    if (Qt::WindingFill == m_pNode->clipRule())
        m_pWriter->writeAttribute(QLatin1String("clip-rule"), QLatin1String("nonzero"));
    if (Qt::OddEvenFill == m_pNode->clipRule())
        m_pWriter->writeAttribute(QLatin1String("clip-rule"), QLatin1String("evenodd"));
}

void QSvgStyleWriter::writeDisplayMode()
{
    if (m_pNode->displayMode() != QSvgNode::BlockMode)
        m_pWriter->writeAttribute(QLatin1String("display"), translate_displaymode(m_pNode->displayMode()));
}

//-----------------------------------------------------------------------------
class QSvgImageWriterPrivate
{
public:
    QSvgImageWriterPrivate(const QSvgTinyDocument* doc, QIODevice* device)
        :doc(doc), xmlWriter(device)
    {
    }
    QSvgImageWriterPrivate(const QSvgTinyDocument* doc, QByteArray* arr)
        :doc(doc), xmlWriter(arr)
    {
    }
    bool write();

protected:
    void writeDoc(const QSvgTinyDocument* doc);
    void writeSvgNode(const QSvgNode* node);
    void writeStructure(const QSvgStructureNode* structureNode);
    void writeMarker(const QSvgMarker *node);
    void writeClipPath(const QSvgClipPath *node);
    void writeEllipse(const QSvgEllipse* node);
    void writeImage(const QSvgImage* node);
    void writeLine(const QSvgLine* node);
    void writePath(const QSvgPath* node);
    void writePattern(const QSvgPattern* node);
    void writePoly(const QSvgNode* node);
    void writeRect(const QSvgRect* node);
    void writeText(const QSvgText* node);
    void writeUse(const QSvgUse* node);
    void writeTspan(const QSvgTspan *node);

    void writeNodeCore(const QSvgNode* node);
    void writeNodeStyle(const QSvgNode* node);
    void writePolyInner(const QPolygonF& poly);

    void writeViewBox(const QSvgTinyDocument* doc);
    void writeDefs(const QSvgTinyDocument* doc);
    void writeSvgFont(const QSvgTinyDocument* doc);

public:
    QXmlStreamWriter xmlWriter;
    const QSvgTinyDocument* doc;
};

bool QSvgImageWriterPrivate::write()
{
    if (!xmlWriter.device()) 
    {
        qWarning("QSvgImageWriterPrivate::write(), no output device");
        return false;
    }

    if (!xmlWriter.device()->isOpen()) 
    {
        if (!xmlWriter.device()->open(QIODevice::WriteOnly | QIODevice::Text)) {
            qWarning("QSvgImageWriterPrivate::write(), could not open output device: '%s'",
                qPrintable(xmlWriter.device()->errorString()));
            return false;
        }
    }
    else if (!xmlWriter.device()->isWritable()) 
    {
        qWarning("QSvgImageWriterPrivate::write(), could not write to read-only output device: '%s'",
            qPrintable(xmlWriter.device()->errorString()));
        return false;
    }

    xmlWriter.setCodec("UTF-8");
    xmlWriter.writeStartDocument(QLatin1String("1.0"), true);
    writeSvgNode(doc);
    xmlWriter.writeEndDocument();
    return true;
}

void QSvgImageWriterPrivate::writeDoc(const QSvgTinyDocument * doc)
{
    xmlWriter.writeAttribute(QLatin1String("xmlns"), QLatin1String("http://www.w3.org/2000/svg"));
    xmlWriter.writeAttribute(QLatin1String("xmlns:xlink"), QLatin1String("http://www.w3.org/1999/xlink"));
    xmlWriter.writeAttribute(QLatin1String("version"), QLatin1String("1.2"));
    xmlWriter.writeAttribute(QLatin1String("baseProfile"), QLatin1String("tiny"));
    xmlWriter.writeAttribute(QLatin1String("x"), QString::number(doc->coord().x()));
    xmlWriter.writeAttribute(QLatin1String("y"), QString::number(doc->coord().y()));
    xmlWriter.writeAttribute(QLatin1String("width"), translate_length(doc->width(), doc->widthPercent()));
    xmlWriter.writeAttribute(QLatin1String("height"), translate_length(doc->height(), doc->heightPercent()));
    writeViewBox(doc);
    writeDefs(doc);
    writeSvgFont(doc);

    writeStructure(doc);
}

void QSvgImageWriterPrivate::writeSvgNode(const QSvgNode* node)
{
    xmlWriter.writeStartElement(translate_node(node->type()));

    if (!node->nodeId().isEmpty())
        xmlWriter.writeAttribute(QLatin1String("id"), node->nodeId());
    writeNodeCore(node);
    writeNodeStyle(node);

    switch (node->type())
    {
    case QSvgNode::DOC:
        writeDoc(static_cast<const QSvgTinyDocument*>(node));
        break;
    case QSvgNode::G:
    case QSvgNode::DEFS:
    case QSvgNode::SWITCH:
        writeStructure(static_cast<const QSvgStructureNode*>(node));
        break;
    case QSvgNode::MARKER:
        writeMarker(static_cast<const QSvgMarker*>(node));
        writeStructure(static_cast<const QSvgStructureNode*>(node));
        break;
    case QSvgNode::CLIPPATH:
        writeClipPath(static_cast<const QSvgClipPath*>(node));
        writeStructure(static_cast<const QSvgStructureNode *>(node));
        break;
    case QSvgNode::CIRCLE:
    case QSvgNode::ELLIPSE:
        writeEllipse(static_cast<const QSvgEllipse*>(node));
        break;
    case QSvgNode::IMAGE:
        writeImage(static_cast<const QSvgImage*>(node));
        break;
    case QSvgNode::LINE:
        writeLine(static_cast<const QSvgLine*>(node));
        break;
    case QSvgNode::PATH:
        writePath(static_cast<const QSvgPath*>(node));
        break;
    case QSvgNode::PATTERN:
        writePattern(static_cast<const QSvgPattern*>(node));
        writeStructure(static_cast<const QSvgStructureNode*>(node));
        break;
    case QSvgNode::POLYGON:
    case QSvgNode::POLYLINE:
        writePoly(node);
        break;
    case QSvgNode::RECT:
        writeRect(static_cast<const QSvgRect*>(node));
        break;
    case QSvgNode::TEXT:
        writeText(static_cast<const QSvgText*>(node));
        break;
    case QSvgNode::USE:
        writeUse(static_cast<const QSvgUse*>(node));
        break;
    default:
        break;
    }

    xmlWriter.writeEndElement();
}

void QSvgImageWriterPrivate::writeStructure(const QSvgStructureNode* structureNode)
{
    for (QSvgNode* node : structureNode->renderers())
        writeSvgNode(node);
}

void QSvgImageWriterPrivate::writeMarker(const QSvgMarker *node)
{
    if (node->viewBox().isValid()) {
        QString viewBox = QString::fromLatin1("%1 %2 %3 %4")
                                  .arg(node->viewBox().x())
                                  .arg(node->viewBox().y())
                                  .arg(node->viewBox().width())
                                  .arg(node->viewBox().height());
        xmlWriter.writeAttribute(QLatin1String("viewBox"), viewBox);
    }

    const QPointF ref = node->ref();
    const QSize mSize = node->size();
    xmlWriter.writeAttribute(QLatin1String("refX"), QString::number(ref.x()));
    xmlWriter.writeAttribute(QLatin1String("refY"), QString::number(ref.y()));
    xmlWriter.writeAttribute(QLatin1String("markerWidth"), QString::number(mSize.width()));
    xmlWriter.writeAttribute(QLatin1String("markerHeight"), QString::number(mSize.height()));

    if (node->unitsMode() == QSvgMarker::userSpaceOnUse)
        xmlWriter.writeAttribute(QLatin1String("markerUnits"), QLatin1String("userSpaceOnUse"));
    else
        xmlWriter.writeAttribute(QLatin1String("markerUnits"), QLatin1String("strokeWidth"));

    if (node->isAutoOrient())
        xmlWriter.writeAttribute(QLatin1String("orient"), QLatin1String("auto"));
    else
        xmlWriter.writeAttribute(QLatin1String("orient"), QString::number(node->orientAngle()));
}

void QSvgImageWriterPrivate::writeClipPath(const QSvgClipPath *node) 
{
    if (node->getCoordinateMode() == QSvgClipPath::objectBoundingBox)
        xmlWriter.writeAttribute(QLatin1String("clipPathUnits"), QLatin1String("objectBoundingBox"));
    else if (node->getCoordinateMode() == QSvgClipPath::userSpaceOnUse)
        xmlWriter.writeAttribute(QLatin1String("clipPathUnits"), QLatin1String("userSpaceOnUse"));
}

void QSvgImageWriterPrivate::writeEllipse(const QSvgEllipse* node)
{
    const QRectF& rectf = node->bounds();
    xmlWriter.writeAttribute(QLatin1String("cx"), QString::number(rectf.x() + rectf.width() / 2));
    xmlWriter.writeAttribute(QLatin1String("cy"), QString::number(rectf.y() + rectf.height() / 2));

    if (node->type() == QSvgNode::CIRCLE)
    {
        xmlWriter.writeAttribute(QLatin1String("r"), QString::number(rectf.width() / 2));
    }
    else
    {
        xmlWriter.writeAttribute(QLatin1String("rx"), QString::number(rectf.width() / 2));
        xmlWriter.writeAttribute(QLatin1String("ry"), QString::number(rectf.height() / 2));
    }
}

static void writeMarkerLink(QXmlStreamWriter &xmlWriter, QSvgMarkerUse markerLink)
{
    if (!markerLink.startId.isEmpty())
        xmlWriter.writeAttribute(QLatin1String("marker-start"), QString("url(#%1)").arg(markerLink.startId));
    if (!markerLink.midId.isEmpty())
        xmlWriter.writeAttribute(QLatin1String("marker-mid"), QString("url(#%1)").arg(markerLink.midId));
    if (!markerLink.endId.isEmpty())
        xmlWriter.writeAttribute(QLatin1String("marker-end"), QString("url(#%1)").arg(markerLink.endId));
}

void QSvgImageWriterPrivate::writeImage(const QSvgImage* node)
{
    const QRectF& rect = node->bounds();
    xmlWriter.writeAttribute(QLatin1String("x"), QString::number(rect.x()));
    xmlWriter.writeAttribute(QLatin1String("y"), QString::number(rect.y()));

    xmlWriter.writeAttribute(QLatin1String("width"), QString::number(rect.width()));
    xmlWriter.writeAttribute(QLatin1String("height"), QString::number(rect.height()));

    {
        const QImage& image = node->image();
        QByteArray data;
        QBuffer buffer(&data);
        buffer.open(QBuffer::ReadWrite);
        image.save(&buffer, "PNG");
        buffer.close();

        QString imgBase64 = QString::fromLatin1("data:image/png;base64,")
                                    .append(QString::fromLatin1(data.toBase64().constData()));
        xmlWriter.writeAttribute(QLatin1String("xlink:href"), imgBase64);
    }
}

void QSvgImageWriterPrivate::writeLine(const QSvgLine* node)
{
    const QLineF& line = node->line();
    xmlWriter.writeAttribute(QLatin1String("x1"), QString::number(line.x1()));
    xmlWriter.writeAttribute(QLatin1String("y1"), QString::number(line.y1()));
    xmlWriter.writeAttribute(QLatin1String("x2"), QString::number(line.x2()));
    xmlWriter.writeAttribute(QLatin1String("y2"), QString::number(line.y2()));

    writeMarkerLink(xmlWriter, node->Marker());
}

void QSvgImageWriterPrivate::writePath(const QSvgPath* node)
{
    xmlWriter.writeAttribute(QLatin1String("d"), translate_path(node->path()));
    writeMarkerLink(xmlWriter, node->Marker());
}

void QSvgImageWriterPrivate::writePattern(const QSvgPattern *node)
{
    if (node->viewBox().isValid()) {
        QString viewBox = QString::fromLatin1("%1 %2 %3 %4")
                                    .arg(node->viewBox().x())
                                    .arg(node->viewBox().y())
                                    .arg(node->viewBox().width())
                                    .arg(node->viewBox().height());
        xmlWriter.writeAttribute(QLatin1String("viewBox"), viewBox);
    }

    if (node->patternUnits() == QSvgPattern::userSpaceOnUse)
        xmlWriter.writeAttribute(QLatin1String("patternUnits"), QLatin1String("userSpaceOnUse"));
    else
        xmlWriter.writeAttribute(QLatin1String("patternUnits"), QLatin1String("objectBoundingBox"));

    const QRectF &rectf = (node->patternUnits() == QSvgPattern::userSpaceOnUse) ? node->bounds() : node->ratioBounds();
    xmlWriter.writeAttribute(QLatin1String("x"), QString::number(rectf.x()));
    xmlWriter.writeAttribute(QLatin1String("y"), QString::number(rectf.y()));
    xmlWriter.writeAttribute(QLatin1String("width"), QString::number(rectf.width()));
    xmlWriter.writeAttribute(QLatin1String("height"), QString::number(rectf.height()));

    if (node->patternContentUnits() == QSvgPattern::userSpaceOnUse)
        xmlWriter.writeAttribute(QLatin1String("patternContentUnits"), QLatin1String("userSpaceOnUse"));
    else
        xmlWriter.writeAttribute(QLatin1String("patternContentUnits"), QLatin1String("objectBoundingBox"));

    if (node->style().transform) {
        const QTransform& trans = node->style().transform->qtransform();
        xmlWriter.writeAttribute(QLatin1String("patternTransform"), translate_matrix(trans.toAffine()));
    }
}

void QSvgImageWriterPrivate::writePoly(const QSvgNode* node)
{
    if (QSvgNode::POLYGON == node->type()) 
    {
        const QSvgPolygon *polygon = static_cast<const QSvgPolygon *>(node);
        writeMarkerLink(xmlWriter, polygon->Marker());

        return writePolyInner(polygon->poly());
    } 
    else if (QSvgNode::POLYLINE == node->type()) 
    {
        const QSvgPolyline *polyline = static_cast<const QSvgPolyline *>(node);
        writeMarkerLink(xmlWriter, polyline->Marker());

        return writePolyInner(polyline->poly());
    }
}

void QSvgImageWriterPrivate::writePolyInner(const QPolygonF & poly)
{
    Q_ASSERT(poly.size() > 0);
    QString strPoints = QString::fromLatin1("%1,%2").arg(poly[0].x()).arg(poly[0].y());
    for (int i = 1; i < poly.size(); ++i)
    {
        strPoints.append(QString::fromLatin1(" %1,%2").arg(poly[i].x()).arg(poly[i].y()));
    }
    xmlWriter.writeAttribute(QLatin1String("points"), strPoints);
}

void QSvgImageWriterPrivate::writeRect(const QSvgRect* node)
{
    const QRectF& rectf = node->rect();
    xmlWriter.writeAttribute(QLatin1String("x"), QString::number(rectf.x()));
    xmlWriter.writeAttribute(QLatin1String("y"), QString::number(rectf.y()));
    xmlWriter.writeAttribute(QLatin1String("width"), QString::number(rectf.width()));
    xmlWriter.writeAttribute(QLatin1String("height"), QString::number(rectf.height()));

    //qt draw rounded rect from 0...99
    //svg from 0...bounds.width()/2 so adjusting the coordinates
    //node->x() == (rx /(width /2)) * 100
    if (node->x() != 0)
    {
        qreal rx = (node->x() / 100.0) * (rectf.width() / 2.0);
        xmlWriter.writeAttribute(QLatin1String("rx"), QString::number(rx));
    }
    if (node->y() != 0)
    {
        qreal ry = (node->y() / 100.0) * (rectf.height() / 2.0);
        xmlWriter.writeAttribute(QLatin1String("ry"), QString::number(ry));
    }
}

void QSvgImageWriterPrivate::writeTspan(const QSvgTspan* node)
{
    xmlWriter.writeStartElement(QLatin1String("tspan"));
    if (!node->nodeId().isEmpty())
        xmlWriter.writeAttribute(QLatin1String("id"), node->nodeId());
    writeNodeCore(node);
    writeNodeStyle(node);

    if (node->whitespaceMode() == QSvgText::Preserve)
        xmlWriter.writeAttribute(QLatin1String("xml:space"), QLatin1String("preserve"));

    if (!node->coordX().empty()) {
        xmlWriter.writeAttribute(QLatin1String("x"), translateCoordList(node->coordX()));
    }
    if (!node->coordY().empty()) {
        xmlWriter.writeAttribute(QLatin1String("y"), translateCoordList(node->coordY()));
    }
    if (!node->offsetX().empty()) {
        xmlWriter.writeAttribute(QLatin1String("dx"), translateCoordList(node->offsetX()));
    }
    if (!node->offsetY().empty()) {
        xmlWriter.writeAttribute(QLatin1String("dy"), translateCoordList(node->offsetY()));
    }

    for (const QSvgTspan *child : node->renderers()) {
        writeTspan(child);
    }

    xmlWriter.writeCharacters(node->text());
    xmlWriter.writeEndElement();
}

void QSvgImageWriterPrivate::writeText(const QSvgText *node)
{
    xmlWriter.writeAttribute(QLatin1String("x"), QString::number(node->coord().x()));
    xmlWriter.writeAttribute(QLatin1String("y"), QString::number(node->coord().y()));

    bool bTextarea = node->type() == QSvgNode::TEXTAREA;
    if (bTextarea)
    {
        xmlWriter.writeAttribute(QLatin1String("width"), QString::number(node->size().width()));
        xmlWriter.writeAttribute(QLatin1String("height"), QString::number(node->size().height()));
    }

    const QVector<QSvgTspan *>& tspans = node->tspans();
    for (int i = 0, cnt = tspans.size(); i < cnt; ++i)
        writeTspan(tspans[i]);
}

void QSvgImageWriterPrivate::writeUse(const QSvgUse* node)
{
    xmlWriter.writeAttribute(QLatin1String("x"), QString::number(node->start().x()));
    xmlWriter.writeAttribute(QLatin1String("y"), QString::number(node->start().y()));

    xmlWriter.writeAttribute(QLatin1String("xlink:href"), 
                             QString::fromLatin1("#%1").arg(node->linkId()));
}

void QSvgImageWriterPrivate::writeNodeCore(const QSvgNode* node)
{
    auto writeCoreAttr = [&](const char* prop, const QStringList& list)
    {
        if (list.empty())
            return;
        xmlWriter.writeAttribute(QString::fromLatin1(prop), list.join(QString::fromLatin1(",")));
    };

    writeCoreAttr("requiredFeatures", node->requiredFeatures());
    writeCoreAttr("requiredExtensions", node->requiredExtensions());
    writeCoreAttr("systemLanguage", node->requiredLanguages());
    writeCoreAttr("requiredFormats", node->requiredFormats());
    writeCoreAttr("requiredFonts", node->requiredFonts());
    if (!node->xmlClass().isEmpty())
        xmlWriter.writeAttribute(QLatin1String("class"), node->xmlClass());
}

void QSvgImageWriterPrivate::writeNodeStyle(const QSvgNode* node)
{
    QSvgStyleWriter styleWriter(&xmlWriter, node);
    styleWriter.write();
}

void QSvgImageWriterPrivate::writeDefs(const QSvgTinyDocument* doc)
{
    xmlWriter.writeStartElement(QLatin1String("defs"));

    typedef QHash<QString, QSvgRefCounter<QSvgFillStyleProperty>> Fills;
    const Fills& fills = doc->namedStyles();
    Fills::const_iterator iter = fills.begin();
    Fills::const_iterator iterEnd = fills.end();
    for (; iter != iterEnd; ++iter)
    {
        QSvgFillStyleProperty* prop = iter.value();
        if (prop->type() == QSvgStyleProperty::SOLID_COLOR)
            writeSolidColor(&xmlWriter, iter.key(), static_cast<const QSvgSolidColorStyle*>(prop));
        else if (prop->type() == QSvgStyleProperty::GRADIENT)
            writeGradient(&xmlWriter, iter.key(), static_cast<const QSvgGradientStyle*>(prop));
    }

    xmlWriter.writeEndElement();
}

void QSvgImageWriterPrivate::writeSvgFont(const QSvgTinyDocument* doc)
{
    typedef QHash<QString, QSvgRefCounter<QSvgFont>> Fonts;
    const Fonts& fonts = doc->namedFonts();
    Fonts::const_iterator iter = fonts.begin();
    Fonts::const_iterator iterEnd = fonts.end();
    for (; iter != iterEnd; ++iter)
    {
        QSvgFont* prop = iter.value();
        xmlWriter.writeStartElement(QLatin1String("font"));
        xmlWriter.writeAttribute(QLatin1String("horiz-adv-x"), QString::number(prop->m_horizAdvX));
        {
            //font-face
            xmlWriter.writeStartElement(QLatin1String("font-face"));
            xmlWriter.writeAttribute(QLatin1String("font-family"), prop->familyName());
            xmlWriter.writeAttribute(QLatin1String("units-per-em"), QString::number(prop->m_unitsPerEm));
            xmlWriter.writeEndElement();
        }
        {
            //glyph and missing-glyph
            const QHash<QChar, QSvgGlyph>& glyphs = prop->m_glyphs;
            QHash<QChar, QSvgGlyph>::const_iterator it = glyphs.begin();
            QHash<QChar, QSvgGlyph>::const_iterator itEnd = glyphs.end();
            for (; it != itEnd; ++it)
            {
                const QSvgGlyph& glyph = it.value();
                if (glyph.m_unicode.unicode() == 0)
                {
                    xmlWriter.writeStartElement(QLatin1String("missing-glyph"));
                    xmlWriter.writeAttribute(QLatin1String("horiz-adv-x"), QString::number(glyph.m_horizAdvX));
                    xmlWriter.writeAttribute(QLatin1String("d"), translate_path(glyph.m_path));
                    xmlWriter.writeEndElement();
                }
                else
                {
                    xmlWriter.writeStartElement(QLatin1String("glyph"));
                    xmlWriter.writeAttribute(QLatin1String("unicode"), QString(glyph.m_unicode));
                    xmlWriter.writeAttribute(QLatin1String("horiz-adv-x"), QString::number(glyph.m_horizAdvX));
                    xmlWriter.writeAttribute(QLatin1String("d"), translate_path(glyph.m_path));
                    xmlWriter.writeEndElement();
                }
            }
        }
        xmlWriter.writeEndElement();
    }
}

void QSvgImageWriterPrivate::writeViewBox(const QSvgTinyDocument* doc)
{
    if (doc->viewBox().isValid())
    {
        QString viewBox = QString::fromLatin1("%1 %2 %3 %4")
            .arg(doc->viewBox().x()).arg(doc->viewBox().y())
            .arg(doc->viewBox().width()).arg(doc->viewBox().height());
        xmlWriter.writeAttribute(QLatin1String("viewBox"), viewBox);
    }
}

///////////////////////////////////////////////////////////////////////////////
QSvgImageWriter::QSvgImageWriter(const QSvgTinyDocument* doc, QIODevice* device)
    : d(new QSvgImageWriterPrivate(doc, device))
{
}

QSvgImageWriter::QSvgImageWriter(const QSvgTinyDocument* doc, QByteArray* arr)
    : d(new QSvgImageWriterPrivate(doc, arr))
{
}

QSvgImageWriter::~QSvgImageWriter()
{
    device()->close();
}

QIODevice* QSvgImageWriter::device() const
{
    return d->xmlWriter.device();
}

void QSvgImageWriter::write()
{
    d->write();
}

QT_END_NAMESPACE

#endif // QT_NO_SVG
