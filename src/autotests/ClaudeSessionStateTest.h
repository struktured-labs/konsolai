/*
    SPDX-FileCopyrightText: 2025 Struktured Labs

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef CLAUDESESSIONSTATETEST_H
#define CLAUDESESSIONSTATETEST_H

#include <QObject>

namespace Konsolai
{

class ClaudeSessionStateTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();

    // Validity tests
    void testDefaultStateInvalid();
    void testValidState();
    void testInvalidWithEmptySessionName();
    void testInvalidWithEmptySessionId();

    // JSON serialization tests
    void testToJson();
    void testFromJson();
    void testJsonRoundTrip();
    void testFromJsonMissingFields();
    void testFromJsonInvalidData();

    // Equality tests
    void testEquality();
    void testInequalityByName();

    // Property tests
    void testTimestamps();
    void testAttachmentStatus();

    // Auto-continue prompt persistence
    void testAutoContinuePromptSerialization();
    void testAutoContinuePromptRoundTrip();
    void testAutoContinuePromptEmptyNotInJson();
    void testAutoContinuePromptMissingFromJson();
};

}

#endif // CLAUDESESSIONSTATETEST_H
