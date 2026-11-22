#pragma once

#include <QLocale>

namespace ManuscriptNumbers {
inline QLocale locale()
{
    QLocale result;
    if (result.language() == QLocale::C) result = QLocale(QLocale::English, QLocale::UnitedStates);
    result.setNumberOptions(result.numberOptions() & ~QLocale::OmitGroupSeparator);
    return result;
}

inline QString grouped(QString text, const QLocale &locale)
{
    // Some locales first group at five digits. Manuscript figures group at four.
    const qsizetype decimal = text.indexOf(locale.decimalPoint());
    const qsizetype end = decimal < 0 ? text.size() : decimal;
    const qsizetype sign = text.startsWith(locale.negativeSign()) ? locale.negativeSign().size() : 0;
    const qsizetype digit = locale.zeroDigit().size();
    if (end - sign == 4 * digit && !text.contains(locale.groupSeparator()))
        text.insert(end - 3 * digit, locale.groupSeparator());
    return text;
}

inline QString format(qint64 value)
{
    const auto numbers = locale();
    return grouped(numbers.toString(value), numbers);
}

inline QString fixed(double value, int decimals)
{
    const auto numbers = locale();
    return grouped(numbers.toString(value, 'f', decimals), numbers);
}
}
