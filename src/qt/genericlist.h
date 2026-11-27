#pragma once

#include <QString>
class QTextBlock;
class QTextCursor;

namespace GenericList {
// Read at most the item and its immediate predecessor. An explicit request
// also permits a lone bare letter or Roman numeral.
QString nextPrefix(const QTextBlock &item, bool explicitRequest = false);
void insertPrefix(QTextCursor &cursor, const QString &prefix);
void resolveTypedPrefix(QTextCursor &cursor);
void replaceGeneratedPrefixForPaste(QTextCursor &cursor, const QString &text);
bool selectEmptyGeneratedPrefix(QTextCursor &cursor);
}
