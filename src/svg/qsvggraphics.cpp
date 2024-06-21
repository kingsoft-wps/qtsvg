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

#include "qsvggraphics_p.h"
#include "qsvgtinydocument_p.h"

#include "qsvgfont_p.h"

#include <qabstracttextdocumentlayout.h>
#include <qdebug.h>
#include <qpainter.h>
#include <qscopedvaluerollback.h>
#include <qtextcursor.h>
#include <qtextdocument.h>

#include <math.h>
#include <limits.h>

QT_BEGIN_NAMESPACE

#define QT_SVG_DRAW_SHAPE(command)                          \
    qreal oldOpacity = p->opacity();                        \
    QBrush oldBrush = p->brush();                           \
    QPen oldPen = p->pen();                                 \
    p->setPen(Qt::NoPen);                                   \
    p->setOpacity(oldOpacity * states.fillOpacity);         \
    command;                                                \
    p->setPen(oldPen);                                      \
    if (oldPen != Qt::NoPen && oldPen.brush() != Qt::NoBrush && oldPen.widthF() != 0) { \
        p->setOpacity(oldOpacity * states.strokeOpacity);   \
        p->setBrush(Qt::NoBrush);                           \
        command;                                            \
        p->setBrush(oldBrush);                              \
    }                                                       \
    p->setOpacity(oldOpacity);


static QSvgMarker *getMarker(const QString& id, QSvgNode *node)
{
    if (id.isEmpty() || nullptr == node || QSvgNode::MARKER != node->type())
        return nullptr;

    return static_cast<QSvgMarker *>(node);
}

static bool isParentStyle(QSvgNode *node, const QString& id)
{
    QSvgNode *parent = node->parent();
    if (!parent)
        return false;
    if (id == parent->nodeId())
        return true;
    return isParentStyle(parent, id);
}

static void parseMarkerId(QSvgNode* node, QSvgMarkerUse &markerLink,
                          const QString& sId, const QString& mId, const QString& eId)
{
    if (!isParentStyle(node, sId))
        markerLink.startId = sId;
    if (!isParentStyle(node, mId))
        markerLink.midId = mId;
    if (!isParentStyle(node, eId))
        markerLink.endId = eId;
}

static void parseMarkerUse(QSvgMarkerUse &markerLink, const QSvgTinyDocument *doc)
{
    if (nullptr != doc) {
        markerLink.start = getMarker(markerLink.startId, doc->namedNode(markerLink.startId));
        markerLink.mid = getMarker(markerLink.midId, doc->namedNode(markerLink.midId));
        markerLink.end = getMarker(markerLink.endId, doc->namedNode(markerLink.endId));
    }
}

static qreal getRotateAngle(const QPointF& lPoint, const QPointF& rPoint, const QPointF& point)
{
    if (rPoint == point && lPoint == point) {
        QLineF line(QPointF(0, 0), point);
        return 360.0 - line.angle();
    }
    if (lPoint == point) {
        QLineF line(point, rPoint);
        return 360.0 - line.angle();
    }
    if (rPoint == point) {
        QLineF line(lPoint, point);
        return 360.0 - line.angle();
    }

    QLineF line2(rPoint, point);
    QLineF line1(lPoint, point);
    qreal angle = line1.angleTo(line2);
    QLineF line = QLineF::fromPolar(100, line1.angle() + angle / 2);
    QLineF vertical = QLineF::fromPolar(100.0, 90.0);

    return 360.0 - vertical.angleTo(line);
}

static void parsePolyData(QSvgNode::Type type, const QPolygonF &src, QSvgApexList &target)
{
    if (src.size() < 2)
        return;

    const bool bClose = QSvgNode::POLYGON == type || src.first() == src.last();
    for (auto itr = src.cbegin(); itr != src.cend(); ++itr) {
        QPointF left, right, point;

        if (itr == src.cbegin()) {
            left = bClose ? src.back() : *(itr);
            point = *itr;
            right = *(itr + 1);
        } else if (itr == src.cend() - 1) {
            left = *(itr - 1);
            point = *itr;
            right = bClose ? src.front() : *(itr);
        } else {
            left = *(itr - 1);
            point = *itr;
            right = *(itr + 1);
        }

        target.push_back(qMakePair(point, getRotateAngle(left, right, point)));
    }

    if (QSvgNode::POLYGON == type)
        target.push_back(target.first());
}

static void parsePathData(const QPainterPath &path, QSvgApexList &target)
{
    QPainterPath::Element startP;
    int estart = 0, tstart = 0;
    const int cnt = path.elementCount();
    bool bSubClose = false;

    auto parseClosePath = [&](int idx, QPainterPath::Element &l, QPainterPath::Element &r) {
        if (idx < 1 || estart > cnt - 2)
            return;
        bSubClose = true;
        l = path.elementAt(idx - 1);
        r = path.elementAt(estart + 1);
    };

    auto parseMoveElement = [&](int idx, QPainterPath::Element &l,
                                QPainterPath::Element &c, QPainterPath::Element &r) {
        c = path.elementAt(idx);
        l = idx ? path.elementAt(idx - 1) : c;
        r = idx != cnt - 1 ? path.elementAt(idx + 1) : c;
        startP = c;
        estart = idx;
        tstart = target.size();
    };

    auto parseLineElement = [&](int idx, QPainterPath::Element &l, 
                                QPainterPath::Element &c, QPainterPath::Element &r) {
        c = path.elementAt(idx);
        l = idx ? path.elementAt(idx - 1) : c;
        r = idx != cnt - 1 ? path.elementAt(idx + 1) : c;

        if ((QPointF)startP == (QPointF)c && cnt > 2)
            parseClosePath(idx, l, r);
    };

    auto parseCurveElement = [&](int idx, QPainterPath::Element &l, 
                                 QPainterPath::Element &c, QPainterPath::Element &r) {
        QPainterPath::Element c1, c2, ce;
        c1 = path.elementAt(idx);
        c2 = path.elementAt(idx + 1);
        ce = path.elementAt(idx + 2);
        c = ce;
        l = c2 == ce ? c1 : c2;
        r = idx < cnt - 3 ? path.elementAt(idx + 3) : ce;

        if (QPainterPath::CurveToElement == r.type
            && (QPointF)c == (QPointF)r)
            r = path.elementAt(idx + 4);
        if ((QPointF)startP == (QPointF)c && cnt > 2)
            parseClosePath(idx + 2, l, r);
    };

    for (int idx = 0; idx < cnt; idx++) 
    {
        QPainterPath::Element current, left, right;
        switch (path.elementAt(idx).type) 
        {
        case QPainterPath::MoveToElement: 
            parseMoveElement(idx, left, current, right);
            break;
        case QPainterPath::LineToElement: 
            parseLineElement(idx, left, current, right);
            break;
        case QPainterPath::CurveToElement: 
            parseCurveElement(idx, left, current, right);
            break;
        case QPainterPath::CurveToDataElement:
        default:
            continue;
            break;
        }

        qreal angle = getRotateAngle((QPointF)left, (QPointF)right, (QPointF)current);
        target.push_back(qMakePair((QPointF)current, angle));
        if (bSubClose) {
            target[tstart].second = angle;
            bSubClose = false;
        }
    }
}

static qreal getstrokeWidth(const QSvgNode *node)
{
    Q_ASSERT(nullptr != node);
    QSvgStrokeStyle *pCurStroke =
            static_cast<QSvgStrokeStyle *>(node->styleProperty(QSvgStyleProperty::STROKE));

    return (pCurStroke && pCurStroke->isStrokeWidthSet()) ? pCurStroke->width() : 1.0;
}

static void adjustParentCoord(QVector<qreal> &parentCoords, QVector<qreal> &outCoords,
                              const QVector<qreal> &childCoords, int textLen)
{
    const int parentSize = parentCoords.size();
    const int childSize = childCoords.size();
    for (int i = 0; (i < parentSize || i < childSize) && (i < textLen || textLen == 0); ++i) {
         qreal oc = i < childSize ? childCoords[i] : parentCoords[i];
        outCoords.push_back(oc);
    }
    parentCoords.remove(0, qMin(parentSize, textLen));
}

static void resolveCoordAndOffset(const QVector<qreal> &coordX, const QVector<qreal> &coordY,
                                  const QVector<qreal> &offsetX, const QVector<qreal> &offsetY,
                                  QVector<LineCoords> &lineCoordsVec)
{
    const int lenX = coordX.size();
    const int lenY = coordY.size();
    const int lenXOff = offsetX.size();
    const int lenYOff = offsetY.size();
    const int graphLen = qMax(lenX, lenY);

    for (int idx = 0, cnt = graphLen - 1; idx < cnt; ++idx) {
        LineCoords lineCoords;
        lineCoords.validXPos = (idx < lenX);
        lineCoords.validYPos = (idx < lenY);
        lineCoords.firstXPos = (idx < lenX ? coordX[idx] : 0.0);
        lineCoords.firstYPos = (idx < lenY ? coordY[idx] : 0.0);
        QPointF offsetPos;
        offsetPos.rx() = (idx < lenXOff ? offsetX[idx] : 0.0);
        offsetPos.ry() = (idx < lenYOff ? offsetY[idx] : 0.0);
        lineCoords.offset.push_back(offsetPos);
        lineCoordsVec.push_back(lineCoords);
    }

    {
        LineCoords lineCoords;
        lineCoords.validXPos = (graphLen && graphLen <= lenX);
        lineCoords.validYPos = (graphLen && graphLen <= lenY);
        lineCoords.firstXPos = (lineCoords.validXPos ? coordX[graphLen - 1] : 0.0);
        lineCoords.firstYPos = (lineCoords.validYPos ? coordY[graphLen - 1] : 0.0);

        const int curSize = lineCoordsVec.size();
        const int cnt = qMax(lenXOff, lenYOff);
        for (int i = curSize; i < cnt; ++i) {
            const qreal dx = i < lenXOff ? offsetX[i] : 0.0;
            const qreal dy = i < lenYOff ? offsetY[i] : 0.0;
            lineCoords.offset.push_back(QPointF(dx, dy));
        }
        lineCoordsVec.push_back(lineCoords);
    }
}

static void drawPatternNode(QSvgPattern *pattern, QPainter *painter, const QPainterPath &path,
                            const QRectF &targetBounds, QSvgExtraStates &states)
{
    if (pattern) {
        pattern->setClipPath(path);
        pattern->setTargetBounds(targetBounds);
        pattern->drawTile(painter, states);
    }
}

static bool isRatioChildInPattern(QSvgNode *curNode)
{
    bool need = false;
    if (curNode && curNode->parent() && (curNode->parent()->type() == QSvgNode::PATTERN)) {
        QSvgPattern *pattern = static_cast<QSvgPattern*>(curNode->parent());
        if (pattern && (pattern->patternContentUnits() == QSvgPattern::objectBoundingBox)
                && (!curNode->targetBounds().isNull()))
            need = true;
    }
    return need;
}

void QSvgLine::setMarker(const QString& sId, const QString& mId, const QString& eId)
{
    parseMarkerId(this, m_markerLink, sId, mId, eId);
}

void QSvgPath::setMarker(const QString &sId, const QString &mId, const QString &eId)
{
    parseMarkerId(this, m_markerLink, sId, mId, eId);
}

void QSvgPolygon::setMarker(const QString &sId, const QString &mId, const QString &eId)
{
    parseMarkerId(this, m_markerLink, sId, mId, eId);
}

void QSvgPolyline::setMarker(const QString &sId, const QString &mId, const QString &eId)
{
    parseMarkerId(this, m_markerLink, sId, mId, eId);
}

QSvgMarkerUse::QSvgMarkerUse() 
    : start(nullptr), mid(nullptr), end(nullptr), strokeWidth(1.0) {}

void QSvgMarkerUse::drawMarker(QPainter *p, QSvgExtraStates &states)
{
    if (qFuzzyCompare(0.0, strokeWidth))
        return;

    if (nullptr != start && !apexAngle.empty())
        start->draw(p, states, apexAngle.front().first, apexAngle.front().second, strokeWidth);

    if (nullptr != mid && apexAngle.size() > 2) {
        auto begin = apexAngle.cbegin() + 1;
        auto end = apexAngle.cend() - 1;
        for (auto itr = begin; itr != end; ++itr)
            mid->draw(p, states, itr->first, itr->second, strokeWidth);
    }

    if (nullptr != end && apexAngle.size() > 1)
        end->draw(p, states, apexAngle.back().first, apexAngle.back().second, strokeWidth);
}

void QSvgAnimation::draw(QPainter *, QSvgExtraStates &)
{
    qWarning("<animation> no implemented");
}

static inline QRectF boundsOnStroke(QPainter *p, const QPainterPath &path, qreal width)
{
    QPainterPathStroker stroker;
    stroker.setWidth(width);
    QPainterPath stroke = stroker.createStroke(path);
    return p->transform().map(stroke).boundingRect();
}

QSvgEllipse::QSvgEllipse(QSvgNode *parent, const QRectF &rect)
    : QSvgNode(parent), m_bounds(rect), m_fillPattern(nullptr)
{
}


QRectF QSvgEllipse::bounds(QPainter *p, QSvgExtraStates &, bool) const
{
    QPainterPath path;
    path.addEllipse(m_bounds);
    qreal sw = strokeWidth(p);
    return qFuzzyIsNull(sw) ? p->transform().map(path).boundingRect() : boundsOnStroke(p, path, sw);
}

QSvgNode *QSvgEllipse::getFillPattern()
{
    return m_fillPattern;
}

void QSvgEllipse::updateFillPattern(QSvgNode* fillPattern)
{
    Q_ASSERT(!fillPattern || fillPattern->type() == QSvgNode::PATTERN);
    m_fillPattern = static_cast<QSvgPattern*>(fillPattern);
}

void QSvgEllipse::draw(QPainter *p, QSvgExtraStates &states)
{
    applyStyle(p, states);

    bool isRatioInPattern = isRatioChildInPattern(this);
    if (isRatioInPattern)
        p->scale(m_targetBounds.width(), m_targetBounds.height());

    QBrush _oldBrush = p->brush();
    if (m_fillPattern)
        p->setBrush(Qt::NoBrush);

    QT_SVG_DRAW_SHAPE(p->drawEllipse(m_bounds));

    if (isRatioInPattern)
        p->scale((1.0 / m_targetBounds.width()), (1.0 / m_targetBounds.height()));

    if (m_fillPattern)
        p->setBrush(_oldBrush);

    revertStyle(p, states);

    if (m_fillPattern) {
        QPainterPath path;
        path.addEllipse(m_bounds);
        drawPatternNode(m_fillPattern, p, path, m_bounds, states);
    }
}

QSvgArc::QSvgArc(QSvgNode *parent, const QPainterPath &path)
    : QSvgNode(parent), m_path(path)
{
}

void QSvgArc::draw(QPainter *p, QSvgExtraStates &states)
{
    applyStyle(p, states);
    if (p->pen().widthF() != 0) {
        qreal oldOpacity = p->opacity();
        p->setOpacity(oldOpacity * states.strokeOpacity);
        p->drawPath(m_path);
        p->setOpacity(oldOpacity);
    }
    revertStyle(p, states);
}

QSvgImage::QSvgImage(QSvgNode *parent, const QImage &image,
                     const QRectF &bounds)
    : QSvgNode(parent), m_image(image),
      m_bounds(bounds)
{
    if (m_bounds.width() == 0.0)
        m_bounds.setWidth(static_cast<qreal>(m_image.width()));
    if (m_bounds.height() == 0.0)
        m_bounds.setHeight(static_cast<qreal>(m_image.height()));
}

void QSvgImage::draw(QPainter *p, QSvgExtraStates &states)
{
    applyStyle(p, states);

    qreal oldOpacity = p->opacity();
    p->setOpacity(oldOpacity * states.fillOpacity);

    bool isRatioInPattern = isRatioChildInPattern(this);
    if (isRatioInPattern)
        p->scale(m_targetBounds.width(), m_targetBounds.height());

#ifndef QT_NO_EXCEPTIONS
    try {
#endif
        if (auto tinydoc = document()) {
            const QPixmap &pixmap = tinydoc->convertToPixmap(p, m_image);
            p->drawPixmap(m_bounds, pixmap, QRectF(0, 0, m_image.width(), m_image.height()));
        } else {
            p->drawImage(m_bounds, m_image);
        }
#ifndef QT_NO_EXCEPTIONS
    } catch (const std::exception &) {
    }
#endif

    if (isRatioInPattern)
        p->scale((1.0 / m_targetBounds.width()), (1.0 / m_targetBounds.height()));

    p->setOpacity(oldOpacity);
    revertStyle(p, states);
}


QSvgLine::QSvgLine(QSvgNode *parent, const QLineF &line)
    : QSvgNode(parent), m_line(line)
{
}


void QSvgLine::draw(QPainter *p, QSvgExtraStates &states)
{
    applyStyle(p, states);

    bool isRatioInPattern = isRatioChildInPattern(this);
    if (isRatioInPattern)
        p->scale(m_targetBounds.width(), m_targetBounds.height());

    if (p->pen().widthF() != 0) {
        qreal oldOpacity = p->opacity();
        p->setOpacity(oldOpacity * states.strokeOpacity);
        p->drawLine(m_line);
        p->setOpacity(oldOpacity);
    }

    if (isRatioInPattern)
        p->scale((1.0 / m_targetBounds.width()), (1.0 / m_targetBounds.height()));

    revertStyle(p, states);

    parseMarkerUse(m_markerLink, document());
    m_markerLink.strokeWidth = getstrokeWidth(this);
    m_markerLink.drawMarker(p, states);
}

void QSvgLine::updateMarker()
{
    m_markerLink.apexAngle.clear();

    QPair<QPointF, qreal> point1, point2;
    point1.first = m_line.p1();
    point1.second = 360.0 - m_line.angle();
    point2.first = m_line.p2();
    point2.second = 360.0 - m_line.angle();

    m_markerLink.apexAngle.push_back(point1);
    m_markerLink.apexAngle.push_back(point2);
}

QSvgPath::QSvgPath(QSvgNode *parent, const QPainterPath &qpath)
    : QSvgNode(parent), m_path(qpath), m_fillPattern(nullptr)
{
}

void QSvgPath::draw(QPainter *p, QSvgExtraStates &states)
{
    applyStyle(p, states);
    m_path.setFillRule(states.fillRule);

    bool isRatioInPattern = isRatioChildInPattern(this);
    if (isRatioInPattern)
        p->scale(m_targetBounds.width(), m_targetBounds.height());

    QBrush _oldBrush = p->brush();
    if (m_fillPattern)
        p->setBrush(Qt::NoBrush);

    QT_SVG_DRAW_SHAPE(p->drawPath(m_path));

    if (isRatioInPattern)
        p->scale((1.0 / m_targetBounds.width()), (1.0 / m_targetBounds.height()));

    if (m_fillPattern)
        p->setBrush(_oldBrush);

    revertStyle(p, states);

    parseMarkerUse(m_markerLink, document());
    m_markerLink.strokeWidth = getstrokeWidth(this);
    m_markerLink.drawMarker(p, states);

    if (m_fillPattern) {
        bool isDot = (m_path.elementCount() <= 1);
        if (!isDot)
            drawPatternNode(m_fillPattern, p, m_path, m_path.boundingRect(), states);
    }
}

void QSvgPath::updateMarker()
{
    m_markerLink.apexAngle.clear();
    parsePathData(m_path, m_markerLink.apexAngle);
}

QSvgNode *QSvgPath::getFillPattern()
{
    return m_fillPattern;
}

void QSvgPath::updateFillPattern(QSvgNode* fillPattern)
{
    Q_ASSERT(!fillPattern || fillPattern->type() == QSvgNode::PATTERN);
    m_fillPattern = static_cast<QSvgPattern *>(fillPattern);
}

QRectF QSvgPath::bounds(QPainter *p, QSvgExtraStates &, bool) const
{
    qreal sw = strokeWidth(p);
    return qFuzzyIsNull(sw) ? p->transform().map(m_path).boundingRect()
        : boundsOnStroke(p, m_path, sw);
}

QSvgPolygon::QSvgPolygon(QSvgNode *parent, const QPolygonF &poly)
    : QSvgNode(parent), m_poly(poly), m_fillPattern(nullptr)
{
}

QRectF QSvgPolygon::bounds(QPainter *p, QSvgExtraStates &, bool) const
{
    qreal sw = strokeWidth(p);
    if (qFuzzyIsNull(sw)) {
        return p->transform().map(m_poly).boundingRect();
    } else {
        QPainterPath path;
        path.addPolygon(m_poly);
        return boundsOnStroke(p, path, sw);
    }
}

QSvgNode *QSvgPolygon::getFillPattern()
{
    return m_fillPattern;
}

void QSvgPolygon::updateFillPattern(QSvgNode* fillPattern)
{
    Q_ASSERT(!fillPattern || fillPattern->type() == QSvgNode::PATTERN);
    m_fillPattern = static_cast<QSvgPattern *>(fillPattern);
}

void QSvgPolygon::draw(QPainter *p, QSvgExtraStates &states)
{
    applyStyle(p, states);

    bool isRatioInPattern = isRatioChildInPattern(this);
    if (isRatioInPattern)
        p->scale(m_targetBounds.width(), m_targetBounds.height());

    QBrush _oldBrush = p->brush();
    if (m_fillPattern)
        p->setBrush(Qt::NoBrush);

    QT_SVG_DRAW_SHAPE(p->drawPolygon(m_poly, states.fillRule));

    if (isRatioInPattern)
        p->scale((1.0 / m_targetBounds.width()), (1.0 / m_targetBounds.height()));

    if (m_fillPattern)
        p->setBrush(_oldBrush);

    revertStyle(p, states);

    parseMarkerUse(m_markerLink, document());
    m_markerLink.strokeWidth = getstrokeWidth(this);
    m_markerLink.drawMarker(p, states);

    if (m_fillPattern) {
        QPainterPath path;
        path.addPolygon(m_poly);
        drawPatternNode(m_fillPattern, p, path, m_poly.boundingRect(), states);
    }
}

void QSvgPolygon::updateMarker()
{
    m_markerLink.apexAngle.clear();
    parsePolyData(QSvgNode::POLYGON, m_poly, m_markerLink.apexAngle);
}

QSvgPolyline::QSvgPolyline(QSvgNode *parent, const QPolygonF &poly)
    : QSvgNode(parent), m_poly(poly), m_fillPattern(nullptr)
{

}

void QSvgPolyline::draw(QPainter *p, QSvgExtraStates &states)
{
    applyStyle(p, states);

    bool isRatioInPattern = isRatioChildInPattern(this);
    if (isRatioInPattern)
        p->scale(m_targetBounds.width(), m_targetBounds.height());

    QBrush _oldBrush = p->brush();
    if (m_fillPattern)
        p->setBrush(Qt::NoBrush);

    qreal oldOpacity = p->opacity();
    if (p->brush().style() != Qt::NoBrush) {
        QPen save = p->pen();
        p->setPen(QPen(Qt::NoPen));
        p->setOpacity(oldOpacity * states.fillOpacity);
        p->drawPolygon(m_poly, states.fillRule);
        p->setPen(save);
    }
    if (p->pen().widthF() != 0) {
        p->setOpacity(oldOpacity * states.strokeOpacity);
        p->drawPolyline(m_poly);
    }
    p->setOpacity(oldOpacity);

    if (isRatioInPattern)
        p->scale((1.0 / m_targetBounds.width()), (1.0 / m_targetBounds.height()));

    if (m_fillPattern)
        p->setBrush(_oldBrush);

    revertStyle(p, states);

    parseMarkerUse(m_markerLink, document());
    m_markerLink.strokeWidth = getstrokeWidth(this);
    m_markerLink.drawMarker(p, states);

    if (m_fillPattern) {
        QPainterPath path;
        path.addPolygon(m_poly);
        drawPatternNode(m_fillPattern, p, path, m_poly.boundingRect(), states);
    }
}

QSvgNode *QSvgPolyline::getFillPattern()
{
    return m_fillPattern;
}

void QSvgPolyline::updateFillPattern(QSvgNode* fillPattern)
{
    Q_ASSERT(!fillPattern || fillPattern->type() == QSvgNode::PATTERN);
    m_fillPattern = static_cast<QSvgPattern *>(fillPattern);
}

void QSvgPolyline::updateMarker()
{
    m_markerLink.apexAngle.clear();
    parsePolyData(QSvgNode::POLYLINE, m_poly, m_markerLink.apexAngle);
}

QSvgRect::QSvgRect(QSvgNode *node, const QRectF &rect, int rx, int ry)
    : QSvgNode(node),
      m_rect(rect), m_rx(rx), m_ry(ry), m_fillPattern(nullptr)
{
}

QRectF QSvgRect::bounds(QPainter *p, QSvgExtraStates &, bool) const
{
    qreal sw = strokeWidth(p);
    if (qFuzzyIsNull(sw)) {
        return p->transform().mapRect(m_rect);
    } else {
        QPainterPath path;
        path.addRect(m_rect);
        return boundsOnStroke(p, path, sw);
    }
}

QSvgNode *QSvgRect::getFillPattern()
{
    return m_fillPattern;
}

void QSvgRect::updateFillPattern(QSvgNode* fillPattern)
{
    Q_ASSERT(!fillPattern || fillPattern->type() == QSvgNode::PATTERN);
    m_fillPattern = static_cast<QSvgPattern *>(fillPattern);
}

void QSvgRect::draw(QPainter *p, QSvgExtraStates &states)
{
    applyStyle(p, states);

    bool isRatioInPattern = isRatioChildInPattern(this);
    if (isRatioInPattern)
        p->scale(m_targetBounds.width(), m_targetBounds.height());

    QBrush _oldBrush = p->brush();
    if (m_fillPattern)
        p->setBrush(Qt::NoBrush);

    if (m_rx || m_ry) {
        QT_SVG_DRAW_SHAPE(p->drawRoundedRect(m_rect, m_rx, m_ry, Qt::RelativeSize));
    } else {
        QT_SVG_DRAW_SHAPE(p->drawRect(m_rect));
    }

    if (isRatioInPattern)
        p->scale((1.0 / m_targetBounds.width()), (1.0 / m_targetBounds.height()));

    if (m_fillPattern)
        p->setBrush(_oldBrush);

    revertStyle(p, states);

    if (m_fillPattern) {
        QPainterPath path;
        path.addRect(m_rect);
        drawPatternNode(m_fillPattern, p, path, m_rect, states);
    }
}

QSvgTspan * const QSvgText::LINEBREAK = 0;

QSvgText::QSvgText(QSvgNode *parent, const QPointF &coord)
    : QSvgNode(parent)
     , m_coord(coord)
     , m_type(TEXT)
     , m_size(0, 0)
     , m_mode(Default)
     , m_resolved(false)
{
}

QSvgText::QSvgText(const QSvgText &other)
    : QSvgNode(other)
    , m_coord(other.m_coord)
    , m_type(other.m_type)
    , m_size(other.m_size)
    , m_mode(other.m_mode)
    , m_resolved(other.m_resolved)
    , m_paragraphs(other.m_paragraphs)
    , m_paragraphCoords(other.m_paragraphCoords)
    , m_formatRanges(other.m_formatRanges)
{
    int size = other.m_tspans.size();
    m_tspans.reserve(size);
    for (int i = 0; i < size; ++i) {
        if (other.m_tspans[i] == LINEBREAK)
            m_tspans.push_back(LINEBREAK);
        else
            m_tspans.push_back(new QSvgTspan(*other.m_tspans[i]));
    }
}

QSvgText::~QSvgText()
{
    for (int i = m_tspans.size() - 1; i >= 0; --i) {
        if (m_tspans[i] != LINEBREAK) {
            delete m_tspans[i];
            m_tspans[i] = nullptr;
        }
    }
}

qreal QSvgText::lineWidth(QString graph, QTextLayout::FormatRange formatRange, qreal scale,
                          const QVector<QPointF> &offsets, const QSvgFont *svgFont)
{
    if (graph.isEmpty())
        return 0.0;

    qreal lineInc = 0.0;
    for (int i = 1, cnt = offsets.size(); i < cnt; ++i)
        lineInc += offsets[i].x();

    if (svgFont)
        return svgFont->textWidth(graph) / scale + lineInc;

    formatRange.length = graph.length();
    QTextLayout tl(graph);
    QTextOption op = tl.textOption();
    op.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    tl.setTextOption(op);
    tl.setFormats(QVector<QTextLayout::FormatRange>(graph.length(), formatRange));
    tl.beginLayout();
    forever {
        QTextLine line = tl.createLine();
        if (!line.isValid())
            break;
    }
    tl.endLayout();
    QTextLine line = tl.lineAt(0);
    return line.naturalTextWidth() / scale + lineInc;
}

void QSvgText::drawCharacters(QPainter *p, QString characters, QPointF pos,
                             QTextLayout::FormatRange formatRange, QPointF &nextPos)
{
    const qreal scale = 100.0 / p->font().pointSizeF();
    QTextLayout tl(characters);
    QTextOption op = tl.textOption();
    op.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    formatRange.length = characters.length();
    tl.setTextOption(op);
    tl.setFormats(QVector<QTextLayout::FormatRange>(formatRange.length, formatRange));
    tl.beginLayout();
    forever {
        QTextLine line = tl.createLine();
        if (!line.isValid())
            break;
    }
    tl.endLayout();

    QTextLine line = tl.lineAt(0);
    line.setPosition(QPointF(0.0, -line.ascent()));

    nextPos = pos + QPointF(line.naturalTextWidth(), 0) / scale;
    tl.draw(p, pos * scale, QVector<QTextLayout::FormatRange>());
}

void QSvgText::drawLine(QPainter *p, QSvgExtraStates &states, QString graph, QPointF pos,
                        QTextLayout::FormatRange formatRange, const QVector<QPointF> &offsets,
                        QPointF &nextPos)
{
    const qreal scale = 100.0 / p->font().pointSizeF();
    nextPos = pos;
    for (int idx = 0, cnt = graph.size(); idx < cnt; ++idx) {
        QPointF curPos = nextPos + (idx < offsets.size() ? offsets[idx] : QPointF());
        QString curStr = (idx < offsets.size() ? QString(graph[idx]) : graph.right(cnt - idx));

        if (states.svgFont) {
            states.svgFont->draw(p, curPos * scale, curStr, p->font().pointSizeF() * scale, states.textAnchor);
            nextPos += QPointF(states.svgFont->textWidth(curStr) / scale, 0);
        } else {
            drawCharacters(p, curStr, curPos, formatRange, nextPos);
        }

        if (idx >= offsets.size())
            break;
    }
}

void QSvgText::resolveTspans(QPainter *p, QSvgExtraStates &states, qreal scale)
{
    if (!m_resolved) {
        QVector<qreal> coordX, coordY, offsetX, offsetY;
        for (QSvgTspan* child : m_tspans) {
            processTspansCoords(child, coordX, coordY, offsetX, offsetY);
        }
        if (!m_paragraphs.empty() && m_paragraphs.last().endsWith(QChar(' ')))
            m_paragraphs.last().chop(1);
        m_resolved = true;
    }

    m_formatRanges.clear();
    for (QSvgTspan *child : m_tspans) {
        processTspansFormats(child, p, states, scale);
    }
}

void QSvgText::processTspansFormats(QSvgTspan *tspan, QPainter *p, QSvgExtraStates &states,
                                    qreal scale)
{
    for (int i = 0, cnt = tspan->segments(); i < cnt; ++i) {
        tspan->applyStyle(p, states);
        QFont font = p->font();
        QPen pen = p->pen();
        font.setPixelSize(font.pointSizeF() * scale);
        pen.setWidthF(pen.widthF() * scale);
        QTextLayout::FormatRange range;
        range.start = 0;
        range.format.setFont(font);
        range.format.setTextOutline(pen);
        range.format.setForeground(p->brush());
        m_formatRanges.push_back(range);
        tspan->revertStyle(p, states);
    }
    for (QSvgTspan *child : tspan->renderers()) {
        processTspansFormats(child, p, states, scale);
    }
}

void QSvgText::processTspansCoords(QSvgTspan *tspan,
                                   QVector<qreal> &parentXCoords, QVector<qreal> &parentYCoords,
                                   QVector<qreal> &parentoffsetX, QVector<qreal> &parentoffsetY)
{
    QString newText = tspan->text();
    newText.replace(QLatin1Char('\t'), QLatin1Char(' '));
    newText.replace(QLatin1Char('\n'), QLatin1Char(' '));
    const bool bStartSpace = (newText.startsWith(QChar(0x20)) || newText.startsWith(QChar(0xa0)));
    const bool bEndSpace = (newText.endsWith(QChar(0x20)) || newText.endsWith(QChar(0xa0)));

    if (tspan->whitespaceMode() == QSvgText::Default) {
        newText = newText.simplified();
        if (bStartSpace && !m_paragraphs.empty() && !m_paragraphs.last().endsWith(QChar(' ')))
            newText.prepend(QLatin1Char(' '));

        if (bEndSpace && !newText.isEmpty() && !newText.endsWith(QChar(' ')))
            newText.append(QLatin1Char(' '));
    }

    QVector<qreal> coordX, coordY, offsetX, offsetY;
    const int textLen = newText.length();
    adjustParentCoord(parentXCoords, coordX, tspan->coordX(), textLen);
    adjustParentCoord(parentYCoords, coordY, tspan->coordY(), textLen);
    adjustParentCoord(parentoffsetX, offsetX, tspan->offsetX(), textLen);
    adjustParentCoord(parentoffsetY, offsetY, tspan->offsetY(), textLen);

    QVector<LineCoords> lineCoords;
    resolveCoordAndOffset(coordX, coordY, offsetX, offsetY, lineCoords);
    
    Q_ASSERT(m_paragraphs.size() == m_paragraphCoords.size());
    const int paragraphSize = qMin(lineCoords.size(), newText.length());
    tspan->setSegments(paragraphSize);

    for (int idx = 0; idx < paragraphSize; ++idx) {
        QString graphs(newText[idx]);
        if (idx == paragraphSize - 1)
            graphs = newText.right(newText.length() - idx);
        m_paragraphs.push_back(graphs);
        m_paragraphCoords.push_back(lineCoords[idx]);
    }

    for (QSvgTspan *child : tspan->renderers()) {
        processTspansCoords(child, coordX, coordY, offsetX, offsetY);
    }
}

void QSvgText::clearTspans()
{
    for (int i = m_tspans.size() - 1; i >= 0; --i) {
        if (m_tspans[i] != LINEBREAK) {
            delete m_tspans[i];
            m_tspans[i] = nullptr;
        }
    }
    m_tspans.clear();
}

void QSvgText::setTextArea(const QSizeF &size)
{
    m_size = size;
    m_type = TEXTAREA;
}

//QRectF QSvgText::bounds(QPainter *p, QSvgExtraStates &, bool) const {}

void QSvgText::draw(QPainter *p, QSvgExtraStates &states)
{
    applyStyle(p, states);
    qreal oldOpacity = p->opacity();
    p->setOpacity(oldOpacity * states.fillOpacity);

    // Force the font to have a size of 100 pixels to avoid truncation problems
    // when the font is very small.
    qreal scale = 100.0 / p->font().pointSizeF();
    QTransform oldTransform = p->worldTransform();
    p->scale(1 / scale, 1 / scale);

    resolveTspans(p, states, scale);

    QPointF nextPos = m_coord;
    for (int i = 0, cnt = m_paragraphs.size(); i < cnt; ++i) {
        const bool validXPos = m_paragraphCoords[i].validXPos;
        const bool validYPos = m_paragraphCoords[i].validYPos;
        QPointF pos = QPointF(validXPos ? m_paragraphCoords[i].firstXPos : nextPos.rx(),
                              validYPos ? m_paragraphCoords[i].firstYPos : nextPos.ry());

        Qt::Alignment alignment = states.textAnchor;
        if ((alignment == Qt::AlignHCenter || alignment == Qt::AlignRight)
            && (i == 0 || validXPos || validYPos)) {
            qreal lWidth = 0.0;
            for (int j = i; j < cnt; ++j) {
                lWidth += lineWidth(m_paragraphs[j], m_formatRanges[j], scale, m_paragraphCoords[j].offset, states.svgFont);
                if (j + 1 == cnt || m_paragraphCoords[j + 1].validXPos || m_paragraphCoords[j + 1].validYPos)
                    break;
            }
            pos.rx() -= (alignment == Qt::AlignHCenter ? (lWidth / 2.0) : lWidth);
        }
        drawLine(p, states, m_paragraphs[i], pos, m_formatRanges[i], m_paragraphCoords[i].offset, nextPos);
    }

    p->setWorldTransform(oldTransform, false);
    p->setOpacity(oldOpacity);
    revertStyle(p, states);
}

void QSvgText::addText(const QString &text)
{
    m_tspans.append(new QSvgTspan(this));
    m_tspans.back()->setWhitespaceMode(m_mode);
    m_tspans.back()->addText(text);
}

void QSvgTspan::addChild(QSvgTspan *child)
{
    m_renderers.append(child);
}

void QSvgTspan::setCoordAndOffset(const QVector<qreal> &coordX, const QVector<qreal> &coordY,
                                  const QVector<qreal> &offsetX, const QVector<qreal> &offsetY)
{
    m_coordX = coordX;
    m_coordY = coordY;
    m_offsetX = offsetX;
    m_offsetY = offsetY;
}

QSvgTspan::QSvgTspan(const QSvgTspan &other)
    : QSvgNode(other), 
      m_text(other.m_text), 
      m_mode(other.m_mode), 
      m_segments(other.m_segments),
      m_coordX(other.m_coordX),
      m_coordY(other.m_coordY),
      m_offsetX(other.m_offsetX),
      m_offsetY(other.m_offsetY)
{
    m_renderers.reserve(other.m_renderers.size());
    for (QSvgTspan *tsNode : other.m_renderers)
        m_renderers.append(static_cast<QSvgTspan *>(tsNode->clone(this)));
}

QSvgTspan::~QSvgTspan() 
{
    qDeleteAll(m_renderers);
}

QSvgUse::QSvgUse(const QPointF &start, QSvgNode *parent, QSvgNode *node)
    : QSvgNode(parent), m_link(node), m_start(start), m_recursing(false)
{

}

void QSvgUse::draw(QPainter *p, QSvgExtraStates &states)
{
    if (Q_UNLIKELY((NULL == m_link && m_linkId.isEmpty()) || m_recursing))
        return;

    if (NULL == m_link) {
        m_link = document()->namedNode(m_linkId);
        if (NULL == m_link)
            return;
    }

    applyStyle(p, states);

    if (!m_start.isNull()) {
        p->translate(m_start);
    }
    {
        QScopedValueRollback<bool> guard(m_recursing, true);
        m_link->draw(p, states);
    }
    if (!m_start.isNull()) {
        p->translate(-m_start);
    }

    revertStyle(p, states);
}

void QSvgVideo::draw(QPainter *p, QSvgExtraStates &states)
{
    applyStyle(p, states);

    revertStyle(p, states);
}

QSvgUse::QSvgUse(const QSvgUse &other) 
    : QSvgNode(other), m_link(0), m_start(other.start()), m_linkId(other.linkId()), m_recursing(false)
{

}

QSvgNode *QSvgRect::clone(QSvgNode *parent)
{
    QSvgRect *newNode = new QSvgRect(*this);
    newNode->setParent(parent);
    return newNode;
}

QSvgNode *QSvgTspan::clone(QSvgNode *parent)
{
    QSvgNode *newNode = new QSvgTspan(*this);
    newNode->setParent(parent);
    return newNode;
}

QSvgNode *QSvgAnimation::clone(QSvgNode *parent)
{
    QSvgNode *newNode = new QSvgAnimation(*this);
    newNode->setParent(parent);
    return newNode;
}

QSvgNode *QSvgArc::clone(QSvgNode *parent)
{
    QSvgNode *newNode = new QSvgArc(*this);
    newNode->setParent(parent);
    return newNode;
}

QSvgNode *QSvgCircle::clone(QSvgNode *parent)
{
    QSvgNode *newNode = new QSvgCircle(*this);
    newNode->setParent(parent);
    return newNode;
}

QSvgNode *QSvgEllipse::clone(QSvgNode *parent)
{
    QSvgNode *newNode = new QSvgEllipse(*this);
    newNode->setParent(parent);
    return newNode;
}

QSvgNode *QSvgImage::clone(QSvgNode *parent)
{
    QSvgNode *newNode = new QSvgImage(*this);
    newNode->setParent(parent);
    return newNode;
}

QSvgNode *QSvgLine::clone(QSvgNode *parent)
{
    QSvgNode *newNode = new QSvgLine(*this);
    newNode->setParent(parent);
    return newNode;
}

QSvgNode *QSvgPath::clone(QSvgNode *parent)
{
    QSvgNode *newNode = new QSvgPath(*this);
    newNode->setParent(parent);
    return newNode;
}

QSvgNode *QSvgPolygon::clone(QSvgNode *parent)
{
    QSvgNode *newNode = new QSvgPolygon(*this);
    newNode->setParent(parent);
    return newNode;
}

QSvgNode *QSvgPolyline::clone(QSvgNode *parent)
{
    QSvgNode *newNode = new QSvgPolyline(*this);
    newNode->setParent(parent);
    return newNode;
}

QSvgNode *QSvgText::clone(QSvgNode *parent)
{
    QSvgNode *newNode = new QSvgText(*this);
    newNode->setParent(parent);
    return newNode;
}

QSvgNode *QSvgUse::clone(QSvgNode *parent)
{
    QSvgNode *newNode = new QSvgUse(*this);
    newNode->setParent(parent);
    return newNode;
}

QSvgNode *QSvgVideo::clone(QSvgNode *parent)
{
    QSvgNode *newNode = new QSvgVideo(*this);
    newNode->setParent(parent);
    return newNode;
}

QSvgNode::Type QSvgAnimation::type() const
{
    return ANIMATION;
}

QSvgNode::Type QSvgArc::type() const
{
    return ARC;
}

QSvgNode::Type QSvgCircle::type() const
{
    return CIRCLE;
}

QSvgNode::Type QSvgEllipse::type() const
{
    return ELLIPSE;
}

QSvgNode::Type QSvgImage::type() const
{
    return IMAGE;
}

QSvgNode::Type QSvgLine::type() const
{
    return LINE;
}

QSvgNode::Type QSvgPath::type() const
{
    return PATH;
}

QSvgNode::Type QSvgPolygon::type() const
{
    return POLYGON;
}

QSvgNode::Type QSvgPolyline::type() const
{
    return POLYLINE;
}

QSvgNode::Type QSvgRect::type() const
{
    return RECT;
}

QSvgNode::Type QSvgText::type() const
{
    return m_type;
}

QSvgNode::Type QSvgUse::type() const
{
    return USE;
}

QSvgNode::Type QSvgVideo::type() const
{
    return VIDEO;
}

QRectF QSvgUse::bounds(QPainter *p, QSvgExtraStates &states, bool defaultViewCoord) const
{
    QRectF bounds;
    if (Q_LIKELY(m_link && !isDescendantOf(m_link) && !m_recursing)) {
        QScopedValueRollback<bool> guard(m_recursing, true);
        p->translate(m_start);
        bounds = m_link->transformedBounds(p, states, defaultViewCoord);
        p->translate(-m_start);
    }
    return bounds;
}

QRectF QSvgPolyline::bounds(QPainter *p, QSvgExtraStates &, bool) const
{
    qreal sw = strokeWidth(p);
    if (qFuzzyIsNull(sw)) {
        return p->transform().map(m_poly).boundingRect();
    } else {
        QPainterPath path;
        path.addPolygon(m_poly);
        return boundsOnStroke(p, path, sw);
    }
}

QRectF QSvgArc::bounds(QPainter *p, QSvgExtraStates &, bool) const
{
    qreal sw = strokeWidth(p);
    return qFuzzyIsNull(sw) ? p->transform().map(m_path).boundingRect()
        : boundsOnStroke(p, m_path, sw);
}

QRectF QSvgImage::bounds(QPainter *p, QSvgExtraStates &, bool) const
{
    return p->transform().mapRect(m_bounds);
}

QRectF QSvgLine::bounds(QPainter *p, QSvgExtraStates &, bool) const
{
    qreal sw = strokeWidth(p);
    if (qFuzzyIsNull(sw)) {
        QPointF p1 = p->transform().map(m_line.p1());
        QPointF p2 = p->transform().map(m_line.p2());
        qreal minX = qMin(p1.x(), p2.x());
        qreal minY = qMin(p1.y(), p2.y());
        qreal maxX = qMax(p1.x(), p2.x());
        qreal maxY = qMax(p1.y(), p2.y());
        return QRectF(minX, minY, maxX - minX, maxY - minY);
    } else {
        QPainterPath path;
        path.moveTo(m_line.p1());
        path.lineTo(m_line.p2());
        return boundsOnStroke(p, path, sw);
    }
}

QT_END_NAMESPACE
