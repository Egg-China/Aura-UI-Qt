// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Aura Launcher contributors

#ifndef AURA_UI_QT_EXPORT_SELECTION_H
#define AURA_UI_QT_EXPORT_SELECTION_H

#include <QString>
#include <QStringList>

#include <memory>
#include <optional>
#include <vector>

/// One selectable export-tree entry mirrored from the launcher listing command.
struct ExportFileEntryData {
    QString name;
    QString path;
    bool directory = false;
    bool suggested = false;
};

/// Pure tri-state selection tree used by the export dialog and its tests.
///
/// The tree mirrors the JavaFX export wizard semantics: whitelist paths are exact
/// relative paths of selected or partially selected nodes, and every selected
/// directory must have its children materialized before collection.
class ExportSelectionTree final {
public:
    struct Node {
        explicit Node(ExportFileEntryData entryData);
        ~Node();

        ExportFileEntryData entry;
        bool checked = false;
        bool partial = false;
        bool expanded = false;
        bool childrenLoaded = false;
        bool pendingRequest = false;
        bool truncated = false;
        std::vector<std::unique_ptr<Node>> children;
    };

    /// Replaces the tree with one loaded root level.
    void reset(const std::vector<ExportFileEntryData> &rootEntries, bool rootTruncated);

    /// Returns the synthetic root node, or `nullptr` before the root level arrives.
    const Node *root() const { return m_root.get(); }

    /// Applies one lazily loaded directory level; `forceChecked` derives from the node state.
    bool applyListing(const QString &path, const std::vector<ExportFileEntryData> &entries,
                      bool truncated, QString *error);

    /// Toggles one node and propagates the state through loaded descendants.
    void setNodeChecked(const QString &path, bool checked);

    /// Remembers one node's expansion state for view rebuilding.
    void setNodeExpanded(const QString &path, bool expanded);

    /// Marks one directory as having a listing request in flight.
    void markPending(const QString &path, bool pending);

    /// Clears every in-flight marker after a listing failure.
    void clearPending();

    /// Returns one node by path, or `nullptr` when absent.
    const Node *nodeAt(const QString &path) const;

    /// Returns whether any listing request is still in flight.
    bool busy() const;

    /// Returns the first selected directory whose children still need loading.
    QString unloadedSelectedDirectory() const;

    /// Collects exact export paths, or `std::nullopt` when selection is unsafe.
    std::optional<QStringList> collect(QString *error) const;

private:
    static Node *find(Node *node, const QString &path);
    static const Node *find(const Node *node, const QString &path);
    static std::unique_ptr<Node> makeNode(const ExportFileEntryData &entry, bool checked);
    static void propagateDown(Node *node, bool checked);
    static std::pair<bool, bool> recompute(std::vector<std::unique_ptr<Node>> &nodes);
    static void collectInto(const Node &node, QStringList &paths, QString *error, bool *ok);
    static void clearPendingIn(Node &node);
    static bool busyIn(const Node &node);
    static QString unloadedIn(const Node &node);

    std::unique_ptr<Node> m_root;
};

#endif // AURA_UI_QT_EXPORT_SELECTION_H