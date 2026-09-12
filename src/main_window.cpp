// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Aura Launcher contributors

#include "main_window.h"
#include "export_dialog.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QFont>
#include <QFormLayout>
#include <QFrame>
#include <QLineEdit>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QShowEvent>
#include <QTextBrowser>
#include <QVBoxLayout>

#include <algorithm>

namespace {
QString optionalText(const BridgeValue &map, const QString &key, const QString &fallback)
{
    const std::optional<QString> value = map.optionalString(key);
    return value.has_value() && !value->isEmpty() ? *value : fallback;
}

QString optionalInteger(const BridgeValue &map, const QString &key)
{
    const BridgeValue *found = map.entry(key);
    if (found == nullptr || found->type() != BridgeValue::Type::Integer) {
        return QStringLiteral("0");
    }
    return QString::number(found->toInteger());
}
} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    buildInterface();
    setWindowTitle(tr("Aura Launcher — Qt Runtime"));
    resize(1160, 720);
    setMinimumSize(960, 640);
}

void MainWindow::buildInterface()
{
    QFrame *root = new QFrame(this);
    root->setObjectName(QStringLiteral("auraRoot"));
    QHBoxLayout *layout = new QHBoxLayout(root);
    layout->setContentsMargins(18, 18, 18, 18);
    layout->setSpacing(16);

    QVBoxLayout *left = new QVBoxLayout();
    left->setSpacing(10);
    QLabel *heading = new QLabel(tr("Instances"), root);
    heading->setObjectName(QStringLiteral("auraHeading"));
    m_instances = new QListWidget(root);
    m_instances->setObjectName(QStringLiteral("auraList"));
    m_instances->setMinimumWidth(310);
    m_account = new QLabel(tr("Account: not synchronized"), root);
    m_account->setObjectName(QStringLiteral("auraMuted"));
    m_launch = new QPushButton(tr("Launch selected"), root);
    m_import = new QPushButton(tr("Import instance..."), root);
    m_export = new QPushButton(tr("Export selected..."), root);
    m_refresh = new QPushButton(tr("Refresh state"), root);
    m_shutdown = new QPushButton(tr("Ask launcher to exit"), root);
    m_launch->setEnabled(false);
    m_export->setEnabled(false);
    left->addWidget(heading);
    left->addWidget(m_instances, 1);
    left->addWidget(m_account);
    left->addWidget(m_launch);
    left->addWidget(m_import);
    left->addWidget(m_export);
    left->addWidget(m_refresh);
    left->addWidget(m_shutdown);

    QVBoxLayout *right = new QVBoxLayout();
    right->setSpacing(10);
    m_route = new QLabel(tr("Route: home"), root);
    m_route->setObjectName(QStringLiteral("auraRoute"));
    m_details = new QTextBrowser(root);
    m_details->setOpenExternalLinks(false);
    QLabel *contributionsHeading = new QLabel(tr("Plugin contributions"), root);
    contributionsHeading->setObjectName(QStringLiteral("auraHeading"));
    m_contributions = new QListWidget(root);
    m_contributions->setMaximumHeight(165);
    m_status = new QLabel(tr("Waiting for the launcher handshake"), root);
    m_status->setObjectName(QStringLiteral("auraMuted"));
    right->addWidget(m_route);
    right->addWidget(m_details, 1);
    right->addWidget(contributionsHeading);
    right->addWidget(m_contributions);
    right->addWidget(m_status);

    layout->addLayout(left, 0);
    layout->addLayout(right, 1);
    setCentralWidget(root);

    setStyleSheet(QStringLiteral(
        "QFrame#auraRoot { background: #141518; }"
        "QLabel { color: #e5e7eb; font-size: 14px; }"
        "QLabel#auraHeading { color: #ffffff; font-size: 18px; font-weight: 700; }"
        "QLabel#auraRoute { color: #22c55e; font-size: 16px; font-weight: 600; }"
        "QLabel#auraMuted { color: #94a3b8; }"
        "QListWidget, QTextBrowser {"
        " background: #191b1f; border: 1px solid #323439; border-radius: 7px;"
        " color: #e5e7eb; padding: 6px; selection-background-color: #166534;"
        " selection-color: #ffffff; }"
        "QPushButton {"
        " background: #26282d; border: 1px solid #3b3d43; border-radius: 6px;"
        " color: #f8fafc; padding: 8px 12px; }"
        "QPushButton:disabled { color: #64748b; }"
        "QPushButton:hover:enabled { background: #33363d; }"));

    connect(m_instances, &QListWidget::currentItemChanged, this, &MainWindow::updateSelection);
    connect(m_launch, &QPushButton::clicked, this, &MainWindow::emitLaunch);
    connect(m_import, &QPushButton::clicked, this, &MainWindow::emitImport);
    connect(m_export, &QPushButton::clicked, this, &MainWindow::emitExport);
    connect(m_refresh, &QPushButton::clicked, this, &MainWindow::refreshRequested);
    connect(m_shutdown, &QPushButton::clicked, this, &MainWindow::launcherShutdownRequested);
    connect(m_contributions, &QListWidget::itemDoubleClicked, this, [this]() {
        emitPluginAction();
    });
}

void MainWindow::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);
    if (!m_readyEmitted) {
        m_readyEmitted = true;
        emit interfaceReady();
    }
}

void MainWindow::renderInstances(const BridgeValue &instances)
{
    m_models.clear();
    m_instances->clear();
    if (instances.type() != BridgeValue::Type::Array) {
        m_launch->setEnabled(false);
        return;
    }
    for (const BridgeValue &raw : instances.arrayValues()) {
        const std::optional<QString> id = raw.optionalString(QStringLiteral("id"));
        if (!id.has_value()) {
            continue;
        }
        Instance instance;
        instance.id = *id;
        instance.name = optionalText(raw, QStringLiteral("name"), instance.id);
        instance.version = optionalText(raw, QStringLiteral("version"), tr("unknown"));
        instance.loader = optionalText(raw, QStringLiteral("loader"), tr("Vanilla"));
        instance.lastPlayed = optionalText(raw, QStringLiteral("lastPlayed"), tr("never"));
        instance.playTime = optionalText(raw, QStringLiteral("playTime"), tr("0 hours"));
        instance.modCount = optionalText(raw, QStringLiteral("modCount"), QStringLiteral("0"));
        if (raw.entry(QStringLiteral("modCount")) != nullptr) {
            instance.modCount = optionalInteger(raw, QStringLiteral("modCount"));
        }
        instance.description = optionalText(raw, QStringLiteral("description"), tr("No description"));
        const BridgeValue *favorite = raw.entry(QStringLiteral("isFavorite"));
        instance.favorite = favorite != nullptr && favorite->type() == BridgeValue::Type::Boolean
            && favorite->toBoolean();
        m_models.push_back(instance);
    }
    for (const Instance &instance : m_models) {
        const QString title = instance.favorite
            ? QStringLiteral("%1  ★").arg(instance.name)
            : instance.name;
        QListWidgetItem *item = new QListWidgetItem(m_instances);
        item->setText(QStringLiteral("%1\n%2 · %3").arg(title, instance.loader, instance.version));
        item->setData(Qt::UserRole, instance.id);
    }
    if (m_instances->count() > 0) {
        m_instances->setCurrentRow(0);
    } else {
        updateDetails();
    }
}
void MainWindow::renderAccounts(const BridgeValue &accounts)
{
    QString label = tr("Account: not synchronized");
    if (accounts.type() == BridgeValue::Type::Array && !accounts.arrayValues().empty()) {
        const BridgeValue *selected = nullptr;
        for (const BridgeValue &account : accounts.arrayValues()) {
            if (!account.isMap()) {
                continue;
            }
            const BridgeValue *active = account.entry(QStringLiteral("isActive"));
            if (active != nullptr && active->type() == BridgeValue::Type::Boolean && active->toBoolean()) {
                selected = &account;
                break;
            }
            const BridgeValue *defaultFlag = account.entry(QStringLiteral("isDefault"));
            if (defaultFlag != nullptr && defaultFlag->type() == BridgeValue::Type::Boolean
                && defaultFlag->toBoolean()) {
                selected = &account;
                break;
            }
        }
        if (selected == nullptr) {
            selected = &accounts.arrayValues().front();
        }
        std::optional<QString> username = selected->optionalString(QStringLiteral("username"));
        if (!username.has_value()) {
            username = selected->optionalString(QStringLiteral("profileName"));
        }
        if (username.has_value()) {
            label = tr("Account: %1").arg(*username);
        }
    }
    m_account->setText(label);
}

void MainWindow::renderContributions(const BridgeValue &contributions)
{
    m_contributions->clear();
    if (contributions.type() != BridgeValue::Type::Array) {
        return;
    }
    for (const BridgeValue &contribution : contributions.arrayValues()) {
        const std::optional<QString> id = contribution.optionalString(QStringLiteral("id"));
        const std::optional<QString> label = contribution.optionalString(QStringLiteral("label"));
        if (!id.has_value() || !label.has_value()) {
            continue;
        }
        QListWidgetItem *item = new QListWidgetItem(m_contributions);
        item->setText(*label);
        item->setData(Qt::UserRole, *id);
        item->setToolTip(tr("Double-click to invoke %1").arg(*id));
    }
}

void MainWindow::setSnapshot(const BridgeValue &snapshot)
{
    const auto field = [&snapshot](const QString &key) -> BridgeValue {
        const BridgeValue *found = snapshot.entry(key);
        return found == nullptr ? BridgeValue::nullValue() : *found;
    };
    renderInstances(field(QStringLiteral("instances")));
    renderAccounts(field(QStringLiteral("accounts")));
    renderContributions(field(QStringLiteral("pluginContributions")));
    showNotification(tr("Aura"), tr("Launcher state synchronized"));
}

void MainWindow::applyInstances(const BridgeValue &instances)
{
    renderInstances(instances);
}

void MainWindow::applyAccounts(const BridgeValue &accounts)
{
    renderAccounts(accounts);
}

void MainWindow::setRoute(const QString &route)
{
    m_route->setText(tr("Route: %1").arg(route));
}

void MainWindow::showNotification(const QString &title, const QString &message)
{
    m_status->setText(QStringLiteral("%1 — %2").arg(title, message));
}

void MainWindow::updateSelection()
{
    updateDetails();
    m_launch->setEnabled(!selectedInstanceId().isEmpty());
    m_export->setEnabled(!selectedInstanceId().isEmpty());
}

void MainWindow::updateDetails()
{
    const QString id = selectedInstanceId();
    const auto found = std::find_if(m_models.cbegin(), m_models.cend(),
                                    [&id](const Instance &candidate) { return candidate.id == id; });
    if (found == m_models.cend()) {
        m_details->setHtml(QStringLiteral("<h2>%1</h2><p>%2</p>")
                               .arg(tr("No instance"), tr("The launcher did not provide an instance.")));
        return;
    }
    m_details->setHtml(QStringLiteral(
        "<h2>%1</h2>"
        "<p><b>ID:</b> %2</p>"
        "<p><b>Version:</b> %3 &nbsp; <b>Loader:</b> %4</p>"
        "<p><b>Last played:</b> %5 &nbsp; <b>Play time:</b> %6</p>"
        "<p><b>Mods:</b> %7</p>"
        "<p>%8</p>")
        .arg(found->name.toHtmlEscaped(), found->id.toHtmlEscaped(),
             found->version.toHtmlEscaped(), found->loader.toHtmlEscaped(),
             found->lastPlayed.toHtmlEscaped(), found->playTime.toHtmlEscaped(),
             found->modCount.toHtmlEscaped(), found->description.toHtmlEscaped()));
}

QString MainWindow::selectedInstanceId() const
{
    QListWidgetItem *item = m_instances->currentItem();
    return item == nullptr ? QString() : item->data(Qt::UserRole).toString();
}

void MainWindow::emitImport()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Import instance"));
    QFormLayout *form = new QFormLayout(&dialog);
    QLineEdit *source = new QLineEdit(&dialog);
    source->setPlaceholderText(tr("D:/Packs/instance.zip or https://example.com/pack.zip"));
    QLineEdit *name = new QLineEdit(&dialog);
    QLineEdit *group = new QLineEdit(&dialog);
    form->addRow(tr("Archive path or URL"), source);
    form->addRow(tr("Instance name"), name);
    form->addRow(tr("Group (optional)"), group);
    QDialogButtonBox *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted
        || source->text().trimmed().isEmpty()
        || name->text().trimmed().isEmpty()) {
        return;
    }
    emit importRequested(source->text().trimmed(), name->text().trimmed(), group->text().trimmed());
}

void MainWindow::emitExport()
{
    const QString instanceId = selectedInstanceId();
    if (instanceId.isEmpty()) {
        return;
    }
    QString instanceName = instanceId;
    for (const Instance &instance : m_models) {
        if (instance.id == instanceId) {
            instanceName = instance.name;
            break;
        }
    }
    auto *dialog = new ExportDialog(instanceName, this);
    const QString token = dialog->token();
    const QPointer<ExportDialog> tracked = dialog;
    m_exportDialog = dialog;
    m_exportDialogs.insert(token, dialog);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(dialog, &ExportDialog::pathRequested, this,
            [this, instanceId](const QString &token, const QString &path) {
        emit exportFilesRequested(instanceId, token, path);
    });
    connect(dialog, &ExportDialog::acceptedExport, this,
            [this, instanceId](const QString &output, const QString &name,
                               const QStringList &whitelist) {
        emit exportRequested(instanceId, output, name, whitelist);
    });
    connect(dialog, &QDialog::finished, this, [this, token, tracked]() {
        m_exportDialogs.remove(token);
        if (m_exportDialog == tracked) {
            m_exportDialog = nullptr;
        }
    });
    dialog->show();
    dialog->requestRoot();
}

void MainWindow::applyExportFiles(const BridgeValue &listing)
{
    if (listing.type() != BridgeValue::Type::Map) {
        return;
    }
    const std::optional<QString> token = listing.optionalString(QStringLiteral("token"));
    ExportDialog *dialog = nullptr;
    if (token.has_value() && !token->isEmpty()) {
        // Tokenized replies must match their owning dialog; dropping them prevents stale
        // traffic from clearing an unrelated dialog's pending selection state.
        dialog = m_exportDialogs.value(*token).data();
    } else {
        dialog = m_exportDialog;
    }
    if (dialog == nullptr) {
        return;
    }
    const BridgeValue *error = listing.entry(QStringLiteral("error"));
    if (error != nullptr && error->type() == BridgeValue::Type::String) {
        dialog->applyListingError(error->toString());
        return;
    }
    const std::optional<QString> path = listing.optionalString(QStringLiteral("path"));
    const BridgeValue *entries = listing.entry(QStringLiteral("entries"));
    const BridgeValue *truncated = listing.entry(QStringLiteral("truncated"));
    if (!path.has_value() || entries == nullptr
            || entries->type() != BridgeValue::Type::Array
            || truncated == nullptr || truncated->type() != BridgeValue::Type::Boolean) {
        dialog->applyListingError(tr("Malformed export file listing"));
        return;
    }
    std::vector<ExportFileEntryData> parsed;
    for (const BridgeValue &raw : entries->arrayValues()) {
        const BridgeValue *directory = raw.entry(QStringLiteral("directory"));
        const BridgeValue *suggested = raw.entry(QStringLiteral("suggested"));
        if (raw.type() != BridgeValue::Type::Map
                || directory == nullptr || directory->type() != BridgeValue::Type::Boolean
                || suggested == nullptr || suggested->type() != BridgeValue::Type::Boolean) {
            dialog->applyListingError(tr("Malformed export file listing"));
            return;
        }
        const std::optional<QString> name = raw.optionalString(QStringLiteral("name"));
        const std::optional<QString> entryPath = raw.optionalString(QStringLiteral("path"));
        if (!name.has_value() || name->isEmpty()
                || !entryPath.has_value() || entryPath->isEmpty()) {
            dialog->applyListingError(tr("Malformed export file listing"));
            return;
        }
        parsed.push_back(ExportFileEntryData{*name, *entryPath,
                                             directory->toBoolean(),
                                             suggested->toBoolean()});
    }
    dialog->applyListing(*path, parsed, truncated->toBoolean());
}

void MainWindow::applyExportFilesError(const QString &message)
{
    if (m_exportDialog != nullptr) {
        m_exportDialog->applyListingError(message);
    }
}

void MainWindow::emitLaunch()
{
    const QString id = selectedInstanceId();
    if (!id.isEmpty()) {
        emit launchRequested(id);
        showNotification(tr("Launch"), tr("Requested %1").arg(id));
    }
}

void MainWindow::emitPluginAction()
{
    QListWidgetItem *item = m_contributions->currentItem();
    if (item == nullptr) {
        return;
    }
    const QString id = item->data(Qt::UserRole).toString();
    if (!id.isEmpty()) {
        emit pluginActionRequested(id);
        showNotification(tr("Plugin"), tr("Requested %1").arg(id));
    }
}
