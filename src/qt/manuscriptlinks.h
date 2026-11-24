#pragma once

#include <QString>
#include <QUrl>
#include <QVector>

class QWidget;

namespace ManuscriptLinks {
struct Link {
    int start; // Half-open UTF-16 bounds in the original manuscript line.
    int end;
    QUrl target;
};

QVector<Link> findAll(const QString &line);
// A caret just after a URL can follow it. A mouse hit uses the character
// directly under the pointer and must stay inside the half-open range.
QUrl at(const QString &line, int column, bool includeEnd);
void open(const QUrl &target, QWidget *parent);
}
