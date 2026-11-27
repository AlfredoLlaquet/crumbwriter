#include "documentformats.h"
#include <QObject>
#import <PDFKit/PDFKit.h>

QString DocumentFormats::fromPdf(const QString &path, QString *error)
{
    @autoreleasepool {
        PDFDocument *pdf = [[PDFDocument alloc] initWithURL:[NSURL fileURLWithPath:path.toNSString()]];
        if (!pdf || [pdf isLocked]) {
            *error = QObject::tr("The PDF could not be opened or requires a password.");
            [pdf release];
            return {};
        }
        const QString result = QString::fromNSString([pdf string]);
        [pdf release];
        return result;
    }
}
