// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Aura Launcher contributors

#ifndef AURA_UI_QT_EXPORT_DIALOG_H
#define AURA_UI_QT_EXPORT_DIALOG_H

#include "export_selection.h"

#include <QDialog>
#include <QSet>
#include <QString>
#include <QStringList>

#include <vector>

class QLineEdit;
class QLabel;
class QRadioButton;
class QTreeWidget;
class QTreeWidgetItem;
class QDialogButtonBox;

/// Modal export dialog with an asynchronous, lazily loaded selection tree.
class ExportDialog final : public QDialog {
    Q_OBJECT

public:
    ExportDialog(const QString &instanceName, QWidget *parent);

    /// Starts loading the selection-tree root level.
    void requestRoot();

signals:
    /// Requests one selection-tree directory level from the launcher.
    void pathRequested(const QString &path);

    /// Carries the accepted export request; an empty whitelist means full export.
    void acceptedExport(const QString &output, const QString &name,
                        const QStringList &whitelist);

public slots:
    /// Applies one successful listing reply for the requested path.
    void applyListing(const QString &path, const std::vector<ExportFileEntryData> &entries,
                      bool truncated);

    /// Applies one listing failure and cancels any pending collection.
    void applyListingError(const QString &message);

private slots:
    void onAccept();
    void onItemChanged(QTreeWidgetItem *item, int column);
    void onItemExpanded(QTreeWidgetItem *item);
    void onItemCollapsed(QTreeWidgetItem *item);

private:
    void refreshTree();
    void addNodes(QTreeWidgetItem *parentItem,
                  const std::vector<std::unique_ptr<ExportSelectionTree::Node>> &nodes);
    void pumpCollection();
    void abortCollection(const QString &message);

    ExportSelectionTree m_model;
    QSet<QString> m_forcedPaths;
    QLineEdit *m_outputEdit = nullptr;
    QLineEdit *m_nameEdit = nullptr;
    QRadioButton *m_fullMode = nullptr;
    QRadioButton *m_customMode = nullptr;
    QTreeWidget *m_tree = nullptr;
    QLabel *m_statusLabel = nullptr;
    QDialogButtonBox *m_buttons = nullptr;
    bool m_building = false;
    bool m_rootLoaded = false;
    bool m_customSupported = false;
    bool m_collecting = false;
};

#endif // AURA_UI_QT_EXPORT_DIALOG_H