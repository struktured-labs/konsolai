/*
    SPDX-FileCopyrightText: 2025 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef KONSOLAI_AGENTMANAGERPANEL_H
#define KONSOLAI_AGENTMANAGERPANEL_H

#include "AgentProvider.h"
#include "konsoleprivate_export.h"

#include <QWidget>

class QLabel;
class QLineEdit;
class QMenu;
class QTreeWidget;
class QTreeWidgetItem;

namespace Konsolai
{

/**
 * Panel displaying persistent agents from registered providers.
 *
 * Shows a tree of agents grouped by provider, with status badges,
 * budget info, and context menu actions for triggering runs,
 * setting briefs, viewing reports, and attaching interactively.
 */
class KONSOLEPRIVATE_EXPORT AgentManagerPanel : public QWidget
{
    Q_OBJECT

public:
    explicit AgentManagerPanel(QWidget *parent = nullptr);
    ~AgentManagerPanel() override;

    /** Register a provider. Takes ownership. */
    void addProvider(AgentProvider *provider);

    /** Remove and delete a provider by name. */
    void removeProvider(const QString &name);

    /** Get all registered provider names. */
    QStringList providerNames() const;

    /** Get a provider by name. */
    AgentProvider *provider(const QString &name) const;

    /** Total daily spend across all providers. */
    double totalDailySpendUSD() const;

    /** Total daily budget across all providers. */
    double totalDailyBudgetUSD() const;

    /** Force a full refresh of the tree. */
    void refresh();

    /** Set the linker for agent↔session coordination. */
    void setLinker(class AgentSessionLinker *linker);

    /** Select an agent item by ID in the tree. */
    void selectAgentById(const QString &agentId);

Q_SIGNALS:
    /** Request to attach to an agent interactively (open Claude tab). */
    void attachRequested(const AgentAttachInfo &info);

    /** Request to open a new session in a directory. */
    void openProjectRequested(const QString &directory);

    /** Spend totals changed. */
    void spendChanged(double dailySpend, double dailyBudget);

private Q_SLOTS:
    void onAgentChanged(const QString &id);
    void onAgentsReloaded();
    void showContextMenu(const QPoint &pos);
    void updateFooter();

private:
    void setupUi();
    void rebuildTree();
    void applyFilter(const QString &text);
    void updateAgentItem(QTreeWidgetItem *item, AgentProvider *provider, const AgentInfo &info);
    QTreeWidgetItem *findAgentItem(const QString &providerId, const QString &agentId) const;
    QString formatTimeSince(const QDateTime &dt) const;
    QString stateIcon(AgentStatus::State state) const;
    QString stateText(AgentStatus::State state) const;

    // Context menu actions
    void actionTriggerRun(AgentProvider *provider, const QString &agentId);
    void actionSetBrief(AgentProvider *provider, const QString &agentId);
    void actionAddSteeringNote(AgentProvider *provider, const QString &agentId);
    void actionMarkBriefDone(AgentProvider *provider, const QString &agentId);
    void actionViewReports(AgentProvider *provider, const QString &agentId);
    void actionViewLastResult(AgentProvider *provider, const QString &agentId);
    void actionRunHistory(AgentProvider *provider, const QString &agentId);
    void actionResetSession(AgentProvider *provider, const QString &agentId);

    QLineEdit *m_filterEdit = nullptr;
    QTreeWidget *m_tree = nullptr;
    QLabel *m_footerLabel = nullptr;
    QList<AgentProvider *> m_providers;
    QTimer *m_updateDebounce = nullptr;
    AgentSessionLinker *m_linker = nullptr;

    // Item data roles
    static constexpr int ProviderNameRole = Qt::UserRole + 1;
    static constexpr int AgentIdRole = Qt::UserRole + 2;
    static constexpr int IsAgentItemRole = Qt::UserRole + 3;
};

} // namespace Konsolai

#endif // KONSOLAI_AGENTMANAGERPANEL_H
