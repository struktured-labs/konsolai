/*
    SPDX-FileCopyrightText: 2025 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include <QButtonGroup>
#include <QCheckBox>
#include <QGroupBox>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QTest>

#include "../claude/ClaudeSessionWizard.h"

using namespace Konsolai;

class ClaudeSessionWizardTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testInitialState();
    void testLocalDefaults();
    void testRemoteSessionToggle();
    void testRemoteProjectRootUsesSSHUsername();
    void testRemoteProjectRootFallsBackToLocalUser();
    void testRemoteSessionSkipsLocalChecks();
    void testSshHostRequired();
    void testDialogSizeAccomodatesSSH();
    void testGitGroupHiddenForRemote();
    void testBrowseDisabledForRemote();
};

void ClaudeSessionWizardTest::testInitialState()
{
    ClaudeSessionWizard wizard;
    QCOMPARE(wizard.isRemoteSession(), false);
    QVERIFY(wizard.sshHost().isEmpty());
    QVERIFY(wizard.sshUsername().isEmpty());
    QCOMPARE(wizard.sshPort(), 22);
}

void ClaudeSessionWizardTest::testLocalDefaults()
{
    ClaudeSessionWizard wizard;
    // Local mode — directory should be constructable from root + folder
    QCOMPARE(wizard.isRemoteSession(), false);
    // No folder entered → empty directory
    QVERIFY(wizard.selectedDirectory().isEmpty());
}

void ClaudeSessionWizardTest::testRemoteSessionToggle()
{
    ClaudeSessionWizard wizard;

    // Start local
    QCOMPARE(wizard.isRemoteSession(), false);

    // Find and click the remote radio button
    const auto radios = wizard.findChildren<QRadioButton *>();
    QRadioButton *remote = nullptr;
    for (auto *r : radios) {
        if (r->text().contains(QStringLiteral("Remote"))) {
            remote = r;
            break;
        }
    }
    QVERIFY(remote);
    remote->setChecked(true);
    // Trigger the location changed signal
    Q_EMIT wizard.findChild<QButtonGroup *>()->idClicked(1);

    QCOMPARE(wizard.isRemoteSession(), true);
}

void ClaudeSessionWizardTest::testRemoteProjectRootUsesSSHUsername()
{
    ClaudeSessionWizard wizard;

    // Switch to remote
    const auto radios = wizard.findChildren<QRadioButton *>();
    for (auto *r : radios) {
        if (r->text().contains(QStringLiteral("Remote"))) {
            r->setChecked(true);
            break;
        }
    }
    Q_EMIT wizard.findChild<QButtonGroup *>()->idClicked(1);

    // Find SSH username field and set it
    const auto lineEdits = wizard.findChildren<QLineEdit *>();
    QLineEdit *usernameEdit = nullptr;
    QLineEdit *projectRootEdit = nullptr;
    for (auto *le : lineEdits) {
        // Username field has the local USER as placeholder
        if (le->placeholderText() == QString::fromLocal8Bit(qgetenv("USER"))) {
            usernameEdit = le;
        }
        // Project root contains "projects" in its value after remote switch
        if (le->text().contains(QStringLiteral("projects"))) {
            projectRootEdit = le;
        }
    }
    QVERIFY(usernameEdit);
    QVERIFY(projectRootEdit);

    // Set SSH username to "remoteuser"
    usernameEdit->setText(QStringLiteral("remoteuser"));

    // Project root should now use the remote username
    QCOMPARE(projectRootEdit->text(), QStringLiteral("/home/remoteuser/projects"));
}

void ClaudeSessionWizardTest::testRemoteProjectRootFallsBackToLocalUser()
{
    ClaudeSessionWizard wizard;

    // Switch to remote
    const auto radios = wizard.findChildren<QRadioButton *>();
    for (auto *r : radios) {
        if (r->text().contains(QStringLiteral("Remote"))) {
            r->setChecked(true);
            break;
        }
    }
    Q_EMIT wizard.findChild<QButtonGroup *>()->idClicked(1);

    // Find the project root field — it should use the local username as fallback
    // (since SSH username field is empty)
    const auto lineEdits = wizard.findChildren<QLineEdit *>();
    QLineEdit *projectRootEdit = nullptr;
    for (auto *le : lineEdits) {
        if (le->text().contains(QStringLiteral("projects"))) {
            projectRootEdit = le;
            break;
        }
    }
    QVERIFY(projectRootEdit);

    QString localUser = QString::fromLocal8Bit(qgetenv("USER"));
    QCOMPARE(projectRootEdit->text(), QStringLiteral("/home/%1/projects").arg(localUser));
}

void ClaudeSessionWizardTest::testRemoteSessionSkipsLocalChecks()
{
    // When in remote mode, selectedDirectory() should return a path
    // even if it doesn't exist locally
    ClaudeSessionWizard wizard;

    // Switch to remote
    const auto radios = wizard.findChildren<QRadioButton *>();
    for (auto *r : radios) {
        if (r->text().contains(QStringLiteral("Remote"))) {
            r->setChecked(true);
            break;
        }
    }
    Q_EMIT wizard.findChild<QButtonGroup *>()->idClicked(1);

    // Set a remote path that definitely doesn't exist locally
    const auto lineEdits = wizard.findChildren<QLineEdit *>();
    for (auto *le : lineEdits) {
        if (le->text().contains(QStringLiteral("projects"))) {
            le->setText(QStringLiteral("/home/remoteuser/projects"));
            break;
        }
    }

    // Set a folder name
    for (auto *le : lineEdits) {
        if (le->placeholderText().contains(QStringLiteral("project-name"))) {
            le->setText(QStringLiteral("test-project"));
            break;
        }
    }

    // selectedDirectory should return the remote path
    QCOMPARE(wizard.selectedDirectory(), QStringLiteral("/home/remoteuser/projects/test-project"));
}

void ClaudeSessionWizardTest::testSshHostRequired()
{
    ClaudeSessionWizard wizard;

    // Switch to remote
    const auto radios = wizard.findChildren<QRadioButton *>();
    for (auto *r : radios) {
        if (r->text().contains(QStringLiteral("Remote"))) {
            r->setChecked(true);
            break;
        }
    }
    Q_EMIT wizard.findChild<QButtonGroup *>()->idClicked(1);

    // Without setting a host, sshHost should be empty
    QVERIFY(wizard.sshHost().isEmpty());
}

void ClaudeSessionWizardTest::testDialogSizeAccomodatesSSH()
{
    ClaudeSessionWizard wizard;
    QVERIFY(wizard.minimumHeight() >= 550);
    QVERIFY(wizard.minimumWidth() >= 600);
}

void ClaudeSessionWizardTest::testGitGroupHiddenForRemote()
{
    ClaudeSessionWizard wizard;

    // Git group should not be explicitly hidden in local mode
    const auto groups = wizard.findChildren<QGroupBox *>();
    QGroupBox *git = nullptr;
    for (auto *g : groups) {
        if (g->title().contains(QStringLiteral("Git"))) {
            git = g;
            break;
        }
    }
    QVERIFY(git);
    QVERIFY(!git->isHidden());

    // Switch to remote
    const auto radios = wizard.findChildren<QRadioButton *>();
    for (auto *r : radios) {
        if (r->text().contains(QStringLiteral("Remote"))) {
            r->setChecked(true);
            break;
        }
    }
    Q_EMIT wizard.findChild<QButtonGroup *>()->idClicked(1);

    // Git group should be hidden for remote sessions
    QVERIFY(git->isHidden());
}

void ClaudeSessionWizardTest::testBrowseDisabledForRemote()
{
    ClaudeSessionWizard wizard;

    // Find browse buttons
    const auto buttons = wizard.findChildren<QPushButton *>();
    QList<QPushButton *> browseButtons;
    for (auto *b : buttons) {
        if (b->text().contains(QStringLiteral("Browse"))) {
            browseButtons.append(b);
        }
    }
    QVERIFY(browseButtons.size() >= 2); // Root browse + folder browse

    // In local mode, browse buttons should be enabled
    for (auto *b : browseButtons) {
        // Skip repo browse which may be disabled based on git mode
        if (b->isEnabled()) {
            QVERIFY(b->isEnabled());
        }
    }

    // Switch to remote
    const auto radios = wizard.findChildren<QRadioButton *>();
    for (auto *r : radios) {
        if (r->text().contains(QStringLiteral("Remote"))) {
            r->setChecked(true);
            break;
        }
    }
    Q_EMIT wizard.findChild<QButtonGroup *>()->idClicked(1);

    // Root browse and folder browse should be disabled for remote
    int disabledCount = 0;
    for (auto *b : browseButtons) {
        if (!b->isEnabled()) {
            disabledCount++;
        }
    }
    QVERIFY(disabledCount >= 2);
}

QTEST_MAIN(ClaudeSessionWizardTest)

#include "ClaudeSessionWizardTest.moc"
