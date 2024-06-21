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

#ifndef QSVGSTRUCTURE_P_H
#define QSVGSTRUCTURE_P_H

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

#include "QtCore/qlist.h"
#include "QtCore/qhash.h"

QT_BEGIN_NAMESPACE

class QSvgTinyDocument;
class QSvgNode;
class QPainter;
class QSvgDefs;

class Q_SVG_PRIVATE_EXPORT QSvgStructureNode : public QSvgNode
{
public:
    QSvgStructureNode(QSvgNode *parent);
    QSvgStructureNode(const QSvgStructureNode &other);
    ~QSvgStructureNode();
    QSvgNode *scopeNode(const QString &id) const;
    void addChild(QSvgNode *child, const QString &id);
    QRectF bounds(QPainter *p, QSvgExtraStates &states, bool defaultViewCoord) const override;
    QSvgNode *previousSiblingNode(QSvgNode *n) const;
    const QList<QSvgNode*>& renderers() const { return m_renderers; }
protected:
    QList<QSvgNode*>          m_renderers;
    QHash<QString, QSvgNode*> m_scope;
    QList<QSvgStructureNode*> m_linkedScopes;
    mutable bool              m_recursing = false;
};

class Q_SVG_PRIVATE_EXPORT QSvgG : public QSvgStructureNode
{
public:
    QSvgG(QSvgNode *parent);
    void draw(QPainter *p, QSvgExtraStates &states) override;
    QSvgNode *clone(QSvgNode *parent) override;
    Type type() const override;
};

class Q_SVG_PRIVATE_EXPORT QSvgDefs : public QSvgStructureNode
{
public:
    QSvgDefs(QSvgNode *parent);
    void draw(QPainter *p, QSvgExtraStates &states) override;
    QSvgNode *clone(QSvgNode *parent) override;
    Type type() const override;
};

class Q_SVG_PRIVATE_EXPORT QSvgSwitch : public QSvgStructureNode
{
public:
    QSvgSwitch(QSvgNode *parent);
    QSvgSwitch(const QSvgSwitch& other);
    void draw(QPainter *p, QSvgExtraStates &states) override;
    QSvgNode *clone(QSvgNode *parent) override;
    Type type() const override;
private:
    void init();
private:
    QString m_systemLanguage;
    QString m_systemLanguagePrefix;
};

class Q_SVG_PRIVATE_EXPORT QSvgMarker : public QSvgStructureNode
{
public:
    QSvgMarker(QSvgNode *parent);
    void draw(QPainter *p, QSvgExtraStates &states) override;
    QSvgNode *clone(QSvgNode *parent) override;
    Type type() const override;

public:
    enum MarkerUnits 
    { 
        userSpaceOnUse,
        strokeWidth 
    };

    bool viewBoxValid() const;
    const QRectF& viewBox() const;
    void setViewBox(const QRectF &rect) { m_viewBox = rect; }

    MarkerUnits unitsMode() const { return m_unitsMode; }
    void setUnitsMode(MarkerUnits unitsMode) { m_unitsMode = unitsMode; }

    const QPointF& ref() const { return m_ref; }
    void setRef(const QPointF &point) { m_ref = point; }

    bool isAutoOrient() const { return m_bAutoOrient; }
    qreal orientAngle() const { return m_orientAngle; }
    void enableAutoOrient(bool bAuto) { m_bAutoOrient = bAuto; }
    void setOrientAngle(qreal angle);

    const QSize& size() const { return m_size; }
    void setSize(const QSize &point) { m_size = point; }

    void draw(QPainter *p, QSvgExtraStates &states, const QPointF &origin, qreal angle, qreal strokeWidth);

private:
    bool m_bAutoOrient;
    MarkerUnits m_unitsMode;
    mutable QRectF m_viewBox;
    QPointF m_ref;
    qreal m_orientAngle;
    QSize m_size;
};

class Q_SVG_PRIVATE_EXPORT QSvgPattern : public QSvgStructureNode
{
public:
    enum PatternUnits
    {
        objectBoundingBox,
        userSpaceOnUse
    };

    QSvgPattern(QSvgNode *parent, const QRectF &bounds, PatternUnits units, PatternUnits contentUnits);
    void draw(QPainter *p, QSvgExtraStates &states) override;
    void drawTile(QPainter *p, QSvgExtraStates &states);
    QRectF rect4DrawTile(qreal fPatternX, qreal fPatternY);
    QPixmap patternContentPixmap(QPainter *p, QSvgExtraStates &states);
    QSvgNode *clone(QSvgNode *parent) override;
    Type type() const override;
    void setClipPath(const QPainterPath &path);

    const PatternUnits &patternContentUnits() const { return m_patternContentUnits; }
    const PatternUnits &patternUnits() const { return m_patternUnits; }

    const QRectF &viewBox() const { return m_viewBox; }
    void setViewBox(const QRectF &rect) { m_viewBox = rect; }

    const QRectF &bounds() const { return m_bounds; }
    const QRectF &ratioBounds() const { return m_ratioBounds; }
private:
    QRectF m_bounds;
    QRectF m_ratioBounds;
    QRectF m_viewBox;
    QPainterPath m_clipPath;
    PatternUnits m_patternUnits;
    PatternUnits m_patternContentUnits;
};

class Q_SVG_PRIVATE_EXPORT QSvgClipPath : public QSvgStructureNode
{
public:
    QSvgClipPath(QSvgNode *parent);
    void draw(QPainter *p, QSvgExtraStates &states) override;
    QSvgNode *clone(QSvgNode *parent) override;
    Type type() const override;

public:
    enum CoordinateMode 
    { 
        userSpaceOnUse, 
        objectBoundingBox 
    };

    CoordinateMode getCoordinateMode() const { return m_coordinateMode; }
    void setCoordinateMode(CoordinateMode mode) { m_coordinateMode = mode; }

    void parseClipPathList();
    const QVector<QPainterPath> &getClipPathList() const { return m_pathList; }

    bool isParsed() const { return m_bParsed; }

private:
    bool m_bParsed;
    CoordinateMode m_coordinateMode;
    QVector<QPainterPath> m_pathList;
};

QT_END_NAMESPACE

#endif // QSVGSTRUCTURE_P_H
