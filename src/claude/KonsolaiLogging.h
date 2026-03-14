/*
    SPDX-FileCopyrightText: 2025 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef KONSOLAILOGGING_H
#define KONSOLAILOGGING_H

#include <QLoggingCategory>

// Runtime-filterable logging category for the Konsolai Claude module.
// Enable debug output:  export QT_LOGGING_RULES="konsolai.claude.debug=true"
// Disable all output:   export QT_LOGGING_RULES="konsolai.claude=false"
Q_DECLARE_LOGGING_CATEGORY(KonsolaiLog)

#endif // KONSOLAILOGGING_H
