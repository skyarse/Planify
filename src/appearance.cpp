#include "appearance.h"

#include <QEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QVariant>

void drawMark(QPainter &p, Mark mark, const QRectF &rect, QColor color, qreal stroke)
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.translate(rect.topLeft());
    p.scale(rect.width() / 24.0, rect.height() / 24.0);
    p.setPen(QPen(color, stroke, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    auto path = [&](std::initializer_list<QPointF> points, bool closed = false) {
        auto it = points.begin();
        QPainterPath shape(*it++);
        while (it != points.end())
            shape.lineTo(*it++);
        if (closed)
            shape.closeSubpath();
        p.drawPath(shape);
    };
    switch (mark) {
    case Mark::Inbox:
        p.drawRoundedRect(QRectF(3, 4, 18, 16), 2, 2);
        path({{3,12},{8,12},{9.5,15},{14.5,15},{16,12},{21,12}});
        break;
    case Mark::Today:
        path({{12,3},{14.7,9},{21,9.7},{16.3,14},{17.5,20},{12,17},{6.5,20},{7.7,14},{3,9.7},{9.3,9}}, true);
        break;
    case Mark::Calendar:
        p.drawRoundedRect(QRectF(4,5,16,16), 2, 2);
        p.drawLine(QPointF(4,9), QPointF(20,9));
        p.drawLine(QPointF(8,3), QPointF(8,6));
        p.drawLine(QPointF(16,3), QPointF(16,6));
        for (int y : {13,17})
            for (int x : {8,12,16}) p.drawPoint(x,y);
        break;
    case Mark::Tag:
        path({{3,4},{11,4},{21,14},{14,21},{3,10}}, true);
        p.drawEllipse(QPointF(7.5,8), .8, .8);
        break;
    case Mark::Pin:
        path({{8,4},{16,4},{15,10},{18,14},{6,14},{9,10}}, true);
        p.drawLine(12,14,12,21);
        break;
    case Mark::Complete:
        p.drawArc(QRectF(2.5,2.5,19,19), 60*16, 300*16);
        path({{8,12},{12,16},{21.5,5.5}});
        break;
    case Mark::Search:
        p.drawEllipse(QRectF(4,4,11,11));
        p.drawLine(14,14,20,20);
        break;
    case Mark::Menu:
        for (int y : {6,12,18}) p.drawLine(4,y,20,y);
        break;
    case Mark::Split:
        p.drawRoundedRect(QRectF(3,4,18,16), 2, 2);
        p.drawLine(10,4,10,20);
        break;
    case Mark::Sort:
        p.drawLine(3,6,13,6);
        p.drawLine(3,10,11,10);
        p.drawLine(3,14,8,14);
        path({{17,4},{17,21}});
        path({{14,18},{17,21},{20,18}});
        break;
    case Mark::More:
        for (int y : {6,12,18}) p.drawPoint(12,y);
        break;
    case Mark::Cloud: {
        QPainterPath cloud({5,18});
        cloud.cubicTo(0,18,1,11,7,12);
        cloud.cubicTo(6,5,17,4,18,11);
        cloud.cubicTo(23,11,24,18,19,18);
        cloud.closeSubpath();
        p.drawPath(cloud);
        break;
    }
    case Mark::Home:
        path({{3,11},{12,3},{21,11}});
        path({{6,10},{6,21},{10,21},{10,15},{14,15},{14,21},{18,21},{18,10}});
        break;
    case Mark::Gear:
        p.drawEllipse(QPointF(12,12), 6,6);
        p.drawEllipse(QPointF(12,12), 2,2);
        for (int i=0; i<8; ++i) {
            p.drawLine(12,3,12,6);
            p.translate(12,12); p.rotate(45); p.translate(-12,-12);
        }
        break;
    case Mark::Colors:
        for (QPointF c : {QPointF(12,6), QPointF(7,16), QPointF(17,16)})
            p.drawEllipse(c,4,4);
        break;
    case Mark::Quick:
        path({{12,4},{5,4},{5,20},{12,20}});
        path({{15,4},{19,4},{19,8}});
        p.drawLine(13,14,21,14);
        p.drawLine(17,10,17,18);
        break;
    case Mark::Backup:
        p.drawEllipse(QRectF(3,3,18,18));
        p.drawLine(12,7,12,16);
        path({{8,13},{12,17},{16,13}});
        break;
    case Mark::Help:
        p.drawEllipse(QRectF(3,3,18,18));
        p.drawEllipse(QRectF(8,8,8,8));
        p.drawLine(5,5,8,8); p.drawLine(16,16,19,19);
        p.drawLine(5,19,8,16); p.drawLine(16,8,19,5);
        break;
    case Mark::Plus: p.drawLine(4,12,20,12); p.drawLine(12,4,12,20); break;
    case Mark::Minus: path({{5,9},{12,16},{19,9}}); break;
    case Mark::Maximize: path({{5,15},{12,8},{19,15}}); break;
    case Mark::Close: p.drawLine(6,6,18,18); p.drawLine(18,6,6,18); break;
    case Mark::Back: path({{15,5},{8,12},{15,19}}); break;
    case Mark::Sync:
        p.drawArc(QRectF(4,4,16,16), 24*16, 132*16);
        p.drawArc(QRectF(4,4,16,16), 204*16, 132*16);
        path({{4,4},{4,9},{9,9}});
        path({{20,20},{20,15},{15,15}});
        break;
    case Mark::Circle:
        p.drawEllipse(QRectF(3, 3, 18, 18));
        break;
    case Mark::Attachment: {
        QPainterPath clip({7, 11});
        clip.lineTo(7, 17);
        clip.cubicTo(7, 23, 17, 23, 17, 17);
        clip.lineTo(17, 7);
        clip.cubicTo(17, 2, 10, 2, 10, 7);
        clip.lineTo(10, 16);
        clip.cubicTo(10, 19, 14, 19, 14, 16);
        clip.lineTo(14, 8);
        p.drawPath(clip);
        break;
    }
    case Mark::Flag:
        p.drawLine(5, 4, 5, 21);
        path({{5, 5}, {11, 5}, {14, 8}, {20, 8}, {20, 15}, {14, 15}, {11, 12}, {5, 12}});
        break;
    case Mark::Clock:
        p.drawEllipse(QRectF(3, 3, 18, 18));
        path({{12, 7}, {12, 12}, {16, 14}});
        break;
    case Mark::History:
        p.drawArc(QRectF(4, 4, 16, 16), 0, 285 * 16);
        path({{3, 21}, {3, 15}, {9, 15}});
        path({{12, 7}, {12, 12}, {15, 14}});
        break;
    case Mark::Project:
        p.drawEllipse(QRectF(4,4,16,16));
        p.drawEllipse(QRectF(8,8,8,8));
        break;
    }
    p.restore();
}

QIcon markIcon(Mark mark, QColor color)
{
    QPixmap pixmap(48,48);
    pixmap.setDevicePixelRatio(2);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    drawMark(painter, mark, QRectF(0,0,24,24), color);
    painter.end();
    return QIcon(pixmap);
}

namespace {
class IconButton : public QPushButton {
public:
    IconButton(Mark mark, QWidget *parent) : QPushButton(parent), mark(mark) {}

protected:
    void paintEvent(QPaintEvent *event) override
    {
        QPushButton::paintEvent(event);
        const int size = objectName() == "addTask" ? 24 : 18;
        auto ink = palette().color(QPalette::WindowText);
        if (objectName() == "addTask") ink = Qt::white;
        QPainter painter(this);
        drawMark(painter, mark, QRectF((width()-size)/2.0, (height()-size)/2.0, size, size), ink);
    }

private:
    Mark mark;
};
}

QPushButton *iconButton(QWidget *parent, Mark mark, const QString &label, const QString &style)
{
    auto *button = new IconButton(mark, parent);
    button->setObjectName(style);
    button->setAccessibleName(label);
    button->setToolTip(label);
    button->setCursor(Qt::PointingHandCursor);
    button->setFixedSize(style == "addTask" ? QSize(50,50) : QSize(28,28));
    return button;
}
