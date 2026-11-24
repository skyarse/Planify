#pragma once

#include <QColor>
#include <QIcon>
#include <QString>
#include <QWidget>

class QPainter;
class QPushButton;

enum class Mark {
    Inbox, Today, Calendar, Tag, Pin, Complete, Search, Menu, Split, Sort, More,
    Cloud, Home, Gear, Colors, Quick, Backup, Help, Plus, Minus, Maximize, Close, Back, Sync, Project, Circle, Attachment, Flag, Clock, History
};

void drawMark(QPainter &painter, Mark mark, const QRectF &rect, QColor color, qreal stroke = 1.8);
QIcon markIcon(Mark mark, QColor color);
QPushButton *iconButton(QWidget *parent, Mark mark, const QString &label, const QString &style = "tool");
