/*
    SPDX-FileCopyrightText: 2025 Struktured Labs

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef SESSIONMANAGERPANELTEST_H
#define SESSIONMANAGERPANELTEST_H

#include <QObject>

namespace Konsolai
{

class SessionManagerPanelTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();
    void cleanup();

    // Metadata filtering
    void testAllSessionsEmpty();
    void testAllSessionsLoaded();
    void testPinnedSessionsFilter();
    void testArchivedSessionsFilter();
    void testPinnedExcludesArchived();

    // Pin/Unpin
    void testPinSession();
    void testUnpinSession();
    void testPinNonexistentSession();

    // Archive
    void testArchiveSession();
    void testArchiveNonexistentSession();

    // Close (new feature)
    void testCloseSessionNotArchived();
    void testCloseNonexistentSession();

    // Mark expired
    void testMarkExpired();
    void testMarkExpiredUnknownSession();

    // Collapsed state
    void testCollapsedToggle();
    void testCollapsedSignal();
    void testCollapsedIdempotent();

    // Metadata persistence round-trip
    void testMetadataPersistence();
    void testMetadataYoloPersistence();
    void testMetadataSshFields();
};

}

#endif // SESSIONMANAGERPANELTEST_H
