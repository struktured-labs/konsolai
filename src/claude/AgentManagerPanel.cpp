/*
    SPDX-FileCopyrightText: 2025 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "AgentManagerPanel.h"
#include "AgentSessionLinker.h"

#include <QBrush>
#include <QColor>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QScrollArea>
#include <QTextBrowser>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <KLocalizedString>

namespace Konsolai
{

AgentManagerPanel::AgentManagerPanel(QWidget *parent)
    : QWidget(parent)
{
    setupUi();

    m_updateDebounce = new QTimer(this);
    m_updateDebounce->setSingleShot(true);
    m_updateDebounce->setInterval(100);
    connect(m_updateDebounce, &QTimer::timeout, this, &AgentManagerPanel::rebuildTree);
}

AgentManagerPanel::~AgentManagerPanel() = default;

void AgentManagerPanel::setupUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Search filter
    m_filterEdit = new QLineEdit(this);
    m_filterEdit->setPlaceholderText(i18n("Filter agents..."));
    m_filterEdit->setClearButtonEnabled(true);
    m_filterEdit->setObjectName(QStringLiteral("agentFilter"));
    connect(m_filterEdit, &QLineEdit::textChanged, this, &AgentManagerPanel::applyFilter);
    layout->addWidget(m_filterEdit);

    // Agent tree
    m_tree = new QTreeWidget(this);
    m_tree->setObjectName(QStringLiteral("agentTree"));
    m_tree->setHeaderLabels({i18n("Agent"), i18n("Status")});
    m_tree->setColumnCount(2);
    m_tree->setRootIsDecorated(true);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_tree->setIndentation(16);
    m_tree->header()->setStretchLastSection(false);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);

    connect(m_tree, &QTreeWidget::customContextMenuRequested, this, &AgentManagerPanel::showContextMenu);

    layout->addWidget(m_tree, 1);

    // Footer with aggregate spend
    m_footerLabel = new QLabel(this);
    m_footerLabel->setObjectName(QStringLiteral("agentFooter"));
    m_footerLabel->setAlignment(Qt::AlignCenter);
    m_footerLabel->setContentsMargins(4, 4, 4, 4);
    updateFooter();

    layout->addWidget(m_footerLabel);
}

void AgentManagerPanel::addProvider(AgentProvider *provider)
{
    provider->setParent(this);
    m_providers.append(provider);

    connect(provider, &AgentProvider::agentChanged, this, &AgentManagerPanel::onAgentChanged);
    connect(provider, &AgentProvider::agentsReloaded, this, &AgentManagerPanel::onAgentsReloaded);

    rebuildTree();
}

void AgentManagerPanel::removeProvider(const QString &providerName)
{
    for (int i = 0; i < m_providers.size(); ++i) {
        if (m_providers[i]->name() == providerName) {
            delete m_providers.takeAt(i);
            rebuildTree();
            return;
        }
    }
}

QStringList AgentManagerPanel::providerNames() const
{
    QStringList names;
    for (auto *p : m_providers) {
        names.append(p->name());
    }
    return names;
}

AgentProvider *AgentManagerPanel::provider(const QString &providerName) const
{
    for (auto *p : m_providers) {
        if (p->name() == providerName) {
            return p;
        }
    }
    return nullptr;
}

double AgentManagerPanel::totalDailySpendUSD() const
{
    double total = 0.0;
    for (auto *p : m_providers) {
        const QList<AgentInfo> agentList = p->agents();
        for (const AgentInfo &info : agentList) {
            AgentStatus status = p->agentStatus(info.id);
            total += status.dailySpentUSD;
        }
    }
    return total;
}

double AgentManagerPanel::totalDailyBudgetUSD() const
{
    double total = 0.0;
    for (auto *p : m_providers) {
        const QList<AgentInfo> agentList = p->agents();
        for (const AgentInfo &info : agentList) {
            total += info.budget.dailyUSD;
        }
    }
    return total;
}

void AgentManagerPanel::refresh()
{
    rebuildTree();
}

void AgentManagerPanel::setLinker(AgentSessionLinker *linker)
{
    m_linker = linker;
    if (m_linker) {
        connect(m_linker, &AgentSessionLinker::agentTabPresenceChanged, this, [this](const QString &agentId, bool) {
            Q_UNUSED(agentId);
            m_updateDebounce->stop();
            m_updateDebounce->start();
        });
    }
}

void AgentManagerPanel::selectAgentById(const QString &agentId)
{
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = m_tree->topLevelItem(i);
        if (item->data(0, AgentIdRole).toString() == agentId) {
            m_tree->setCurrentItem(item);
            m_tree->scrollToItem(item);
            return;
        }
    }
}

void AgentManagerPanel::onAgentChanged(const QString &id)
{
    // Find and update the specific agent item
    for (auto *p : m_providers) {
        QTreeWidgetItem *item = findAgentItem(p->name(), id);
        if (item) {
            const QList<AgentInfo> agentList = p->agents();
            for (const AgentInfo &info : agentList) {
                if (info.id == id) {
                    updateAgentItem(item, p, info);
                    break;
                }
            }
        }
    }
    updateFooter();
}

void AgentManagerPanel::onAgentsReloaded()
{
    // Debounce full rebuilds
    m_updateDebounce->stop();
    m_updateDebounce->start();
}

void AgentManagerPanel::rebuildTree()
{
    m_tree->clear();

    for (auto *prov : m_providers) {
        if (!prov->isAvailable()) {
            continue;
        }

        const QList<AgentInfo> agentList = prov->agents();
        for (const AgentInfo &info : agentList) {
            auto *item = new QTreeWidgetItem(m_tree);
            item->setData(0, ProviderNameRole, prov->name());
            item->setData(0, AgentIdRole, info.id);
            item->setData(0, IsAgentItemRole, true);

            updateAgentItem(item, prov, info);

            // Add child items for details
            AgentStatus status = prov->agentStatus(info.id);

            // Schedule line
            if (!info.schedule.isEmpty()) {
                auto *schedItem = new QTreeWidgetItem(item);
                schedItem->setText(0, QStringLiteral("\u23F0 ") + info.schedule);
                schedItem->setFlags(schedItem->flags() & ~Qt::ItemIsSelectable);
            }

            // Budget line
            if (info.budget.dailyUSD > 0.0) {
                auto *budgetItem = new QTreeWidgetItem(item);
                budgetItem->setText(0, QStringLiteral("$%1 / $%2 today").arg(status.dailySpentUSD, 0, 'f', 2).arg(info.budget.dailyUSD, 0, 'f', 2));
                budgetItem->setFlags(budgetItem->flags() & ~Qt::ItemIsSelectable);
            }

            // Last result
            if (!status.lastSummary.isEmpty()) {
                auto *resultItem = new QTreeWidgetItem(item);
                QString prefix = (status.state == AgentStatus::Error) ? QStringLiteral("\u26A0 ") : QStringLiteral("\u2713 ");
                resultItem->setText(0, prefix + status.lastSummary);
                resultItem->setFlags(resultItem->flags() & ~Qt::ItemIsSelectable);
            }

            // Brief
            if (!status.brief.direction.isEmpty() && !status.brief.isDone) {
                auto *briefItem = new QTreeWidgetItem(item);
                briefItem->setText(0, i18n("Brief: \"%1\"", status.brief.direction));
                briefItem->setFlags(briefItem->flags() & ~Qt::ItemIsSelectable);

                for (const QString &note : status.brief.steeringNotes) {
                    auto *noteItem = new QTreeWidgetItem(briefItem);
                    noteItem->setText(0, i18n("Steer: \"%1\"", note));
                    noteItem->setFlags(noteItem->flags() & ~Qt::ItemIsSelectable);
                }
            }

            // Session linkage child node (when linker is connected)
            if (m_linker) {
                auto *sessionItem = new QTreeWidgetItem(item);
                sessionItem->setData(0, Qt::UserRole + 10, QStringLiteral("session-link"));
                sessionItem->setData(0, AgentIdRole, info.id);

                if (m_linker->hasActiveTab(info.id)) {
                    sessionItem->setText(0, QStringLiteral("\U0001F4CB Active session"));
                    QFont f = sessionItem->font(0);
                    f.setBold(true);
                    sessionItem->setFont(0, f);
                    // Add [Tab] badge to the agent node itself
                    item->setText(0, info.name + QStringLiteral(" [Tab]"));
                } else if (m_linker->hasDetachedSession(info.id)) {
                    sessionItem->setText(0, QStringLiteral("\U0001F4CB Detached session"));
                    sessionItem->setForeground(0, QBrush(QColor(140, 140, 140)));
                } else {
                    sessionItem->setText(0, i18n("(no session)"));
                    QFont f = sessionItem->font(0);
                    f.setItalic(true);
                    sessionItem->setFont(0, f);
                    sessionItem->setForeground(0, QBrush(QColor(120, 120, 120)));
                }
            }
        }
    }

    updateFooter();

    // Reapply filter after rebuild
    if (m_filterEdit && !m_filterEdit->text().isEmpty()) {
        applyFilter(m_filterEdit->text());
    }
}

void AgentManagerPanel::updateAgentItem(QTreeWidgetItem *item, AgentProvider *provider, const AgentInfo &info)
{
    AgentStatus status = provider->agentStatus(info.id);

    // Col 0: name + truncated goal
    QString goalPreview = info.goal;
    if (goalPreview.length() > 40) {
        goalPreview = goalPreview.left(37) + QStringLiteral("...");
    }
    item->setText(0, info.name);
    item->setToolTip(0, info.goal);

    // Col 1: state badge + timing
    QString timing;
    if (status.state == AgentStatus::Running) {
        if (status.lastRun.isValid()) {
            int secs = static_cast<int>(status.lastRun.secsTo(QDateTime::currentDateTime()));
            timing = (secs < 60) ? QStringLiteral("%1s").arg(secs) : QStringLiteral("%1m").arg(secs / 60);
        }
    } else if (status.lastRun.isValid()) {
        timing = formatTimeSince(status.lastRun);
    }

    QString statusText = stateIcon(status.state) + QStringLiteral(" ") + stateText(status.state);
    if (!timing.isEmpty()) {
        statusText += QStringLiteral("  ") + timing;
    }
    item->setText(1, statusText);
}

QTreeWidgetItem *AgentManagerPanel::findAgentItem(const QString &providerId, const QString &agentId) const
{
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = m_tree->topLevelItem(i);
        if (item->data(0, ProviderNameRole).toString() == providerId && item->data(0, AgentIdRole).toString() == agentId) {
            return item;
        }
    }
    return nullptr;
}

QString AgentManagerPanel::formatTimeSince(const QDateTime &dt) const
{
    if (!dt.isValid()) {
        return {};
    }

    qint64 secs = dt.secsTo(QDateTime::currentDateTime());
    if (secs < 60) {
        return i18n("%1s ago", secs);
    }
    if (secs < 3600) {
        return i18n("%1m ago", secs / 60);
    }
    if (secs < 86400) {
        return i18n("%1h ago", secs / 3600);
    }
    return i18n("%1d ago", secs / 86400);
}

QString AgentManagerPanel::stateIcon(AgentStatus::State state) const
{
    switch (state) {
    case AgentStatus::Running:
        return QStringLiteral("\u25CF"); // ●
    case AgentStatus::Idle:
        return QStringLiteral("\u25CB"); // ○
    case AgentStatus::Budget:
        return QStringLiteral("$");
    case AgentStatus::Error:
        return QStringLiteral("\u26A0"); // ⚠
    case AgentStatus::Paused:
        return QStringLiteral("\u23F8"); // ⏸
    }
    return QStringLiteral("?");
}

QString AgentManagerPanel::stateText(AgentStatus::State state) const
{
    switch (state) {
    case AgentStatus::Running:
        return i18n("run");
    case AgentStatus::Idle:
        return i18n("idle");
    case AgentStatus::Budget:
        return i18n("budget");
    case AgentStatus::Error:
        return i18n("error");
    case AgentStatus::Paused:
        return i18n("paused");
    }
    return {};
}

void AgentManagerPanel::showContextMenu(const QPoint &pos)
{
    QTreeWidgetItem *item = m_tree->itemAt(pos);

    // Handle session-link child node clicks
    if (item && item->data(0, Qt::UserRole + 10).toString() == QStringLiteral("session-link")) {
        QString linkedAgentId = item->data(0, AgentIdRole).toString();
        if (m_linker && !linkedAgentId.isEmpty()) {
            if (m_linker->hasActiveTab(linkedAgentId)) {
                m_linker->focusAgentTab(linkedAgentId);
            }
        }
        return;
    }

    if (!item || !item->data(0, IsAgentItemRole).toBool()) {
        return;
    }

    QString providerName = item->data(0, ProviderNameRole).toString();
    QString agentId = item->data(0, AgentIdRole).toString();

    AgentProvider *prov = provider(providerName);
    if (!prov) {
        return;
    }

    AgentStatus status = prov->agentStatus(agentId);
    AgentAttachInfo attach = prov->attachInfo(agentId);

    QMenu menu(this);

    // Focus Tab action when an active tab exists
    if (m_linker && m_linker->hasActiveTab(agentId)) {
        menu.addAction(QIcon::fromTheme(QStringLiteral("go-jump")), i18n("Focus Tab"), this, [this, agentId]() {
            m_linker->focusAgentTab(agentId);
        });
        menu.addSeparator();
    }

    if (attach.canAttach) {
        // Change label to "Reattach" when a detached session exists
        bool hasDetached = m_linker && m_linker->hasDetachedSession(agentId);
        QString label = hasDetached ? i18n("Reattach") : i18n("Chat / Attach");
        menu.addAction(QIcon::fromTheme(QStringLiteral("utilities-terminal")), label, this, [this, attach]() {
            Q_EMIT attachRequested(attach);
        });
        menu.addSeparator();
    }

    menu.addAction(QIcon::fromTheme(QStringLiteral("media-playback-start")), i18n("Trigger Run..."), this, [this, prov, agentId]() {
        actionTriggerRun(prov, agentId);
    });

    menu.addSeparator();

    menu.addAction(i18n("Set Brief..."), this, [this, prov, agentId]() {
        actionSetBrief(prov, agentId);
    });

    menu.addAction(i18n("Add Steering Note..."), this, [this, prov, agentId]() {
        actionAddSteeringNote(prov, agentId);
    });

    QAction *doneAction = menu.addAction(i18n("Mark Brief Done"), this, [this, prov, agentId]() {
        actionMarkBriefDone(prov, agentId);
    });
    doneAction->setEnabled(!status.brief.direction.isEmpty() && !status.brief.isDone);

    menu.addSeparator();

    menu.addAction(QIcon::fromTheme(QStringLiteral("document-open")), i18n("View Reports"), this, [this, prov, agentId]() {
        actionViewReports(prov, agentId);
    });

    menu.addAction(i18n("View Last Result"), this, [this, prov, agentId]() {
        actionViewLastResult(prov, agentId);
    });

    menu.addAction(i18n("Run History"), this, [this, prov, agentId]() {
        actionRunHistory(prov, agentId);
    });

    menu.addSeparator();

    if (!attach.workingDirectory.isEmpty()) {
        menu.addAction(QIcon::fromTheme(QStringLiteral("folder-open")), i18n("Open Project"), this, [this, attach]() {
            Q_EMIT openProjectRequested(attach.workingDirectory);
        });
    }

    menu.addAction(QIcon::fromTheme(QStringLiteral("edit-clear")), i18n("Reset Session"), this, [this, prov, agentId]() {
        actionResetSession(prov, agentId);
    });

    menu.exec(m_tree->viewport()->mapToGlobal(pos));
}

void AgentManagerPanel::actionTriggerRun(AgentProvider *provider, const QString &agentId)
{
    bool ok;
    QString task = QInputDialog::getText(this, i18n("Trigger Run"), i18n("Task override (optional):"), QLineEdit::Normal, QString(), &ok);
    if (ok) {
        provider->triggerRun(agentId, task);
    }
}

void AgentManagerPanel::actionSetBrief(AgentProvider *provider, const QString &agentId)
{
    bool ok;
    QString direction = QInputDialog::getMultiLineText(this, i18n("Set Brief"), i18n("Creative direction:"), QString(), &ok);
    if (ok && !direction.isEmpty()) {
        provider->setBrief(agentId, direction);
    }
}

void AgentManagerPanel::actionAddSteeringNote(AgentProvider *provider, const QString &agentId)
{
    bool ok;
    QString note = QInputDialog::getText(this, i18n("Steering Note"), i18n("Note:"), QLineEdit::Normal, QString(), &ok);
    if (ok && !note.isEmpty()) {
        provider->addSteeringNote(agentId, note);
    }
}

void AgentManagerPanel::actionMarkBriefDone(AgentProvider *provider, const QString &agentId)
{
    provider->markBriefDone(agentId);
}

void AgentManagerPanel::actionViewReports(AgentProvider *provider, const QString &agentId)
{
    QList<AgentReport> reports = provider->recentReports(agentId);
    if (reports.isEmpty()) {
        QMessageBox::information(this, i18n("Reports"), i18n("No reports found for this agent."));
        return;
    }

    auto *dialog = new QDialog(this);
    dialog->setWindowTitle(i18n("Agent Reports"));
    dialog->resize(600, 400);
    auto *layout = new QVBoxLayout(dialog);

    auto *browser = new QTextBrowser(dialog);
    for (const AgentReport &report : reports) {
        browser->append(QStringLiteral("## %1\n*%2*\n\n%3\n\n---\n").arg(report.title, report.timestamp.toString(), report.content));
    }
    browser->moveCursor(QTextCursor::Start);

    layout->addWidget(browser);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::accept);
    layout->addWidget(buttons);

    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
}

void AgentManagerPanel::actionViewLastResult(AgentProvider *provider, const QString &agentId)
{
    AgentRunResult result = provider->lastResult(agentId);
    if (result.summary.isEmpty() && result.fullOutput.isEmpty()) {
        QMessageBox::information(this, i18n("Last Result"), i18n("No results found for this agent."));
        return;
    }

    auto *dialog = new QDialog(this);
    dialog->setWindowTitle(i18n("Last Run Result"));
    dialog->resize(500, 350);
    auto *layout = new QVBoxLayout(dialog);

    QString statusStr;
    switch (result.status) {
    case AgentRunResult::Ok:
        statusStr = i18n("OK");
        break;
    case AgentRunResult::Error:
        statusStr = i18n("Error");
        break;
    case AgentRunResult::Budget:
        statusStr = i18n("Budget");
        break;
    case AgentRunResult::Timeout:
        statusStr = i18n("Timeout");
        break;
    }

    auto *infoLabel =
        new QLabel(i18n("Status: %1 | Cost: $%2 | Exit: %3", statusStr, QString::number(result.costUSD, 'f', 2), QString::number(result.exitCode)), dialog);
    layout->addWidget(infoLabel);

    auto *browser = new QTextBrowser(dialog);
    browser->setPlainText(QStringLiteral("Summary: %1\n\n%2").arg(result.summary, result.fullOutput));
    layout->addWidget(browser);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::accept);
    layout->addWidget(buttons);

    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
}

void AgentManagerPanel::actionRunHistory(AgentProvider *provider, const QString &agentId)
{
    QList<AgentRunResult> results = provider->recentResults(agentId);
    if (results.isEmpty()) {
        QMessageBox::information(this, i18n("Run History"), i18n("No run history for this agent."));
        return;
    }

    auto *dialog = new QDialog(this);
    dialog->setWindowTitle(i18n("Run History"));
    dialog->resize(700, 400);
    auto *layout = new QVBoxLayout(dialog);

    auto *tree = new QTreeWidget(dialog);
    tree->setHeaderLabels({i18n("Time"), i18n("Status"), i18n("Cost"), i18n("Summary")});
    tree->setColumnCount(4);

    for (const AgentRunResult &r : results) {
        auto *item = new QTreeWidgetItem(tree);
        item->setText(0, r.timestamp.toString(QStringLiteral("yyyy-MM-dd hh:mm")));

        QString st;
        switch (r.status) {
        case AgentRunResult::Ok:
            st = QStringLiteral("OK");
            break;
        case AgentRunResult::Error:
            st = QStringLiteral("Error");
            break;
        case AgentRunResult::Budget:
            st = QStringLiteral("Budget");
            break;
        case AgentRunResult::Timeout:
            st = QStringLiteral("Timeout");
            break;
        }
        item->setText(1, st);
        item->setText(2, QStringLiteral("$%1").arg(r.costUSD, 0, 'f', 2));
        item->setText(3, r.summary);
    }

    tree->header()->setSectionResizeMode(3, QHeaderView::Stretch);
    layout->addWidget(tree);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::accept);
    layout->addWidget(buttons);

    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
}

void AgentManagerPanel::actionResetSession(AgentProvider *provider, const QString &agentId)
{
    auto result = QMessageBox::question(this, i18n("Reset Session"), i18n("Clear session context for fresh start?"), QMessageBox::Yes | QMessageBox::No);
    if (result == QMessageBox::Yes) {
        provider->resetSession(agentId);
    }
}

void AgentManagerPanel::updateFooter()
{
    double spend = totalDailySpendUSD();
    double budget = totalDailyBudgetUSD();

    if (budget > 0.0) {
        m_footerLabel->setText(i18n("Fleet total: $%1 / $%2", QString::number(spend, 'f', 2), QString::number(budget, 'f', 2)));
    } else if (spend > 0.0) {
        m_footerLabel->setText(i18n("Fleet total: $%1 today", QString::number(spend, 'f', 2)));
    } else {
        m_footerLabel->setText(i18n("No agent activity today"));
    }

    Q_EMIT spendChanged(spend, budget);
}

void AgentManagerPanel::applyFilter(const QString &text)
{
    if (!m_tree) {
        return;
    }

    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        auto *item = m_tree->topLevelItem(i);
        if (text.isEmpty()) {
            item->setHidden(false);
        } else {
            // Match against agent name (col 0), status (col 1), tooltip (goal),
            // and agentId (data role)
            QString agentId = item->data(0, AgentIdRole).toString();
            bool matches = item->text(0).contains(text, Qt::CaseInsensitive) || item->text(1).contains(text, Qt::CaseInsensitive)
                || item->toolTip(0).contains(text, Qt::CaseInsensitive) || agentId.contains(text, Qt::CaseInsensitive);
            item->setHidden(!matches);
        }
    }
}

} // namespace Konsolai

#include "moc_AgentManagerPanel.cpp"
