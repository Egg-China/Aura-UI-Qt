// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Aura Launcher contributors

#ifndef AURA_UI_QT_MAIN_WINDOW_H
#define AURA_UI_QT_MAIN_WINDOW_H

#include "bridge_value.h"

#include <QHash>
#include <QMainWindow>
#include <QPointer>
#include <QString>
#include <QStringList>

#include <vector>

class ExportDialog;
class QLabel;
class QListWidget;
class QPushButton;
class QTextBrowser;

class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

    void setSnapshot(const BridgeValue &snapshot);
    void applyInstances(const BridgeValue &instances);
    void applyAccounts(const BridgeValue &accounts);
    void setRoute(const QString &route);
    void showNotification(const QString &title, const QString &message);
    void applyExportFiles(const BridgeValue &listing);
    void applyExportFilesError(const QString &message);

signals:
    void interfaceReady();
    void launchRequested(const QString &instanceId);
    void importRequested(const QString &source, const QString &name, const QString &group);
    void exportRequested(const QString &instanceId, const QString &output, const QString &name,
                         const QStringList &whitelist);
    void exportFilesRequested(const QString &instanceId, const QString &token, const QString &path);
    void refreshRequested();
    void pluginActionRequested(const QString &actionId);
    void launcherShutdownRequested();

protected:
    void showEvent(QShowEvent *event) override;

private slots:
    void updateSelection();
    void emitLaunch();
    void emitImport();
    void emitExport();
    void emitPluginAction();

private:
    struct Instance {
        QString id;
        QString name;
        QString version;
        QString loader;
        QString lastPlayed;
        QString playTime;
        QString modCount;
        QString description;
        bool favorite = false;
    };

    void buildInterface();
    void renderInstances(const BridgeValue &instances);
    void renderAccounts(const BridgeValue &accounts);
    void renderContributions(const BridgeValue &contributions);
    void updateDetails();
    QString selectedInstanceId() const;

    QListWidget *m_instances = nullptr;
    QListWidget *m_contributions = nullptr;
    QTextBrowser *m_details = nullptr;
    QLabel *m_account = nullptr;
    QLabel *m_route = nullptr;
    QLabel *m_status = nullptr;
    QPushButton *m_launch = nullptr;
    QPushButton *m_import = nullptr;
    QPushButton *m_export = nullptr;
    QPushButton *m_refresh = nullptr;
    QPushButton *m_shutdown = nullptr;
    QPointer<ExportDialog> m_exportDialog;
    QHash<QString, QPointer<ExportDialog>> m_exportDialogs;
    std::vector<Instance> m_models;
    bool m_readyEmitted = false;
};

#endif // AURA_UI_QT_MAIN_WINDOW_H
