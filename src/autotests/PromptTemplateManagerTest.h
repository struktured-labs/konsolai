/*
    SPDX-FileCopyrightText: 2025 Struktured Labs

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef PROMPTTEMPLATEMANAGERTEST_H
#define PROMPTTEMPLATEMANAGERTEST_H

#include <QObject>

namespace Konsolai
{

class PromptTemplateManagerTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();
    void cleanup();

    // Builtin templates
    void testBuiltinTemplatesNonEmpty();
    void testBuiltinTemplateIds();
    void testBuiltinTemplateFields();

    // User templates
    void testUserTemplatesInitiallyEmpty();
    void testSaveAndLoadUserTemplate();

    // Instantiation
    void testInstantiateBasic();
    void testInstantiateMultipleFields();
    void testInstantiateUnusedFieldsIgnored();
    void testInstantiateMissingFieldsLeftAsPlaceholder();

    // JSON serialization
    void testToJsonRoundTrip();
    void testFromJsonMissingFields();

    // allTemplates
    void testAllTemplatesIncludesBuiltinAndUser();
};

}

#endif // PROMPTTEMPLATEMANAGERTEST_H
