#include "manuscriptlinks.h"

#include <QDesktopServices>
#include <QMessageBox>
#include <glib.h>
extern "C" {
#include "../crumb.h"
}

QVector<ManuscriptLinks::Link> ManuscriptLinks::findAll(const QString &line)
{
    QVector<Link> links;
    if (!line.contains(QStringLiteral("://")) && !line.contains(QStringLiteral("www."), Qt::CaseInsensitive))
        return links;

    const QByteArray utf8 = line.toUtf8();
    int from = 0, column = 0, start, end;
    while (cw_next_web_link(utf8.constData(), utf8.size(), from, &start, &end)) {
        // Advance through the line once. A supplementary letter occupies two
        // Qt positions, even if it precedes the first address in the paragraph.
        column += QString::fromUtf8(utf8.constData() + from, start - from).size();
        const QString source = QString::fromUtf8(utf8.constData() + start, end - start);
        const int limit = column + int(source.size());
        const bool bare = source.startsWith(QStringLiteral("www."), Qt::CaseInsensitive);
        const QUrl target(bare ? QStringLiteral("https://") + source : source, QUrl::StrictMode);
        if (target.isValid() && !target.host().isEmpty())
            links.append({column, limit, target});
        from = end;
        column = limit;
    }
    return links;
}

QUrl ManuscriptLinks::at(const QString &line, int column, bool includeEnd)
{
    for (const auto &link : findAll(line)) {
        if (column >= link.start && (column < link.end || (includeEnd && column == link.end)))
            return link.target;
    }
    return {};
}

void ManuscriptLinks::open(const QUrl &target, QWidget *parent)
{
    if (QDesktopServices::openUrl(target)) return;
    auto *notice = new QMessageBox(QMessageBox::Warning, QObject::tr("Open Link"),
        QObject::tr("The default browser could not open this link."), QMessageBox::Close, parent);
    notice->setAttribute(Qt::WA_DeleteOnClose);
    notice->setWindowModality(Qt::NonModal);
    notice->show();
}
