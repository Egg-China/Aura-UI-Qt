// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Aura Launcher contributors

#ifndef AURA_UI_QT_RUNTIME_CONTROLLER_H
#define AURA_UI_QT_RUNTIME_CONTROLLER_H

#include "bridge_value.h"
#include "protocol.h"

#include <QObject>
#include <QString>

#include <unordered_map>

class MainWindow;
class QTimer;

class RuntimeController final : public QObject {
    Q_OBJECT

public:
    explicit RuntimeController(MainWindow *window, QObject *parent = nullptr);

    void start();
    int exitCode() const { return m_exitCode; }
    bool shutdownComplete() const { return m_shutdownComplete; }

private:
    void startReader();
    void handleFrame(const QByteArray &body);
    void handleLauncherRequest(const AuraProtocol::Envelope &request);
    void handleReply(const AuraProtocol::Envelope &reply);
    void sendReadyIfPossible();
    bool sendFrame(const QByteArray &body);
    bool sendResult(std::int64_t requestId, const BridgeValue &value);
    bool sendProtocolError(std::int64_t requestId, const QString &code, const QString &message);
    bool sendFrontendRequest(const QString &method, const BridgeValue &params);
    void requestLauncherState();
    void requestAuraCoreState();
    void startImportTaskPolling(const QString &taskId);
    void stopImportTaskPolling();
    void applyAuraCoreInstances(const BridgeValue &instances);
    void applyAuraCoreAccounts(const BridgeValue &accounts);
    BridgeValue normalizeAuraCoreInstance(const BridgeValue &raw) const;
    void failSession(const QString &reason);
    void finishSession();

    MainWindow *m_window = nullptr;
    std::unordered_map<std::int64_t, QString> m_pending;
    std::int64_t m_nextRequestId = 6;
    QString m_coreEngine = QStringLiteral("hmcl");
    QString m_importTaskId;
    QTimer *m_importPollTimer = nullptr;
    bool m_helloSeen = false;
    bool m_snapshotSeen = false;
    bool m_windowReady = false;
    bool m_readySent = false;
    bool m_shutdownComplete = false;
    bool m_failed = false;
    int m_exitCode = 1;
};

#endif // AURA_UI_QT_RUNTIME_CONTROLLER_H
