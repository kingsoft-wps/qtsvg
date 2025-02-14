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

#ifndef QSVGTINYDOCUMENT_P_H
#define QSVGTINYDOCUMENT_P_H

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

#include "qsvgstructure_p.h"
#include "qtsvgglobal_p.h"

#include "QtCore/qrect.h"
#include "QtCore/qlist.h"
#include "QtCore/qhash.h"
#include "QtCore/qdatetime.h"
#include "QtCore/qxmlstream.h"
#include "qsvgstyle_p.h"
#include "qsvgfont_p.h"

QT_BEGIN_NAMESPACE

class QPainter;
class QByteArray;
class QSvgFont;

class Q_SVG_PRIVATE_EXPORT QSvgProp
{
public:
    QSvgProp() = default;
    virtual ~QSvgProp() = default;
};

class Q_SVG_PRIVATE_EXPORT QSvgTinyDocument : public QSvgStructureNode
{
public:
    static QSvgTinyDocument *load(const QString &file);
    static QSvgTinyDocument *load(const QByteArray &contents);
    static QSvgTinyDocument *load(QXmlStreamReader *contents);
    static QSvgTinyDocument *load(const QString &file,
                                  const QMap<QString, QMap<QString, QVariant>> &classProperties);

public:
    QSvgTinyDocument(QSvgNode *parent = nullptr);
    QSvgTinyDocument(const QSvgTinyDocument &other);
    ~QSvgTinyDocument();
    Type type() const override;

    virtual QSvgNode *clone(QSvgNode *parent) override;

    void setCoord(const QPointF &coord);
    const QPointF &coord() const;

    QSize size() const;
    void setWidth(int len, bool percent);
    void setHeight(int len, bool percent);
    int width() const;
    int height() const;
    bool widthPercent() const;
    bool heightPercent() const;

    bool preserveAspectRatio() const;

    bool viewBoxValid() const;
    QRectF viewBox() const;
    void setViewBox(const QRectF &rect);

    void draw(QPainter *p, QSvgExtraStates &) override; // from the QSvgNode

    void draw(QPainter *p);
    void draw(QPainter *p, const QRectF &bounds, const QRectF &source);
    void draw(QPainter *p, const QString &id, const QRectF &bounds = QRectF());
    void draw(QPainter *p, const QRectF &bounds, const QRectF &source,
              std::function<QPixmap(QPainter*, int, int)> createPixmapBuffer,
              std::function<QPixmap(QPainter*, const QImage &img)> convertFunc);

    QMatrix matrixForElement(const QString &id) const;
    QRectF boundsOnElement(const QString &id) const;
    bool elementExists(const QString &id) const;

    void addSvgFont(QSvgFont *);
    QSvgFont *svgFont(const QString &family) const;
    void addNamedNode(const QString &id, QSvgNode *node);
    QSvgNode *namedNode(const QString &id) const;
    void addNamedStyle(const QString &id, QSvgFillStyleProperty *style);
    QSvgFillStyleProperty *namedStyle(const QString &id) const;

    const QHash<QString, QSvgRefCounter<QSvgFont>> &namedFonts() const;
    const QHash<QString, QSvgRefCounter<QSvgFillStyleProperty>> &namedStyles() const;

    void restartAnimation();
    int currentElapsed() const;
    bool animated() const;
    void setAnimated(bool a);
    int animationDuration() const;
    int currentFrame() const;
    void setCurrentFrame(int);
    void setFramesPerSecond(int num);

    void appendXmlClass(const QStringList &xmlClasses);
    QStringList xmlClassList();

    void setSvgProp(const QSharedPointer<QSvgProp> &svgProp);
    QSvgProp *getSvgProp();

    QPixmap createPixmapBuffer(QPainter *p, int, int);
    QPixmap convertToPixmap(QPainter *p, const QImage &img);

private:
    void mapSourceToTarget(QPainter *p, const QRectF &targetRect,
                           const QRectF &sourceRect = QRectF());

private:
    QPointF m_coord;
    QSize m_size;
    bool m_widthPercent;
    bool m_heightPercent;
    bool m_firstRender;
    bool m_animated;

    mutable QRectF m_viewBox;

    QHash<QString, QSvgRefCounter<QSvgFont>> m_fonts;
    QHash<QString, QSvgNode *> m_namedNodes;
    QHash<QString, QSvgRefCounter<QSvgFillStyleProperty>> m_namedStyles;

    QStringList m_xmlClassList;
    QTime m_time;
    int m_animationDuration;
    int m_fps;

    QSvgExtraStates m_states;
    QSharedPointer<QSvgProp> m_svgProp;
    std::function<QPixmap(QPainter*, int, int)> m_createPixmapBufferFun = nullptr;
    std::function<QPixmap(QPainter*, const QImage &img)> m_convertToPixmapFun = nullptr;
};

inline void QSvgTinyDocument::setCoord(const QPointF &coord)
{
    m_coord = coord;
}

inline const QPointF &QSvgTinyDocument::coord() const
{
    return m_coord;
}

inline QSize QSvgTinyDocument::size() const
{
    if (m_size.isEmpty())
        return viewBox().size().toSize();
    if (m_widthPercent || m_heightPercent) {
        const int width = m_widthPercent ? qRound(0.01 * m_size.width() * viewBox().size().width())
                                         : m_size.width();
        const int height = m_heightPercent
                ? qRound(0.01 * m_size.height() * viewBox().size().height())
                : m_size.height();
        return QSize(width, height);
    }
    return m_size;
}

inline int QSvgTinyDocument::width() const
{
    return size().width();
}

inline int QSvgTinyDocument::height() const
{
    return size().height();
}

inline bool QSvgTinyDocument::widthPercent() const
{
    return m_widthPercent;
}

inline bool QSvgTinyDocument::heightPercent() const
{
    return m_heightPercent;
}

inline bool QSvgTinyDocument::viewBoxValid() const
{
    return m_viewBox.isValid();
}

inline QRectF QSvgTinyDocument::viewBox() const
{
    if (m_viewBox.isNull())
        m_viewBox = transformedBounds();

    return m_viewBox;
}

inline bool QSvgTinyDocument::preserveAspectRatio() const
{
    return false;
}

inline int QSvgTinyDocument::currentElapsed() const
{
    // set -1 because animation could from 0
    return m_animated ? m_time.elapsed() : -1;
}

inline int QSvgTinyDocument::animationDuration() const
{
    return m_animationDuration;
}

inline const QHash<QString, QSvgRefCounter<QSvgFont>> &QSvgTinyDocument::namedFonts() const
{
    return m_fonts;
}

inline const QHash<QString, QSvgRefCounter<QSvgFillStyleProperty>> &
QSvgTinyDocument::namedStyles() const
{
    return m_namedStyles;
}

inline void QSvgTinyDocument::setSvgProp(const QSharedPointer<QSvgProp> &svgProp)
{
    m_svgProp = svgProp;
}

inline QSvgProp *QSvgTinyDocument::getSvgProp()
{
    return m_svgProp.get();
}

QT_END_NAMESPACE

#endif // QSVGTINYDOCUMENT_P_H
