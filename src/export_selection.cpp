// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Aura Launcher contributors

#include "export_selection.h"

ExportSelectionTree::Node::Node(ExportFileEntryData entryData)
    : entry(std::move(entryData))
{
}

ExportSelectionTree::Node::~Node() = default;

void ExportSelectionTree::reset(const std::vector<ExportFileEntryData> &rootEntries,
                                bool rootTruncated)
{
    auto rootNode = std::make_unique<Node>(ExportFileEntryData{
            QStringLiteral("instance"), QString(), true, false});
    rootNode->checked = false;
    rootNode->partial = false;
    rootNode->expanded = true;
    rootNode->childrenLoaded = true;
    rootNode->truncated = rootTruncated;
    for (const ExportFileEntryData &entry : rootEntries) {
        rootNode->children.push_back(makeNode(entry, entry.suggested));
    }
    m_root = std::move(rootNode);
}

bool ExportSelectionTree::applyListing(const QString &path,
                                       const std::vector<ExportFileEntryData> &entries,
                                       bool truncated, bool forceChecked, QString *error)
{
    Node *node = m_root == nullptr ? nullptr : find(m_root.get(), path);
    if (node == nullptr || !node->entry.directory) {
        if (error != nullptr) {
            *error = QStringLiteral("export listing target is not a directory: %1").arg(path);
        }
        return false;
    }
    if (node->childrenLoaded) {
        if (error != nullptr) {
            *error = QStringLiteral("export listing already loaded: %1").arg(path);
        }
        return false;
    }
    node->pendingRequest = false;
    node->childrenLoaded = true;
    node->truncated = truncated;
    for (const ExportFileEntryData &entry : entries) {
        node->children.push_back(makeNode(entry, forceChecked || entry.suggested));
    }
    recompute(m_root->children);
    return true;
}

void ExportSelectionTree::setNodeChecked(const QString &path, bool checked)
{
    if (m_root == nullptr) {
        return;
    }
    Node *node = find(m_root.get(), path);
    if (node == nullptr) {
        return;
    }
    if (node->childrenLoaded) {
        propagateDown(node, checked);
    } else {
        node->checked = checked;
        node->partial = false;
    }
    recompute(m_root->children);
}

void ExportSelectionTree::setNodeExpanded(const QString &path, bool expanded)
{
    if (m_root == nullptr) {
        return;
    }
    Node *node = find(m_root.get(), path);
    if (node != nullptr && node->entry.directory) {
        node->expanded = expanded;
    }
}

void ExportSelectionTree::markPending(const QString &path, bool pending)
{
    if (m_root == nullptr) {
        return;
    }
    Node *node = find(m_root.get(), path);
    if (node != nullptr) {
        node->pendingRequest = pending;
    }
}

const ExportSelectionTree::Node *ExportSelectionTree::nodeAt(const QString &path) const
{
    return m_root == nullptr ? nullptr : find(m_root.get(), path);
}

bool ExportSelectionTree::busy() const
{
    return m_root != nullptr && busyIn(*m_root);
}

QString ExportSelectionTree::unloadedSelectedDirectory() const
{
    if (m_root == nullptr) {
        return QString();
    }
    return unloadedIn(*m_root);
}

std::optional<QStringList> ExportSelectionTree::collect(QString *error) const
{
    if (m_root == nullptr) {
        if (error != nullptr) {
            *error = QStringLiteral("export selection tree is not loaded");
        }
        return std::nullopt;
    }
    QStringList paths;
    bool ok = true;
    for (const std::unique_ptr<Node> &child : m_root->children) {
        collectInto(*child, paths, error, &ok);
        if (!ok) {
            return std::nullopt;
        }
    }
    return paths;
}

ExportSelectionTree::Node *ExportSelectionTree::find(Node *node, const QString &path)
{
    if (node->entry.path == path) {
        return node;
    }
    for (const std::unique_ptr<Node> &child : node->children) {
        Node *match = find(child.get(), path);
        if (match != nullptr) {
            return match;
        }
    }
    return nullptr;
}

const ExportSelectionTree::Node *ExportSelectionTree::find(const Node *node, const QString &path)
{
    if (node->entry.path == path) {
        return node;
    }
    for (const std::unique_ptr<Node> &child : node->children) {
        const Node *match = find(child.get(), path);
        if (match != nullptr) {
            return match;
        }
    }
    return nullptr;
}

std::unique_ptr<ExportSelectionTree::Node> ExportSelectionTree::makeNode(
        const ExportFileEntryData &entry, bool checked)
{
    auto node = std::make_unique<Node>(entry);
    node->checked = checked;
    return node;
}

void ExportSelectionTree::propagateDown(Node *node, bool checked)
{
    node->checked = checked;
    node->partial = false;
    for (const std::unique_ptr<Node> &child : node->children) {
        propagateDown(child.get(), checked);
    }
}

std::pair<bool, bool> ExportSelectionTree::recompute(std::vector<std::unique_ptr<Node>> &nodes)
{
    bool allSelected = true;
    bool anySelected = false;
    for (const std::unique_ptr<Node> &childPtr : nodes) {
        Node &node = *childPtr;
        if (node.childrenLoaded) {
            const std::pair<bool, bool> state = recompute(node.children);
            node.checked = state.first;
            node.partial = state.second && !state.first;
        }
        if (node.checked || node.partial) {
            anySelected = true;
        }
        if (!node.checked || node.partial) {
            allSelected = false;
        }
    }
    return {allSelected && !nodes.empty(), anySelected};
}

void ExportSelectionTree::collectInto(const Node &node, QStringList &paths,
                                      QString *error, bool *ok)
{
    if (!*ok) {
        return;
    }
    if (!node.checked && !node.partial) {
        return;
    }
    if (node.entry.directory) {
        if (!node.childrenLoaded) {
            *error = QStringLiteral("export selection is still loading: %1").arg(node.entry.path);
            *ok = false;
            return;
        }
        if (node.checked && node.truncated) {
            *error = QStringLiteral("export directory was truncated: %1").arg(node.entry.path);
            *ok = false;
            return;
        }
    }
    if (!node.entry.path.isEmpty()) {
        paths.append(node.entry.path);
    }
    for (const std::unique_ptr<Node> &child : node.children) {
        collectInto(*child, paths, error, ok);
    }
}

bool ExportSelectionTree::busyIn(const Node &node)
{
    if (node.pendingRequest) {
        return true;
    }
    for (const std::unique_ptr<Node> &child : node.children) {
        if (busyIn(*child)) {
            return true;
        }
    }
    return false;
}

QString ExportSelectionTree::unloadedIn(const Node &node)
{
    if (node.entry.directory && (node.checked || node.partial)
            && !node.childrenLoaded && !node.pendingRequest) {
        return node.entry.path;
    }
    for (const std::unique_ptr<Node> &child : node.children) {
        const QString match = unloadedIn(*child);
        if (!match.isNull()) {
            return match;
        }
    }
    return QString();
}