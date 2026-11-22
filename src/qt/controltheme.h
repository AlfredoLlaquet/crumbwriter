#pragma once

#include <QPalette>
#include <QString>

// Apply the shared desktop, light, or dark preference and return the paper palette.
QPalette configureControlTheme(const QString &choice);
