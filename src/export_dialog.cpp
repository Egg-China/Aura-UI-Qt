// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Aura Launcher contributors

#include "export_dialog.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {

constexpr int kPathRole = Qt::UserRole;

} // namespace

ExportDialog::ExportDialog(const QString &instanceName, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Export MultiMC archive"));
    setModal(true);
    setMinimumSize(QSize(480, 420));

    auto *form = new QFormLayout();
    m_outputEdit = new QLineEdit(instanceName + QStringLiteral(".zip"), this);
    m_nameEdit = new QLineEdit(instanceName, this);
    form->addRow(tr("Output file"), m_outputEdit);
    form->addRow(tr("Packaged name"), m_nameEdit);

    m_fullMode = new QRadioButton(tr("Full export (recommended for migration)"), this);
    m_customMode = new QRadioButton(tr("Custom selection"), this);
    m_fullMode->setChecked(true);
    m_customMode->setEnabled(false);

    m_tree = new QTreeWidget(this);
    m_tree->setHeaderHidden(true);
    m_tree->setRootIsDecorated(true);
    m_tree->setSelectionMode(QAbstractItemView::NoSelection);
    m_tree->setEnabled(false);
    connect(m_tree, &QTreeWidget::itemChanged, this, &ExportDialog::onItemChanged);
    connect(m_tree, &QTreeWidget::itemExpanded, this, &ExportDialog::onItemExpanded);
    connect(m_tree, &QTreeWidget::itemCollapsed, this, &ExportDialog::onItemCollapsed);

    m_statusLabel = new QLabel(tr("Loading instance files..."), this);
    m_statusLabel->setWordWrap(true);

    m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(m_buttons, &QDialogButtonBox::accepted, this, &ExportDialog::onAccept);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(m_fullMode);
    layout->addWidget(m_customMode);
    layout->addWidget(m_tree, 1);
    layout->addWidget(m_statusLabel);
    layout->addWidget(m_buttons);

    connect(m_customMode, &QRadioButton::toggled, this, [this](bool checked) {
        m_tree->setEnabled(checked && m_customSupported);
    });
}

void ExportDialog::requestRoot()
{
    emit pathRequested(QString());
}

void ExportDialog::applyListing(const QString &path,
                                const std::vector<ExportFileEntryData> &entries,
                                bool truncated)
{
    if (m_model.root() == nullptr) {
        if (!path.isEmpty()) {
            return;
        }
        m_model.reset(entries, truncated);
        m_rootLoaded = true;
        m_customSupported = !truncated;
        m_customMode->setEnabled(m_customSupported);
        m_tree->setEnabled(m_customMode->isChecked() && m_customSupported);
        m_statusLabel->setText(m_customSupported
                ? QString()
                : tr("Root has too many entries; custom selection is unavailable"));
        refreshTree();
        return;
    }

    const bool forced = m_forcedPaths.contains(path);
    m_forcedPaths.remove(path);
    QString error;
    if (!m_model.applyListing(path, entries, truncated, forced, &error)) {
        m_statusLabel->setText(error);
        abortCollection(error);
        return;
    }
    refreshTree();
    pumpCollection();
}

void ExportDialog::applyListingError(const QString &message)
{
    m_statusLabel->setText(message);
    abortCollection(message);
}

void ExportDialog::onAccept()
{
    const QString output = m_outputEdit->text().trimmed();
    const QString name = m_nameEdit->text().trimmed();
    if (output.isEmpty() || name.isEmpty()) {
        m_statusLabel->setText(tr("Output file and packaged name are required"));
        return;
    }
    if (m_fullMode->isChecked()) {
        emit acceptedExport(output, name, QStringList());
        accept();
        return;
    }
    if (!m_customSupported || !m_rootLoaded) {
        m_statusLabel->setText(tr("Custom selection is unavailable"));
        return;
    }
    m_collecting = true;
    m_buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
    m_statusLabel->setText(tr("Collecting selection..."));
    pumpCollection();
}

void ExportDialog::onItemChanged(QTreeWidgetItem *item, int column)
{
    if (m_building || item == nullptr || column != 0) {
        return;
    }
    const QString path = item->data(0, kPathRole).toString();
    m_model.setNodeChecked(path, item->checkState(0) != Qt::Unchecked);
    refreshTree();
}

void ExportDialog::onItemExpanded(QTreeWidgetItem *item)
{
    if (m_building || item == nullptr) {
        return;
    }
    const QString path = item->data(0, kPathRole).toString();
    const ExportSelectionTree::Node *node = m_model.nodeAt(path);
    if (node == nullptr) {
        return;
    }
    m_model.setNodeExpanded(path, true);
    if (!node->childrenLoaded && !node->pendingRequest) {
        m_model.markPending(path, true);
        emit pathRequested(path);
    }
}

void ExportDialog::onItemCollapsed(QTreeWidgetItem *item)
{
    if (m_building || item == nullptr) {
        return;
    }
    m_model.setNodeExpanded(item->data(0, kPathRole).toString(), false);
}

void ExportDialog::refreshTree()
{
    m_building = true;
    m_tree->clear();
    const ExportSelectionTree::Node *root = m_model.root();
    if (root != nullptr) {
        addNodes(nullptr, root->children);
    }
    m_building = false;
}

void ExportDialog::addNodes(QTreeWidgetItem *parentItem,
                            const std::vector<std::unique_ptr<ExportSelectionTree::Node>> &nodes)
{
    for (const std::unique_ptr<ExportSelectionTree::Node> &nodePtr : nodes) {
        const ExportSelectionTree::Node &node = *nodePtr;
        QTreeWidgetItem *item = parentItem == nullptr
            ? new QTreeWidgetItem(m_tree)
            : new QTreeWidgetItem(parentItem);
        item->setText(0, node.truncated
                ? tr("%1 (truncated)").arg(node.entry.name)
                : node.entry.name);
        item->setData(0, kPathRole, node.entry.path);
        item->setCheckState(0, node.checked
                ? Qt::Checked
                : (node.partial ? Qt::PartiallyChecked : Qt::Unchecked));
        if (node.entry.directory) {
            item->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
            item->setExpanded(node.expanded);
        }
        addNodes(item, node.children);
    }
}

void ExportDialog::pumpCollection()
{
    if (!m_collecting) {
        return;
    }
    if (m_model.busy()) {
        return;
    }
    const QString next = m_model.unloadedSelectedDirectory();
    if (!next.isEmpty()) {
        m_model.markPending(next, true);
        m_forcedPaths.insert(next);
        emit pathRequested(next);
        return;
    }
    QString error;
    const std::optional<QStringList> selection = m_model.collect(&error);
    if (!selection.has_value() || selection->isEmpty()) {
        abortCollection(selection.has_value() ? tr("Select at least one entry") : error);
        return;
    }
    m_collecting = false;
    emit acceptedExport(m_outputEdit->text().trimmed(), m_nameEdit->text().trimmed(), *selection);
    accept();
}

void ExportDialog::abortCollection(const QString &message)
{
    m_collecting = false;
    m_buttons->button(QDialogButtonBox::Ok)->setEnabled(true);
    if (!message.isEmpty()) {
        m_statusLabel->setText(message);
    }
}