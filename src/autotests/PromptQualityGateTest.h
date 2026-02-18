/*
    SPDX-FileCopyrightText: 2025 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef PROMPTQUALITYGATETEST_H
#define PROMPTQUALITYGATETEST_H

#include <QObject>

namespace Konsolai
{

class PromptQualityGateTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testVaguePrompt();
    void testSpecificPrompt();
    void testMediumPrompt();
    void testFileDetection();
    void testAcceptanceCriteria();
    void testBoundedScope();
    void testSuggestions();
    void testTemplateInstantiation();
    void testBuiltinTemplates();
    void testTemplateSerialization();
    void testYoloLevelEstimation();
    void testEmptyPrompt();
};

} // namespace Konsolai

#endif // PROMPTQUALITYGATETEST_H
