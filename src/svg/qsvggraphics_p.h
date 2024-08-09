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

#ifndef QSVGGRAPHICS_P_H
#define QSVGGRAPHICS_P_H

//
//  W A R N I N G
//  -------------
//
// This file is not part of the Qt API.  It exists purely as an
// implementation detail.  This header file may change from version to
// version without notice, or even be removed.
//
// We mean it.
//

#include "qsvgnode_p.h"
#include "qtsvgglobal_p.h"

#include "QtGui/qpainterpath.h"
#include "QtGui/qimage.h"
#include "QtGui/qtextlayout.h"
#include "QtGui/qtextoption.h"
#include "QtCore/qstack.h"
#include "qsvgstructure_p.h"

QT_BEGIN_NAMESPACE

class QTextCharFormat;

typedef QVector<QPair<QPointF, qreal>> QSvgApexList;

struct LineCoords
{
    bool validXPos = false;
    bool validYPos = false;
    qreal firstXPos = 0.0;
    qreal firstYPos = 0.0;
    QVector<QPointF> offset;
};

class QSvgMarkerUse
{
public:
    QSvgMarkerUse();

    void drawMarker(QPainter *p, QSvgExtraStates &states);

    QSvgMarker *start;
    QSvgMarker *mid;
    QSvgMarker *end;

    QString startId;
    QString midId;
    QString endId;

    QSvgApexList apexAngle;
    qreal strokeWidth;
};

class Q_SVG_PRIVATE_EXPORT QSvgAnimation : public QSvgNode
{
public:
    void draw(QPainter *p, QSvgExtraStates &states) override;
    Type type() const override;
    QSvgNode *clone(QSvgNode *parent) override;
};

class Q_SVG_PRIVATE_EXPORT QSvgArc : public QSvgNode
{
public:
    QSvgArc(QSvgNode *parent, const QPainterPath &path);
    void draw(QPainter *p, QSvgExtraStates &states) override;
    Type type() const override;
    QSvgNode *clone(QSvgNode *parent) override;
    QRectF bounds(QPainter *p, QSvgExtraStates &states, bool defaultViewCoord) const override;
private:
    QPainterPath m_path;
};

class Q_SVG_PRIVATE_EXPORT QSvgEllipse : public QSvgNode
{
public:
    QSvgEllipse(QSvgNode *parent, const QRectF &rect);
    void draw(QPainter *p, QSvgExtraStates &states) override;
    Type type() const override;
    QSvgNode *clone(QSvgNode *parent) override;
    QRectF bounds(QPainter *p, QSvgExtraStates &states, bool defaultViewCoord) const override;
    QSvgNode *getFillPattern() override;
    void updateFillPattern(QSvgNode* fillPattern) override;

    const QRectF &bounds() const { return m_bounds; }
private:
    QRectF m_bounds;
    QSvgPattern *m_fillPattern;
};

class Q_SVG_PRIVATE_EXPORT QSvgCircle : public QSvgEllipse
{
public:
    QSvgCircle(QSvgNode *parent, const QRectF &rect) : QSvgEllipse(parent, rect) { }
    QSvgNode *clone(QSvgNode *parent) override;
    Type type() const override;
};

class Q_SVG_PRIVATE_EXPORT QSvgImage : public QSvgNode
{
public:
    QSvgImage(QSvgNode *parent, const QImage &image,
              const QRectF &bounds);
    void draw(QPainter *p, QSvgExtraStates &states) override;
    Type type() const override;
    QSvgNode *clone(QSvgNode *parent) override;
    QRectF bounds(QPainter *p, QSvgExtraStates &states, bool defaultViewCoord) const override;

    const QRectF &bounds() const { return m_bounds; }
    const QImage &image() const { return m_image; }
private:
    QImage m_image;
    QRectF m_bounds;
};

class Q_SVG_PRIVATE_EXPORT QSvgLine : public QSvgNode
{
public:
    QSvgLine(QSvgNode *parent, const QLineF &line);
    void draw(QPainter *p, QSvgExtraStates &states) override;
    Type type() const override;
    QSvgNode *clone(QSvgNode *parent) override;
    QRectF bounds(QPainter *p, QSvgExtraStates &states, bool defaultViewCoord) const override;

    const QLineF &line() const { return m_line; }
    const QSvgMarkerUse& Marker() const { return m_markerLink; }
    void setMarker(const QString& sId, const QString& mId, const QString& eId);
    void updateMarker();

private:
    QSvgMarkerUse m_markerLink;
    QLineF m_line;
};

class Q_SVG_PRIVATE_EXPORT QSvgPath : public QSvgNode
{
public:
    QSvgPath(QSvgNode *parent, const QPainterPath &qpath);
    void draw(QPainter *p, QSvgExtraStates &states) override;
    Type type() const override;
    QSvgNode *clone(QSvgNode *parent) override;
    QRectF bounds(QPainter *p, QSvgExtraStates &states, bool defaultViewCoord) const override;
    QSvgNode *getFillPattern() override;
    void updateFillPattern(QSvgNode* fillPattern) override;

    QPainterPath *qpath() {
        return &m_path;
    }
    const QPainterPath &path() const { return m_path; }
    const QSvgMarkerUse &Marker() const { return m_markerLink; }
    void setMarker(const QString &sId, const QString &mId, const QString &eId);
    void updateMarker();

private:
    QSvgMarkerUse m_markerLink;
    QPainterPath m_path;
    QSvgPattern *m_fillPattern;
};

class Q_SVG_PRIVATE_EXPORT QSvgPolygon : public QSvgNode
{
public:
    QSvgPolygon(QSvgNode *parent, const QPolygonF &poly);
    void draw(QPainter *p, QSvgExtraStates &states) override;
    Type type() const override;
    QSvgNode *clone(QSvgNode *parent) override;
    QRectF bounds(QPainter *p, QSvgExtraStates &states, bool defaultViewCoord) const override;
    QSvgNode *getFillPattern() override;
    void updateFillPattern(QSvgNode* fillPattern) override;

    const QPolygonF &poly() const { return m_poly; }
    const QSvgMarkerUse &Marker() const { return m_markerLink; }
    void setMarker(const QString &sId, const QString &mId, const QString &eId);
    void updateMarker();

private:
    QSvgMarkerUse m_markerLink;
    QPolygonF m_poly;
    QSvgPattern *m_fillPattern;
};

class Q_SVG_PRIVATE_EXPORT QSvgPolyline : public QSvgNode
{
public:
    QSvgPolyline(QSvgNode *parent, const QPolygonF &poly);
    void draw(QPainter *p, QSvgExtraStates &states) override;
    Type type() const override;
    QSvgNode *clone(QSvgNode *parent) override;
    QRectF bounds(QPainter *p, QSvgExtraStates &states, bool defaultViewCoord) const override;
    QSvgNode *getFillPattern() override;
    void updateFillPattern(QSvgNode* fillPattern) override;
    const QPolygonF &poly() const { return m_poly; }

    const QSvgMarkerUse &Marker() const { return m_markerLink; }
    void setMarker(const QString &sId, const QString &mId, const QString &eId);
    void updateMarker();

private:
    QSvgMarkerUse m_markerLink;
    QPolygonF m_poly;
    QSvgPattern *m_fillPattern;
};

class Q_SVG_PRIVATE_EXPORT QSvgRect : public QSvgNode
{
public:
    QSvgRect(QSvgNode *paren, const QRectF &rect, int rx=0, int ry=0);
    Type type() const override;
    void draw(QPainter *p, QSvgExtraStates &states) override;
    QSvgNode *clone(QSvgNode *parent) override;
    QRectF bounds(QPainter *p, QSvgExtraStates &states, bool defaultViewCoord) const override;
    QSvgNode *getFillPattern() override;
    void updateFillPattern(QSvgNode* fillPattern) override;

    const QRectF &rect() const { return m_rect; }
    int x() const { return m_rx; }
    int y() const { return m_ry; }
private:
    QRectF m_rect;
    int m_rx, m_ry;
    QSvgPattern *m_fillPattern;
};

class  QSvgTspan;

class Q_SVG_PRIVATE_EXPORT QSvgText : public QSvgNode
{
public:
    enum WhitespaceMode
    {
        Default,
        Preserve
    };

    QSvgText(QSvgNode *parent, const QPointF &coord);
    QSvgText(const QSvgText &other);
    ~QSvgText();
    void setTextArea(const QSizeF &size);
    void setCoord(const QPointF &coord) { m_coord = coord; };
    void clearTspans();

    void draw(QPainter *p, QSvgExtraStates &states) override;
    Type type() const override;

    QSvgNode *clone(QSvgNode *parent) override;

    void addTspan(QSvgTspan *tspan) {m_tspans.append(tspan);}
    void addText(const QString &text);
    void addLineBreak() {m_tspans.append(LINEBREAK);}
    void setWhitespaceMode(WhitespaceMode mode) {m_mode = mode;}

    const QPointF &coord() const { return m_coord; }
    const QSizeF &size() const { return m_size; }
    const QVector<QSvgTspan *> &tspans() const { return m_tspans; }
    // QRectF bounds(QPainter *p, QSvgExtraStates &states, bool defaultViewCoord) const override;

private:
    qreal lineWidth(QString graph, QTextLayout::FormatRange formatRange, qreal scale,
                    const QVector<QPointF> &offsets, const QSvgFont *svgFont);
    void drawLine(QPainter *p, QSvgExtraStates &states, QString graph, QPointF pos,
                  QTextLayout::FormatRange formatRange, const QVector<QPointF> &offsets,
                  QPointF &nextPos);
    void drawCharacters(QPainter *p, QString characters, QPointF pos,
                       QTextLayout::FormatRange formatRange, QPointF &nextPos);

    void processTspansCoords(QSvgTspan *tspan, 
                             QVector<qreal> &parentXCoords, QVector<qreal> &parentYCoords,
                             QVector<qreal> &parentoffsetX, QVector<qreal> &parentoffsetY);
    void processTspansFormats(QSvgTspan *tspan, QPainter *p, QSvgExtraStates &states, qreal scale);

    void resolveTspans(QPainter *p, QSvgExtraStates &states, qreal scale);

 private:
    static QSvgTspan * const LINEBREAK;

    QPointF m_coord;
    // 'm_tspans' is also used to store characters outside tspans and line breaks.
    // If a 'm_tspan' item is null, it indicates a line break.
    QVector<QSvgTspan *> m_tspans;
    QVector<QString> m_paragraphs;
    QVector<QTextLayout::FormatRange> m_formatRanges;
    QVector<LineCoords> m_paragraphCoords;

    bool m_resolved;
    Type m_type;
    QSizeF m_size;
    WhitespaceMode m_mode;
};

class Q_SVG_PRIVATE_EXPORT QSvgTspan : public QSvgNode
{
public:
    QSvgTspan(QSvgNode *parent) : QSvgNode(parent), m_mode(QSvgText::Default), m_segments(0){ }
    QSvgTspan(const QSvgTspan &other);
    ~QSvgTspan();
    Type type() const override { return TSPAN; }
    void draw(QPainter *, QSvgExtraStates &) override { Q_ASSERT(!"Tspans should be drawn through QSvgText::draw()."); }
    QSvgNode *clone(QSvgNode *parent) override;
    void addText(const QString &text) {m_text += text;}
    const QString &text() const {return m_text;}
    void setWhitespaceMode(QSvgText::WhitespaceMode mode) {m_mode = mode;}
    QSvgText::WhitespaceMode whitespaceMode() const {return m_mode;}
    void setSegments(int segments) { m_segments = segments; }
    int segments() const { return m_segments; }

    void addChild(QSvgTspan *child);
    void setCoordAndOffset(const QVector<qreal> &coordX, const QVector<qreal> &coordY,
                           const QVector<qreal> &offsetX, const QVector<qreal> &offsetY);
    const QList<QSvgTspan *> &renderers() const { return m_renderers; }
    const QVector<qreal> &coordX() const { return m_coordX; }
    const QVector<qreal> &coordY() const { return m_coordY; }
    const QVector<qreal> &offsetX() const { return m_offsetX; }
    const QVector<qreal> &offsetY() const { return m_offsetY; }

    void setCoordAndOffsetType(const QVector<int> &coordXType, const QVector<int> &coordYType,
                               const QVector<int> &offsetXType, const QVector<int> &offsetYType);
    void updateOffsetY(QPainter *p, QSvgExtraStates &states);

private:
    QList<QSvgTspan *> m_renderers;
    QString m_text;
    QSvgText::WhitespaceMode m_mode;
    int m_segments;

    QVector<qreal> m_coordX;
    QVector<qreal> m_coordY;
    QVector<qreal> m_offsetX;
    QVector<qreal> m_offsetY;

    QVector<int> m_coordXType;
    QVector<int> m_coordYType;
    QVector<int> m_offsetXType;
    QVector<int> m_offsetYType;
};

class QSvgUse : public QSvgNode
{
public:
    QSvgUse(const QPointF &start, QSvgNode *parent, QSvgNode *link);
    QSvgUse(const QPointF &start, QSvgNode *parent, const QString &linkId)
        : QSvgUse(start, parent, nullptr)
    { m_linkId = linkId; }
    QSvgUse(const QSvgUse &other);
    void draw(QPainter *p, QSvgExtraStates &states) override;
    Type type() const override;
    QSvgNode *clone(QSvgNode *parent) override;
    QRectF bounds(QPainter *p, QSvgExtraStates &states, bool defaultViewCoord) const override;
    bool isResolved() const { return m_link != nullptr; }
    const QString linkId() const
    {
        return m_linkId.isEmpty() && isResolved() ? m_link->nodeId() : m_linkId;
    }
    void setLink(QSvgNode *link) { m_link = link; }
    const QPointF &start() const { return m_start; }

private:
    QSvgNode *m_link;
    QPointF   m_start;
    QString   m_linkId;
    mutable bool m_recursing;
};

class QSvgVideo : public QSvgNode
{
public:
    void draw(QPainter *p, QSvgExtraStates &states) override;
    Type type() const override;
    QSvgNode *clone(QSvgNode *parent) override;
};

QT_END_NAMESPACE

#endif // QSVGGRAPHICS_P_H
