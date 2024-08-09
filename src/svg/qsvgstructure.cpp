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

#include "qsvgstructure_p.h"

#include "qsvgnode_p.h"
#include "qsvgstyle_p.h"
#include "qsvgtinydocument_p.h"
#include "qsvggraphics_p.h"

#include "qpainter.h"
#include "qlocale.h"
#include "qdebug.h"

#include <qscopedvaluerollback.h>

QT_BEGIN_NAMESPACE

QSvgG::QSvgG(QSvgNode *parent) : QSvgStructureNode(parent) {}

QSvgStructureNode::~QSvgStructureNode()
{
    qDeleteAll(m_renderers);
}

void QSvgG::draw(QPainter *p, QSvgExtraStates &states)
{
    QList<QSvgNode*>::iterator itr = m_renderers.begin();
    applyStyle(p, states);

    while (itr != m_renderers.end()) {
        QSvgNode *node = *itr;
        if ((node->isVisible()) && (node->displayMode() != QSvgNode::NoneMode))
            node->draw(p, states);
        ++itr;
    }
    revertStyle(p, states);
}

QSvgNode *QSvgG::clone(QSvgNode *parent)
{
    QSvgNode *newNode = new QSvgG(*this);
    newNode->setParent(parent);
    return newNode;
}

QSvgNode::Type QSvgG::type() const
{
    return G;
}

QSvgStructureNode::QSvgStructureNode(QSvgNode *parent) : QSvgNode(parent) {}

QSvgStructureNode::QSvgStructureNode(const QSvgStructureNode &other) : QSvgNode(other)
{
    // m_renderers
    m_renderers.reserve(other.m_renderers.size());
    for (QSvgNode *node : other.m_renderers)
        m_renderers.append((node)->clone(this));

    // m_scope
    Q_ASSERT(m_scope.isEmpty());
    // m_linkedScopes
    Q_ASSERT(m_linkedScopes.isEmpty());
}

QSvgNode * QSvgStructureNode::scopeNode(const QString &id) const
{
    QSvgTinyDocument *doc = document();
    return doc ? doc->namedNode(id) : 0;
}

void QSvgStructureNode::addChild(QSvgNode *child, const QString &id)
{
    m_renderers.append(child);

    if (id.isEmpty())
        return; //we can't add it to scope without id

    QSvgTinyDocument *doc = document();
    if (doc)
        doc->addNamedNode(id, child);
}

QSvgDefs::QSvgDefs(QSvgNode *parent) : QSvgStructureNode(parent) {}

void QSvgDefs::draw(QPainter *, QSvgExtraStates &)
{
    // noop
}

QSvgNode *QSvgDefs::clone(QSvgNode *parent)
{
    QSvgDefs *newNode = new QSvgDefs(*this);
    newNode->setParent(parent);
    return newNode;
}

QSvgNode::Type QSvgDefs::type() const
{
    return DEFS;
}

/*
  Below is a lookup function based on the gperf output using the following set:

  http://www.w3.org/Graphics/SVG/feature/1.2/#SVG
  http://www.w3.org/Graphics/SVG/feature/1.2/#SVG-static
  http://www.w3.org/Graphics/SVG/feature/1.2/#CoreAttribute
  http://www.w3.org/Graphics/SVG/feature/1.2/#Structure
  http://www.w3.org/Graphics/SVG/feature/1.2/#ConditionalProcessing
  http://www.w3.org/Graphics/SVG/feature/1.2/#ConditionalProcessingAttribute
  http://www.w3.org/Graphics/SVG/feature/1.2/#Image
  http://www.w3.org/Graphics/SVG/feature/1.2/#Prefetch
  http://www.w3.org/Graphics/SVG/feature/1.2/#Shape
  http://www.w3.org/Graphics/SVG/feature/1.2/#Text
  http://www.w3.org/Graphics/SVG/feature/1.2/#PaintAttribute
  http://www.w3.org/Graphics/SVG/feature/1.2/#OpacityAttribute
  http://www.w3.org/Graphics/SVG/feature/1.2/#GraphicsAttribute
  http://www.w3.org/Graphics/SVG/feature/1.2/#Gradient
  http://www.w3.org/Graphics/SVG/feature/1.2/#SolidColor
  http://www.w3.org/Graphics/SVG/feature/1.2/#XlinkAttribute
  http://www.w3.org/Graphics/SVG/feature/1.2/#ExternalResourcesRequiredAttribute
  http://www.w3.org/Graphics/SVG/feature/1.2/#Font
  http://www.w3.org/Graphics/SVG/feature/1.2/#Hyperlinking
  http://www.w3.org/Graphics/SVG/feature/1.2/#Extensibility
*/

// ----- begin of generated code -----

/* C code produced by gperf version 3.0.2 */
/* Command-line: gperf -c -L c svg  */
/* Computed positions: -k'45-46' */

#if !((' ' == 32) && ('!' == 33) && ('"' == 34) && ('#' == 35) \
      && ('%' == 37) && ('&' == 38) && ('\'' == 39) && ('(' == 40) \
      && (')' == 41) && ('*' == 42) && ('+' == 43) && (',' == 44) \
      && ('-' == 45) && ('.' == 46) && ('/' == 47) && ('0' == 48) \
      && ('1' == 49) && ('2' == 50) && ('3' == 51) && ('4' == 52) \
      && ('5' == 53) && ('6' == 54) && ('7' == 55) && ('8' == 56) \
      && ('9' == 57) && (':' == 58) && (';' == 59) && ('<' == 60) \
      && ('=' == 61) && ('>' == 62) && ('?' == 63) && ('A' == 65) \
      && ('B' == 66) && ('C' == 67) && ('D' == 68) && ('E' == 69) \
      && ('F' == 70) && ('G' == 71) && ('H' == 72) && ('I' == 73) \
      && ('J' == 74) && ('K' == 75) && ('L' == 76) && ('M' == 77) \
      && ('N' == 78) && ('O' == 79) && ('P' == 80) && ('Q' == 81) \
      && ('R' == 82) && ('S' == 83) && ('T' == 84) && ('U' == 85) \
      && ('V' == 86) && ('W' == 87) && ('X' == 88) && ('Y' == 89) \
      && ('Z' == 90) && ('[' == 91) && ('\\' == 92) && (']' == 93) \
      && ('^' == 94) && ('_' == 95) && ('a' == 97) && ('b' == 98) \
      && ('c' == 99) && ('d' == 100) && ('e' == 101) && ('f' == 102) \
      && ('g' == 103) && ('h' == 104) && ('i' == 105) && ('j' == 106) \
      && ('k' == 107) && ('l' == 108) && ('m' == 109) && ('n' == 110) \
      && ('o' == 111) && ('p' == 112) && ('q' == 113) && ('r' == 114) \
      && ('s' == 115) && ('t' == 116) && ('u' == 117) && ('v' == 118) \
      && ('w' == 119) && ('x' == 120) && ('y' == 121) && ('z' == 122) \
      && ('{' == 123) && ('|' == 124) && ('}' == 125) && ('~' == 126))
/* The character set is not based on ISO-646.  */
#error "gperf generated tables don't work with this execution character set. Please report a bug to <bug-gnu-gperf@gnu.org>."
#endif

enum {
    TOTAL_KEYWORDS = 20,
    MIN_WORD_LENGTH = 47,
    MAX_WORD_LENGTH = 78,
    MIN_HASH_VALUE = 48,
    MAX_HASH_VALUE = 88
};
/* maximum key range = 41, duplicates = 0 */

inline static bool isSupportedSvgFeature(const QString &str)
{
    static const unsigned char asso_values[] = {
        89, 89, 89, 89, 89, 89, 89, 89, 89, 89,
        89, 89, 89, 89, 89, 89, 89, 89, 89, 89,
        89, 89, 89, 89, 89, 89, 89, 89, 89, 89,
        89, 89, 89, 89, 89, 89, 89, 89, 89, 89,
        89, 89, 89, 89, 89, 89, 89, 89, 89, 89,
        89, 89, 89, 89, 89, 89, 89, 89, 89, 89,
        89, 89, 89, 89, 89, 89, 89,  0, 89,  5,
        15,  5,  0, 10, 89, 89, 89, 89, 89,  0,
        15, 89, 89,  0,  0, 89,  5, 89,  0, 89,
        89, 89, 89, 89, 89, 89, 89,  0, 89, 89,
        89,  0, 89, 89,  0, 89, 89, 89,  0,  5,
        89,  0,  0, 89,  5, 89,  0, 89, 89, 89,
        5,  0, 89, 89, 89, 89, 89, 89, 89, 89,
        89, 89, 89, 89, 89, 89, 89, 89, 89, 89,
        89, 89, 89, 89, 89, 89, 89, 89, 89, 89,
        89, 89, 89, 89, 89, 89, 89, 89, 89, 89,
        89, 89, 89, 89, 89, 89, 89, 89, 89, 89,
        89, 89, 89, 89, 89, 89, 89, 89, 89, 89,
        89, 89, 89, 89, 89, 89, 89, 89, 89, 89,
        89, 89, 89, 89, 89, 89, 89, 89, 89, 89,
        89, 89, 89, 89, 89, 89, 89, 89, 89, 89,
        89, 89, 89, 89, 89, 89, 89, 89, 89, 89,
        89, 89, 89, 89, 89, 89, 89, 89, 89, 89,
        89, 89, 89, 89, 89, 89, 89, 89, 89, 89,
        89, 89, 89, 89, 89, 89, 89, 89, 89, 89,
        89, 89, 89, 89, 89, 89
    };

    static const char * wordlist[] = {
        "", "", "", "", "", "", "", "", "",
        "", "", "", "", "", "", "", "", "",
        "", "", "", "", "", "", "", "", "",
        "", "", "", "", "", "", "", "", "",
        "", "", "", "", "", "", "", "", "",
        "", "", "",
        "http://www.w3.org/Graphics/SVG/feature/1.2/#Text",
        "http://www.w3.org/Graphics/SVG/feature/1.2/#Shape",
        "", "",
        "http://www.w3.org/Graphics/SVG/feature/1.2/#SVG",
        "http://www.w3.org/Graphics/SVG/feature/1.2/#Structure",
        "http://www.w3.org/Graphics/SVG/feature/1.2/#SolidColor",
        "",
        "http://www.w3.org/Graphics/SVG/feature/1.2/#Hyperlinking",
        "http://www.w3.org/Graphics/SVG/feature/1.2/#CoreAttribute",
        "http://www.w3.org/Graphics/SVG/feature/1.2/#XlinkAttribute",
        "http://www.w3.org/Graphics/SVG/feature/1.2/#SVG-static",
        "http://www.w3.org/Graphics/SVG/feature/1.2/#OpacityAttribute",
        "",
        "http://www.w3.org/Graphics/SVG/feature/1.2/#Gradient",
        "http://www.w3.org/Graphics/SVG/feature/1.2/#Font",
        "http://www.w3.org/Graphics/SVG/feature/1.2/#Image",
        "http://www.w3.org/Graphics/SVG/feature/1.2/#ConditionalProcessing",
        "",
        "http://www.w3.org/Graphics/SVG/feature/1.2/#Extensibility",
        "", "", "",
        "http://www.w3.org/Graphics/SVG/feature/1.2/#GraphicsAttribute",
        "http://www.w3.org/Graphics/SVG/feature/1.2/#Prefetch",
        "http://www.w3.org/Graphics/SVG/feature/1.2/#PaintAttribute",
        "http://www.w3.org/Graphics/SVG/feature/1.2/#ConditionalProcessingAttribute",
        "", "", "", "", "", "", "", "", "",
        "", "", "", "",
        "http://www.w3.org/Graphics/SVG/feature/1.2/#ExternalResourcesRequiredAttribute"
    };

    if (str.length() <= MAX_WORD_LENGTH && str.length() >= MIN_WORD_LENGTH) {
        const int key = str.length()
                        + asso_values[str.at(45).unicode()]
                        + asso_values[str.at(44).unicode()];
        if (key <= MAX_HASH_VALUE && key >= 0)
            return str == QLatin1String(wordlist[key]);
    }
    return false;
}

// ----- end of generated code -----

static inline bool isSupportedSvgExtension(const QString &)
{
    return false;
}

QSvgSwitch::QSvgSwitch(QSvgNode *parent) : QSvgStructureNode(parent)
{
    init();
}

QSvgSwitch::QSvgSwitch(const QSvgSwitch &other) : QSvgStructureNode(other)
{
    init();
}

void QSvgSwitch::draw(QPainter *p, QSvgExtraStates &states)
{
    QList<QSvgNode*>::iterator itr = m_renderers.begin();
    applyStyle(p, states);

    while (itr != m_renderers.end()) {
        QSvgNode *node = *itr;
        if (node->isVisible() && (node->displayMode() != QSvgNode::NoneMode)) {
            const QStringList &features  = node->requiredFeatures();
            const QStringList &extensions = node->requiredExtensions();
            const QStringList &languages = node->requiredLanguages();
            const QStringList &formats = node->requiredFormats();
            const QStringList &fonts = node->requiredFonts();

            bool okToRender = true;
            if (!features.isEmpty()) {
                QStringList::const_iterator sitr = features.constBegin();
                for (; sitr != features.constEnd(); ++sitr) {
                    if (!isSupportedSvgFeature(*sitr)) {
                        okToRender = false;
                        break;
                    }
                }
            }

            if (okToRender && !extensions.isEmpty()) {
                QStringList::const_iterator sitr = extensions.constBegin();
                for (; sitr != extensions.constEnd(); ++sitr) {
                    if (!isSupportedSvgExtension(*sitr)) {
                        okToRender = false;
                        break;
                    }
                }
            }

            if (okToRender && !languages.isEmpty()) {
                QStringList::const_iterator sitr = languages.constBegin();
                okToRender = false;
                for (; sitr != languages.constEnd(); ++sitr) {
                    if ((*sitr).startsWith(m_systemLanguagePrefix)) {
                        okToRender = true;
                        break;
                    }
                }
            }

            if (okToRender && !formats.isEmpty()) {
                okToRender = false;
            }

            if (okToRender && !fonts.isEmpty()) {
                okToRender = false;
            }

            if (okToRender) {
                node->draw(p, states);
                break;
            }
        }
        ++itr;
    }
    revertStyle(p, states);
}

QSvgNode *QSvgSwitch::clone(QSvgNode *parent)
{
    QSvgSwitch *newNode = new QSvgSwitch(*this);
    newNode->setParent(parent);
    return newNode;
}

QSvgNode::Type QSvgSwitch::type() const
{
    return SWITCH;
}

void QSvgSwitch::init()
{
    QLocale locale;
    m_systemLanguage = locale.name().replace(QLatin1Char('_'), QLatin1Char('-'));
    int idx = m_systemLanguage.indexOf(QLatin1Char('-'));
    m_systemLanguagePrefix = m_systemLanguage.mid(0, idx);
}

QRectF QSvgStructureNode::bounds(QPainter *p, QSvgExtraStates &states, bool defaultViewCoord) const
{
    QRectF bounds;
    if (!m_recursing) {
        QScopedValueRollback<bool> guard(m_recursing, true);
        for (QSvgNode *node : qAsConst(m_renderers))
            bounds |= node->transformedBounds(p, states, defaultViewCoord);
    }
    return bounds;
}

QSvgNode * QSvgStructureNode::previousSiblingNode(QSvgNode *n) const
{
    QSvgNode *prev = 0;
    QList<QSvgNode*>::const_iterator itr = m_renderers.constBegin();
    for (; itr != m_renderers.constEnd(); ++itr) {
        QSvgNode *node = *itr;
        if (node == n)
            return prev;
        prev = node;
    }
    return prev;
}

QSvgMarker::QSvgMarker(QSvgNode *parent) 
    : QSvgStructureNode(parent), 
      m_unitsMode(strokeWidth), 
      m_bAutoOrient(false), 
      m_orientAngle(0.0)
{
}

void QSvgMarker::draw(QPainter *, QSvgExtraStates &) {}

void QSvgMarker::draw(QPainter *p, QSvgExtraStates &states, const QPointF& point, qreal angle,
                      qreal strokeWidth)
{
    if (MarkerUnits::userSpaceOnUse == unitsMode())
        strokeWidth = 1.0;
    if (!isAutoOrient())
        angle = m_orientAngle;

    p->save();

    qreal scale = 1.0;
    if (m_viewBox.height() && m_viewBox.width())
        scale = qMin(m_size.height() / m_viewBox.height(), m_size.width() / m_viewBox.width());
   
    p->translate(point);
    p->rotate(angle);
    p->scale(strokeWidth, strokeWidth);
    p->translate(-m_ref.x() * scale, -m_ref.y() * scale);
    p->scale(scale, scale);
    if (m_viewBox.isValid())
        p->setClipRect(m_viewBox, Qt::IntersectClip);

    auto itr = m_renderers.cbegin();
    applyStyle(p, states);
    while (itr != m_renderers.cend()) {
        QSvgNode *node = *itr;
        if ((node->isVisible()) && (QSvgNode::NoneMode != node->displayMode()))
            node->draw(p, states);
        ++itr;
    }
    revertStyle(p, states);
    p->restore();
}

QSvgNode *QSvgMarker::clone(QSvgNode *parent)
{
    QSvgNode *newNode = new QSvgMarker(*this);
    newNode->setParent(parent);
    return newNode;
}

QSvgNode::Type QSvgMarker::type() const
{
    return QSvgNode::MARKER;
}

bool QSvgMarker::viewBoxValid() const
{
    return m_viewBox.isValid();
}

const QRectF& QSvgMarker::viewBox() const
{
    if (m_viewBox.isNull())
        m_viewBox = transformedBounds();

    return m_viewBox;
}

void QSvgMarker::setOrientAngle(qreal angle)
{
    Q_ASSERT(!m_bAutoOrient);
    m_orientAngle = angle;
}

QSvgPattern::QSvgPattern(QSvgNode *parent, const QRectF &bounds, PatternUnits units, PatternUnits contentUnits)
    : QSvgStructureNode(parent)
    , m_bounds(bounds)
    , m_ratioBounds(bounds)
    , m_viewBox(QRect())
    , m_clipPath(QPainterPath())
    , m_patternUnits(units)
    , m_patternContentUnits(contentUnits)
{

}

void QSvgPattern::draw(QPainter *, QSvgExtraStates &)
{
    // noop
}

QRectF QSvgPattern::rect4DrawTile(qreal fPatternX, qreal fPatternY)
{
    qreal fSvgWidth = static_cast<qreal>(document()->width());
    qreal fSvgHeight = static_cast<qreal>(document()->height());
    QRectF rect(0.0, 0.0, fSvgWidth, fSvgHeight);

    qreal fXPatternInSvg = fPatternX, fYPatternInSvg = fPatternY;
    if (m_style.transform) {
        fXPatternInSvg += m_style.transform->qtransform().dx();
        fYPatternInSvg += m_style.transform->qtransform().dy();
    }

    qreal fExtendWidth = 0.0;
    while (qAbs(fXPatternInSvg) > fExtendWidth)
        fExtendWidth += fSvgWidth;
    if (!qFuzzyIsNull(fExtendWidth)) {
        rect.setX(-(fExtendWidth));
        rect.setWidth(fExtendWidth + rect.width());
    }

    qreal fExtendHeight = 0.0;
    while (qAbs(fYPatternInSvg) > fExtendHeight)
        fExtendHeight += fSvgHeight;
    if (!qFuzzyIsNull(fExtendHeight)) {
        rect.setY(-(fExtendHeight));
        rect.setHeight(fExtendHeight + rect.height());
    }

    if (m_style.transform) {
        QTransform copy = m_style.transform->qtransform();
        qreal fTangent = 0.0;
        if (!qFuzzyIsNull(copy.m22())) {//skewX
            fTangent = copy.m21() / copy.m22();//m21 = m22*tan(degrees)
            fExtendWidth = m_bounds.height() * fTangent;
            rect.setX(rect.x() - qAbs(fExtendWidth));
            rect.setWidth(rect.width() + qAbs(fExtendWidth));
        }

        fTangent = 0.0;
        if (!qFuzzyIsNull(copy.m11())) {//skewY
            fTangent = copy.m12() / copy.m11();//m12 = m11*tan(degrees)
            fExtendHeight = m_bounds.width() * fTangent;
            rect.setY(rect.y() - qAbs(fExtendHeight));
            rect.setHeight(rect.height() + qAbs(fExtendHeight));
        }

        qreal fScale = 0.0;
        qreal m11Abs = qAbs(copy.m11());
        if ((m11Abs < 1) && (m11Abs > 0) && (!qFuzzyIsNull(m11Abs))) {
            fScale = 1.0 / m11Abs;
            qreal fOldWidth = rect.width();
            rect.setX((rect.x() - fSvgWidth / 2) * fScale + fSvgWidth / 2);
            rect.setWidth(fOldWidth * fScale);
        }

        qreal m22Abs = qAbs(copy.m22());
        if ((m22Abs < 1) && (m22Abs > 0) && (!qFuzzyIsNull(m22Abs))) {
            fScale = 1.0 / m22Abs;
            qreal fOldHeight = rect.height();
            rect.setY((rect.y() - fSvgHeight / 2) * fScale + fSvgHeight / 2);
            rect.setHeight(fOldHeight * fScale);
        }
    }

    return rect;
}

QPixmap QSvgPattern::patternContentPixmap(QPainter *p, QSvgExtraStates &states)
{
    QPixmap pixmap;
    if (auto tinydoc = document())
        pixmap = tinydoc->createPixmapBuffer(p, m_bounds.width(), m_bounds.height());
    else
        pixmap = QPixmap(m_bounds.width(), m_bounds.height());
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setBrush(QBrush(Qt::black));
    auto itr = m_renderers.cbegin();
    while (itr != m_renderers.cend()) {
        QSvgNode *node = *itr;
        if ((node->isVisible()) && (node->displayMode() != QSvgNode::NoneMode)) {
            if (m_patternContentUnits == objectBoundingBox)
                node->setTargetBounds(m_targetBounds);
            node->draw(&painter, states);
        }
        ++itr;
    }
    painter.end();
    return pixmap;
}

void QSvgPattern::drawTile(QPainter *p, QSvgExtraStates &states)
{
    if (objectBoundingBox == m_patternUnits) {// calculate pattern real bounds
        qreal fx = m_ratioBounds.x() * m_targetBounds.width();
        qreal fy = m_ratioBounds.y() * m_targetBounds.height();
        qreal fw = m_ratioBounds.width() * m_targetBounds.width();
        qreal fh = m_ratioBounds.height() * m_targetBounds.height();
        m_bounds.setRect(fx, fy, fw, fh);
    }

    applyStyle(p, states);
    p->setRenderHint(QPainter::SmoothPixmapTransform, false);
    p->setRenderHint(QPainter::HighQualityPixmapTransform, false);
    p->setClipping(true);
    if (!m_clipPath.isEmpty()) {
        if (m_style.transform)
            p->setClipPath(m_style.transform->qtransform().inverted().map(m_clipPath));
        else
            p->setClipPath(m_clipPath);
    }

    qreal fXPatternInSvg = m_bounds.x(), fYPatternInSvg = m_bounds.y();
    if (objectBoundingBox == m_patternUnits) {
        fXPatternInSvg += m_targetBounds.x();
        fYPatternInSvg += m_targetBounds.y();
    }

    QRectF rect = rect4DrawTile(fXPatternInSvg, fYPatternInSvg);

    if (fXPatternInSvg || fYPatternInSvg)
        p->translate(fXPatternInSvg, fYPatternInSvg);

    QPixmap pixmap = patternContentPixmap(p, states);
    p->drawTiledPixmap(rect, pixmap, QPointF(rect.x(),rect.y()));

    p->setClipping(false);
    if (fXPatternInSvg || fYPatternInSvg)
        p->translate(-fXPatternInSvg, -fYPatternInSvg);
    revertStyle(p, states);
}

void QSvgPattern::setClipPath(const QPainterPath &path)
{
    m_clipPath = path;
}

QSvgNode *QSvgPattern::clone(QSvgNode *parent)
{
    QSvgPattern *newNode = new QSvgPattern(*this);
    newNode->setParent(parent);
    return newNode;
}

QSvgNode::Type QSvgPattern::type() const
{
    return PATTERN;
}

QSvgClipPath::QSvgClipPath(QSvgNode *parent) 
    : QSvgStructureNode(parent), 
      m_coordinateMode(userSpaceOnUse), 
      m_bParsed(false)
{
}

void QSvgClipPath::draw(QPainter *p, QSvgExtraStates &states) {}

QSvgNode *QSvgClipPath::clone(QSvgNode *parent)
{
    QSvgNode *newNode = new QSvgClipPath(*this);
    newNode->setParent(parent);
    return newNode;
}

QSvgNode::Type QSvgClipPath::type() const
{
    return QSvgNode::CLIPPATH;
}

static void addTextPath(QSvgText *pText, QPainterPath &clipPath)
{
    for (const QSvgTspan *tspan : pText->tspans()) {
        const QSvgText *toParent = static_cast<const QSvgText *>(tspan->parent());
        QSvgFontStyle *pFontStyle =
                static_cast<QSvgFontStyle *>(tspan->styleProperty(QSvgStyleProperty::FONT));
        if (!pFontStyle)
            continue;

        QFont font = pFontStyle->qfont();
        font.setPixelSize(font.pointSizeF());
        font.setPointSizeF(-1);
        clipPath.addText(toParent->coord(), font, tspan->text());
    }
}

void QSvgClipPath::parseClipPathList() 
{
    m_pathList.clear();
    QSvgTransformStyle *pTransformStyle =
            static_cast<QSvgTransformStyle *>(styleProperty(QSvgStyleProperty::TRANSFORM));
    QSvgClipPathStyle *curClipStyle =
            static_cast<QSvgClipPathStyle *>(styleProperty(QSvgStyleProperty::CLIPPATH));

    auto parseNodePath = [&](QSvgNode *node, QPainterPath& path) {
        if (!(node->isVisible()) || QSvgNode::NoneMode == node->displayMode())
            return;

        QPainterPath nodePath;
        switch (node->type()) {
        case QSvgNode::RECT: {
            QSvgRect *pRect = static_cast<QSvgRect*>(node);
            nodePath.addRect(pRect->rect());
        } break;
        case QSvgNode::ELLIPSE:
        case QSvgNode::CIRCLE: {
            QSvgEllipse *pEllipse = static_cast<QSvgEllipse*>(node);
            nodePath.addEllipse(pEllipse->bounds());
        } break;
        case QSvgNode::PATH: {
            QSvgPath *pPath = static_cast<QSvgPath*>(node);
            nodePath.addPath(pPath->path());
        } break;
        case QSvgNode::POLYGON: {
            QSvgPolygon *pPolygon = static_cast<QSvgPolygon*>(node);
            nodePath.addPolygon(pPolygon->poly());
        } break;
        case QSvgNode::TEXT: {
            QSvgText *pText = static_cast<QSvgText *>(node);
            addTextPath(pText, nodePath);
        } break;
        default:
            return;
        break;
        };

        QSvgClipPathStyle *clipStyle =
                static_cast<QSvgClipPathStyle *>(node->styleProperty(QSvgStyleProperty::CLIPPATH));

        if (clipStyle && clipStyle->getClipNode()) {
            clipStyle->initCurrePath(node->transformedBounds());
            nodePath &= clipStyle->getCurrePath();
        }
        if (curClipStyle && curClipStyle->getClipNode()) {
            curClipStyle->initCurrePath(transformedBounds());
            nodePath &= curClipStyle->getCurrePath();
        }
        if (pTransformStyle)
            nodePath = pTransformStyle->qtransform().map(nodePath);
        path = nodePath;
    };

    auto parseUsePath = [&](QSvgUse *use, QPainterPath &path) { 
        QPainterPath tempPath;
        if (nullptr == use || nullptr == use->document())
            return;

        QSvgNode *link = use->document()->namedNode(use->linkId());
        if (nullptr == link)
            return;

        if (QSvgNode::DOC == link->type() 
            || QSvgNode::G == link->type()
            || QSvgNode::SWITCH == link->type()) {
            QSvgStructureNode *group = static_cast<QSvgStructureNode *>(link);
            for (QSvgNode *node : group->renderers()) {
                QPainterPath gPath;
                parseNodePath(node, gPath);
                tempPath |= gPath;
            }
        } else {
            parseNodePath(link, tempPath);
        }

        QTransform trans;
        trans.translate(use->start().x(), use->start().y());
        path = trans.map(tempPath);
    };

    auto itr = m_renderers.cbegin(); 
    while (itr != m_renderers.cend()) {
        QSvgNode* node = *itr;
        QPainterPath curPath;
        if (QSvgNode::USE == node->type()) {
            QSvgUse *use = static_cast<QSvgUse *>(node);
            parseUsePath(use, curPath);
        }
        else{
            parseNodePath(node, curPath);
        }

        QSvgTransformStyle *curTransformStyle =
                static_cast<QSvgTransformStyle *>(node->styleProperty(QSvgStyleProperty::TRANSFORM));
        if (curTransformStyle != pTransformStyle)
            curPath = curTransformStyle->qtransform().map(curPath);

        if (node->isClipRuleSet())
            curPath.setFillRule(node->clipRule());
        else
            curPath.setFillRule(clipRule());

        m_pathList.push_back(curPath);
        ++itr;
    }

    m_bParsed = true;
}

QT_END_NAMESPACE
