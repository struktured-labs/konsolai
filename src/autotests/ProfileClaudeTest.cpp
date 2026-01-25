/*
    SPDX-FileCopyrightText: 2025 Struktured Labs

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// Own
#include "ProfileClaudeTest.h"

// Qt
#include <QStandardPaths>
#include <QTest>

// Konsole
#include "../profile/Profile.h"

using namespace Konsole;

void ProfileClaudeTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
}

void ProfileClaudeTest::cleanupTestCase()
{
}

void ProfileClaudeTest::testClaudeEnabledDefault()
{
    Profile::Ptr profile(new Profile);

    // Default should be false (Claude disabled)
    QVERIFY(!profile->isPropertySet(Profile::ClaudeEnabled));
    QCOMPARE(profile->property<bool>(Profile::ClaudeEnabled), false);
}

void ProfileClaudeTest::testClaudeEnabledSet()
{
    Profile::Ptr profile(new Profile);

    profile->setProperty(Profile::ClaudeEnabled, true);

    QVERIFY(profile->isPropertySet(Profile::ClaudeEnabled));
    QCOMPARE(profile->property<bool>(Profile::ClaudeEnabled), true);

    profile->setProperty(Profile::ClaudeEnabled, false);
    QCOMPARE(profile->property<bool>(Profile::ClaudeEnabled), false);
}

void ProfileClaudeTest::testClaudeTmuxPersistenceDefault()
{
    Profile::Ptr profile(new Profile);

    // Default should be true (tmux persistence enabled)
    QVERIFY(!profile->isPropertySet(Profile::ClaudeTmuxPersistence));
}

void ProfileClaudeTest::testClaudeTmuxPersistenceSet()
{
    Profile::Ptr profile(new Profile);

    profile->setProperty(Profile::ClaudeTmuxPersistence, true);

    QVERIFY(profile->isPropertySet(Profile::ClaudeTmuxPersistence));
    QCOMPARE(profile->property<bool>(Profile::ClaudeTmuxPersistence), true);

    profile->setProperty(Profile::ClaudeTmuxPersistence, false);
    QCOMPARE(profile->property<bool>(Profile::ClaudeTmuxPersistence), false);
}

void ProfileClaudeTest::testClaudeModelDefault()
{
    Profile::Ptr profile(new Profile);

    QVERIFY(!profile->isPropertySet(Profile::ClaudeModel));
    // Default empty string
    QVERIFY(profile->property<QString>(Profile::ClaudeModel).isEmpty());
}

void ProfileClaudeTest::testClaudeModelSet()
{
    Profile::Ptr profile(new Profile);

    profile->setProperty(Profile::ClaudeModel, QStringLiteral("claude-sonnet-4"));

    QVERIFY(profile->isPropertySet(Profile::ClaudeModel));
    QCOMPARE(profile->property<QString>(Profile::ClaudeModel), QStringLiteral("claude-sonnet-4"));

    profile->setProperty(Profile::ClaudeModel, QStringLiteral("claude-opus-4-5"));
    QCOMPARE(profile->property<QString>(Profile::ClaudeModel), QStringLiteral("claude-opus-4-5"));
}

void ProfileClaudeTest::testClaudeArgsDefault()
{
    Profile::Ptr profile(new Profile);

    QVERIFY(!profile->isPropertySet(Profile::ClaudeArgs));
    QVERIFY(profile->property<QString>(Profile::ClaudeArgs).isEmpty());
}

void ProfileClaudeTest::testClaudeArgsSet()
{
    Profile::Ptr profile(new Profile);

    profile->setProperty(Profile::ClaudeArgs, QStringLiteral("--verbose --no-color"));

    QVERIFY(profile->isPropertySet(Profile::ClaudeArgs));
    QCOMPARE(profile->property<QString>(Profile::ClaudeArgs), QStringLiteral("--verbose --no-color"));
}

void ProfileClaudeTest::testClaudeNotificationChannelsDefault()
{
    Profile::Ptr profile(new Profile);

    QVERIFY(!profile->isPropertySet(Profile::ClaudeNotificationChannels));
}

void ProfileClaudeTest::testClaudeNotificationChannelsSet()
{
    Profile::Ptr profile(new Profile);

    // Set to specific channel bitmask (SystemTray | Desktop = 1 | 2 = 3)
    profile->setProperty(Profile::ClaudeNotificationChannels, 3);

    QVERIFY(profile->isPropertySet(Profile::ClaudeNotificationChannels));
    QCOMPARE(profile->property<int>(Profile::ClaudeNotificationChannels), 3);

    // Set to all channels (15 = 0b1111)
    profile->setProperty(Profile::ClaudeNotificationChannels, 15);
    QCOMPARE(profile->property<int>(Profile::ClaudeNotificationChannels), 15);
}

void ProfileClaudeTest::testClaudeAutoApproveReadDefault()
{
    Profile::Ptr profile(new Profile);

    QVERIFY(!profile->isPropertySet(Profile::ClaudeAutoApproveRead));
    QCOMPARE(profile->property<bool>(Profile::ClaudeAutoApproveRead), false);
}

void ProfileClaudeTest::testClaudeAutoApproveReadSet()
{
    Profile::Ptr profile(new Profile);

    profile->setProperty(Profile::ClaudeAutoApproveRead, true);

    QVERIFY(profile->isPropertySet(Profile::ClaudeAutoApproveRead));
    QCOMPARE(profile->property<bool>(Profile::ClaudeAutoApproveRead), true);
}

void ProfileClaudeTest::testClaudeHooksConfigPathDefault()
{
    Profile::Ptr profile(new Profile);

    QVERIFY(!profile->isPropertySet(Profile::ClaudeHooksConfigPath));
    QVERIFY(profile->property<QString>(Profile::ClaudeHooksConfigPath).isEmpty());
}

void ProfileClaudeTest::testClaudeHooksConfigPathSet()
{
    Profile::Ptr profile(new Profile);

    QString configPath = QStringLiteral("/home/user/.config/claude/hooks.json");
    profile->setProperty(Profile::ClaudeHooksConfigPath, configPath);

    QVERIFY(profile->isPropertySet(Profile::ClaudeHooksConfigPath));
    QCOMPARE(profile->property<QString>(Profile::ClaudeHooksConfigPath), configPath);
}

void ProfileClaudeTest::testClaudePropertyInheritance()
{
    // Create parent profile with Claude settings
    Profile::Ptr parent(new Profile);
    parent->setProperty(Profile::ClaudeEnabled, true);
    parent->setProperty(Profile::ClaudeModel, QStringLiteral("claude-sonnet-4"));
    parent->setProperty(Profile::ClaudeNotificationChannels, 15);

    // Create child profile
    Profile::Ptr child(new Profile(parent));

    // Child should not have properties set locally
    QVERIFY(!child->isPropertySet(Profile::ClaudeEnabled));
    QVERIFY(!child->isPropertySet(Profile::ClaudeModel));
    QVERIFY(!child->isPropertySet(Profile::ClaudeNotificationChannels));

    // But should inherit parent's values
    QCOMPARE(child->property<bool>(Profile::ClaudeEnabled), true);
    QCOMPARE(child->property<QString>(Profile::ClaudeModel), QStringLiteral("claude-sonnet-4"));
    QCOMPARE(child->property<int>(Profile::ClaudeNotificationChannels), 15);

    // Override in child
    child->setProperty(Profile::ClaudeModel, QStringLiteral("claude-haiku"));

    QVERIFY(child->isPropertySet(Profile::ClaudeModel));
    QCOMPARE(child->property<QString>(Profile::ClaudeModel), QStringLiteral("claude-haiku"));

    // Parent should be unchanged
    QCOMPARE(parent->property<QString>(Profile::ClaudeModel), QStringLiteral("claude-sonnet-4"));
}

void ProfileClaudeTest::testClaudePropertyClone()
{
    // Create source profile with Claude settings
    Profile::Ptr source(new Profile);
    source->setProperty(Profile::Name, QStringLiteral("SourceProfile"));
    source->setProperty(Profile::Path, QStringLiteral("SourcePath"));
    source->setProperty(Profile::ClaudeEnabled, true);
    source->setProperty(Profile::ClaudeModel, QStringLiteral("claude-opus-4-5"));
    source->setProperty(Profile::ClaudeAutoApproveRead, true);
    source->setProperty(Profile::ClaudeNotificationChannels, 7);

    // Create target profile
    Profile::Ptr target(new Profile);
    target->setProperty(Profile::Name, QStringLiteral("TargetProfile"));
    target->setProperty(Profile::Path, QStringLiteral("TargetPath"));

    // Clone source to target
    target->clone(source, true);

    // Verify Claude properties were cloned
    QCOMPARE(target->property<bool>(Profile::ClaudeEnabled), true);
    QCOMPARE(target->property<QString>(Profile::ClaudeModel), QStringLiteral("claude-opus-4-5"));
    QCOMPARE(target->property<bool>(Profile::ClaudeAutoApproveRead), true);
    QCOMPARE(target->property<int>(Profile::ClaudeNotificationChannels), 7);

    // Name and Path should NOT be cloned
    QVERIFY(source->property<QString>(Profile::Name) != target->property<QString>(Profile::Name));
    QVERIFY(source->property<QString>(Profile::Path) != target->property<QString>(Profile::Path));
}

QTEST_GUILESS_MAIN(ProfileClaudeTest)

#include "moc_ProfileClaudeTest.cpp"
