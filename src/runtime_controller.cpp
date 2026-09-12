// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Aura Launcher contributors

#include "runtime_controller.h"
#include "frame.h"
#include "main_window.h"

#include <QCoreApplication>
#include <QPointer>
#include <QDateTime>
#include <QLocale>
#include <QTimer>

#include <thread>

RuntimeController::RuntimeController(MainWindow *window, QObject *parent)
    : QObject(parent), m_window(window)
{
    connect(m_window, &MainWindow::interfaceReady, this, [this]() {
        m_windowReady = true;
        sendReadyIfPossible();
    });
    connect(m_window, &MainWindow::launchRequested, this, [this](const QString &id) {
        sendFrontendRequest(QStringLiteral("core.instance.launch"),
                            BridgeValue::map({{QStringLiteral("id"), BridgeValue::string(id)}}));
    });
    connect(m_window, &MainWindow::importRequested, this,
            [this](const QString &source, const QString &name, const QString &group) {
        if (m_coreEngine != QStringLiteral("auracore")) {
            m_window->showNotification(tr("Import"),
                                       tr("Switch the launcher core to AuraCore first"));
            return;
        }
        std::vector<BridgeValueMapEntry> params;
        params.push_back({QStringLiteral("source"), BridgeValue::string(source)});
        params.push_back({QStringLiteral("name"), BridgeValue::string(name)});
        params.push_back({QStringLiteral("group"),
                          group.isEmpty() ? BridgeValue::nullValue() : BridgeValue::string(group)});
        sendFrontendRequest(QStringLiteral("core.auracore.instance.import"),
                            BridgeValue::map(std::move(params)));
    });
    connect(m_window, &MainWindow::exportRequested, this,
            [this](const QString &instanceId, const QString &output, const QString &name,
                   const QStringList &whitelist) {
        if (m_coreEngine == QStringLiteral("auracore")) {
            m_window->showNotification(
                tr("Export"),
                tr("MultiMC export targets launcher-side instances; switch to the HMCL engine"));
            return;
        }
        std::vector<BridgeValueMapEntry> params;
        params.push_back({QStringLiteral("id"), BridgeValue::string(instanceId)});
        params.push_back({QStringLiteral("output"), BridgeValue::string(output)});
        params.push_back({QStringLiteral("name"), BridgeValue::string(name)});
        if (!whitelist.isEmpty()) {
            std::vector<BridgeValue> paths;
            paths.reserve(static_cast<std::size_t>(whitelist.size()));
            for (const QString &path : whitelist) {
                paths.push_back(BridgeValue::string(path));
            }
            params.push_back({QStringLiteral("whitelist"), BridgeValue::array(std::move(paths))});
        }
        sendFrontendRequest(QStringLiteral("core.instance.export.multimc"),
                            BridgeValue::map(std::move(params)));
    });
    connect(m_window, &MainWindow::exportFilesRequested, this,
            [this](const QString &instanceId, const QString &path) {
        sendFrontendRequest(
            QStringLiteral("core.instance.export.files.list"),
            BridgeValue::map({{QStringLiteral("id"), BridgeValue::string(instanceId)},
                              {QStringLiteral("path"), BridgeValue::string(path)}}));
    });
    connect(m_window, &MainWindow::refreshRequested, this, [this]() {
        requestLauncherState();
    });
    connect(m_window, &MainWindow::pluginActionRequested, this, [this](const QString &id) {
        sendFrontendRequest(QStringLiteral("core.plugin.action"),
                            BridgeValue::map({{QStringLiteral("id"), BridgeValue::string(id)}}));
    });
    connect(m_window, &MainWindow::launcherShutdownRequested, this, [this]() {
        sendFrontendRequest(QStringLiteral("core.app.shutdown"), BridgeValue::nullValue());
    });
}

void RuntimeController::start()
{
    AuraFrame::initializeStandardHandles();
    m_window->show();
    startReader();
}

void RuntimeController::startReader()
{
    const QPointer<RuntimeController> alive(this);
    std::thread([alive]() {
        while (true) {
            QString transportError;
            std::optional<QByteArray> body = AuraFrame::read(&transportError);
            if (!body.has_value()) {
                if (transportError.isEmpty()) {
                    transportError = QStringLiteral("launcher stdin closed before ui.shutdown");
                }
                const QString reason = transportError;
                QMetaObject::invokeMethod(alive.data(), [alive, reason]() {
                    if (alive != nullptr) {
                        alive->failSession(reason);
                    }
                }, Qt::QueuedConnection);
                return;
            }
            QByteArray frame = *body;
            QMetaObject::invokeMethod(alive.data(), [alive, frame]() {
                if (alive != nullptr) {
                    alive->handleFrame(frame);
                }
            }, Qt::QueuedConnection);
        }
    }).detach();
}

bool RuntimeController::sendFrame(const QByteArray &body)
{
    QString error;
    if (!AuraFrame::write(body, &error)) {
        failSession(error);
        return false;
    }
    return true;
}

bool RuntimeController::sendResult(std::int64_t requestId, const BridgeValue &value)
{
    return sendFrame(AuraProtocol::encodeResult(requestId, value));
}

bool RuntimeController::sendProtocolError(std::int64_t requestId, const QString &code,
                                         const QString &message)
{
    return sendFrame(AuraProtocol::encodeError(requestId, code, message));
}

bool RuntimeController::sendFrontendRequest(const QString &method, const BridgeValue &params)
{
    const QByteArray body = AuraProtocol::encodeRequest(m_nextRequestId, method, params);
    if (body.isEmpty()) {
        failSession(QStringLiteral("failed encoding frontend request"));
        return false;
    }
    if (!sendFrame(body)) {
        return false;
    }
    m_pending.emplace(m_nextRequestId, method);
    m_nextRequestId += 2;
    return true;
}

void RuntimeController::requestLauncherState()
{
    if (m_pending.find(4) != m_pending.end()) {
        sendFrontendRequest(QStringLiteral("core.snapshot.get"), BridgeValue::nullValue());
        return;
    }
    const QByteArray body = AuraProtocol::encodeRequest(
        4, QStringLiteral("core.snapshot.get"), BridgeValue::nullValue());
    if (sendFrame(body)) {
        m_pending.emplace(4, QStringLiteral("core.snapshot.get"));
    }
}

void RuntimeController::sendReadyIfPossible()
{
    if (!m_helloSeen || !m_snapshotSeen || !m_windowReady || m_readySent) {
        return;
    }
    m_readySent = true;
    const QByteArray body = AuraProtocol::encodeRequest(
        2, QStringLiteral("ui.ready"), BridgeValue::nullValue());
    if (sendFrame(body)) {
        m_pending.emplace(2, QStringLiteral("ui.ready"));
    }
}
void RuntimeController::handleFrame(const QByteArray &body)
{
    QString error;
    const std::optional<AuraProtocol::Envelope> message = AuraProtocol::decode(body, true, &error);
    if (!message.has_value()) {
        failSession(error);
        return;
    }
    if (message->kind == AuraProtocol::Envelope::Kind::Request) {
        handleLauncherRequest(*message);
        return;
    }
    if (message->kind == AuraProtocol::Envelope::Kind::Result
            || message->kind == AuraProtocol::Envelope::Kind::Error) {
        handleReply(*message);
        return;
    }
    failSession(QStringLiteral("unsupported aura.ui.v1 envelope kind"));
}

void RuntimeController::handleLauncherRequest(const AuraProtocol::Envelope &request)
{
    if (request.method == QStringLiteral("ui.hello")) {
        QString error;
        if (m_helloSeen || !AuraProtocol::validateHello(request.params, &error)) {
            sendProtocolError(request.requestId, QStringLiteral("PROTOCOL"), error);
            return;
        }
        m_helloSeen = true;
        sendResult(request.requestId, request.params);
        sendReadyIfPossible();
        return;
    }
    if (request.method == QStringLiteral("ui.snapshot.replace")) {
        if (m_snapshotSeen) {
            sendProtocolError(request.requestId, QStringLiteral("PROTOCOL"),
                              QStringLiteral("duplicate ui.snapshot.replace"));
            return;
        }
        m_snapshotSeen = true;
        m_window->setSnapshot(request.params);
        sendResult(request.requestId, BridgeValue::nullValue());
        sendReadyIfPossible();
        return;
    }
    if (request.method == QStringLiteral("ui.navigate")) {
        QString route = QStringLiteral("unknown");
        if (request.params.type() == BridgeValue::Type::Map) {
            const BridgeValue *routeValue = request.params.entry(QStringLiteral("route"));
            if (routeValue == nullptr) {
                routeValue = request.params.entry(QStringLiteral("tab"));
            }
            if (routeValue != nullptr && routeValue->type() == BridgeValue::Type::String) {
                route = routeValue->toString();
            }
        }
        m_window->setRoute(route);
        sendResult(request.requestId, request.params);
        return;
    }
    if (request.method == QStringLiteral("ui.notify")) {
        QString title = tr("Aura");
        QString message = tr("Launcher notification");
        if (request.params.type() == BridgeValue::Type::Map) {
            const std::optional<QString> titleValue =
                request.params.optionalString(QStringLiteral("title"));
            const std::optional<QString> messageValue =
                request.params.optionalString(QStringLiteral("message"));
            if (titleValue.has_value()) {
                title = *titleValue;
            }
            if (messageValue.has_value()) {
                message = *messageValue;
            }
        }
        m_window->showNotification(title, message);
        sendResult(request.requestId, BridgeValue::nullValue());
        return;
    }
    if (request.method == QStringLiteral("ui.shutdown")) {
        if (sendResult(request.requestId, BridgeValue::nullValue())) {
            finishSession();
        }
        return;
    }
    sendProtocolError(request.requestId, QStringLiteral("UNSUPPORTED"),
                      QStringLiteral("unsupported launcher method: %1").arg(request.method));
}

void RuntimeController::handleReply(const AuraProtocol::Envelope &reply)
{
    const auto pending = m_pending.find(reply.requestId);
    if (pending == m_pending.end()) {
        failSession(QStringLiteral("unexpected aura.ui.v1 reply identifier"));
        return;
    }
    const QString method = pending->second;
    m_pending.erase(pending);
    if (reply.kind == AuraProtocol::Envelope::Kind::Error) {
        m_window->showNotification(tr("Command failed"),
                                   QStringLiteral("%1: %2").arg(reply.code, reply.message));
        return;
    }
    if (method == QStringLiteral("ui.ready")) {
        requestLauncherState();
        m_window->showNotification(tr("Aura"), tr("Qt interface is ready"));
        return;
    }
    if (method == QStringLiteral("core.snapshot.get")) {
        m_window->setSnapshot(reply.value);
        const BridgeValue *settings = reply.value.entry(QStringLiteral("settings"));
        const BridgeValue *engine = settings == nullptr
            ? nullptr
            : settings->entry(QStringLiteral("coreEngine"));
        if (engine != nullptr && engine->type() == BridgeValue::Type::String) {
            m_coreEngine = engine->toString();
        }
        if (m_coreEngine == QStringLiteral("auracore")) {
            requestAuraCoreState();
        }
        return;
    }
    if (method == QStringLiteral("core.auracore.instance.import")) {
        const BridgeValue *error = reply.value.entry(QStringLiteral("error"));
        if (error != nullptr && error->type() == BridgeValue::Type::String) {
            m_window->showNotification(tr("Import failed"), error->toString());
            return;
        }
        const BridgeValue *taskId = reply.value.entry(QStringLiteral("taskId"));
        if (taskId != nullptr && taskId->type() == BridgeValue::Type::String
            && !taskId->toString().isEmpty()) {
            startImportTaskPolling(taskId->toString());
            m_window->showNotification(tr("Import"), tr("Import task started"));
        } else {
            m_window->showNotification(tr("Import failed"),
                                       tr("The backend did not return a task id"));
        }
        return;
    }
    if (method == QStringLiteral("core.auracore.task.status")) {
        if (m_importTaskId.isEmpty()) {
            return;
        }
        const BridgeValue *state = reply.value.entry(QStringLiteral("state"));
        const BridgeValue *error = reply.value.entry(QStringLiteral("error"));
        if (state == nullptr || state->type() != BridgeValue::Type::String) {
            return;
        }
        const QString stateName = state->toString();
        if (stateName == QStringLiteral("succeeded")) {
            stopImportTaskPolling();
            m_window->showNotification(tr("Import"), tr("Import completed"));
            requestLauncherState();
        } else if (stateName == QStringLiteral("failed") || stateName == QStringLiteral("aborted")) {
            const QString detail = error != nullptr && error->type() == BridgeValue::Type::String
                ? error->toString()
                : stateName;
            stopImportTaskPolling();
            m_window->showNotification(tr("Import failed"), detail);
        }
        return;
    }
    if (method == QStringLiteral("core.instance.export.files.list")) {
        m_window->applyExportFiles(reply.value);
        return;
    }
    if (method == QStringLiteral("core.instance.export.multimc")) {
        const BridgeValue *error = reply.value.entry(QStringLiteral("error"));
        if (error != nullptr && error->type() == BridgeValue::Type::String) {
            m_window->showNotification(tr("Export failed"), error->toString());
            return;
        }
        const BridgeValue *output = reply.value.entry(QStringLiteral("output"));
        m_window->showNotification(tr("Export"),
                                   output != nullptr && output->type() == BridgeValue::Type::String
                                       ? tr("Archive written: %1").arg(output->toString())
                                       : tr("Archive written"));
        return;
    }
    if (method == QStringLiteral("core.auracore.instance.list")) {
        applyAuraCoreInstances(reply.value);
        return;
    }
    if (method == QStringLiteral("core.auracore.accounts.list")) {
        applyAuraCoreAccounts(reply.value);
        return;
    }
    m_window->showNotification(tr("Command complete"), method);
}

void RuntimeController::requestAuraCoreState()
{
    sendFrontendRequest(QStringLiteral("core.auracore.instance.list"), BridgeValue::nullValue());
    sendFrontendRequest(QStringLiteral("core.auracore.accounts.list"), BridgeValue::nullValue());
}

void RuntimeController::startImportTaskPolling(const QString &taskId)
{
    stopImportTaskPolling();
    m_importTaskId = taskId;
    m_importPollTimer = new QTimer(this);
    m_importPollTimer->setInterval(2000);
    connect(m_importPollTimer, &QTimer::timeout, this, [this]() {
        if (m_importTaskId.isEmpty()) {
            stopImportTaskPolling();
            return;
        }
        std::vector<BridgeValueMapEntry> params;
        params.push_back({QStringLiteral("taskId"), BridgeValue::string(m_importTaskId)});
        sendFrontendRequest(QStringLiteral("core.auracore.task.status"),
                            BridgeValue::map(std::move(params)));
    });
    m_importPollTimer->start();
}

void RuntimeController::stopImportTaskPolling()
{
    m_importTaskId.clear();
    if (m_importPollTimer != nullptr) {
        m_importPollTimer->stop();
        m_importPollTimer->deleteLater();
        m_importPollTimer = nullptr;
    }
}

void RuntimeController::applyAuraCoreInstances(const BridgeValue &instances)
{
    if (instances.type() == BridgeValue::Type::Map) {
        const BridgeValue *error = instances.entry(QStringLiteral("error"));
        if (error != nullptr && error->type() == BridgeValue::Type::String) {
            m_window->showNotification(tr("AuraCore"),
                                       tr("Instance list failed: %1").arg(error->toString()));
            return;
        }
    }
    if (instances.type() != BridgeValue::Type::Array) {
        return;
    }
    std::vector<BridgeValue> normalized;
    normalized.reserve(instances.arrayValues().size());
    for (const BridgeValue &raw : instances.arrayValues()) {
        if (raw.isMap()) {
            normalized.push_back(normalizeAuraCoreInstance(raw));
        }
    }
    m_window->applyInstances(BridgeValue::array(std::move(normalized)));
    m_window->showNotification(tr("AuraCore"), tr("Backend instances synchronized"));
}

void RuntimeController::applyAuraCoreAccounts(const BridgeValue &accounts)
{
    if (accounts.type() == BridgeValue::Type::Map) {
        const BridgeValue *error = accounts.entry(QStringLiteral("error"));
        if (error != nullptr && error->type() == BridgeValue::Type::String) {
            m_window->showNotification(tr("AuraCore"),
                                       tr("Account list failed: %1").arg(error->toString()));
            return;
        }
    }
    if (accounts.type() != BridgeValue::Type::Array) {
        return;
    }
    std::vector<BridgeValue> normalized;
    normalized.reserve(accounts.arrayValues().size());
    for (const BridgeValue &raw : accounts.arrayValues()) {
        if (!raw.isMap()) {
            continue;
        }
        std::vector<BridgeValueMapEntry> entries;
        const std::optional<QString> profileName = raw.optionalString(QStringLiteral("profileName"));
        if (profileName.has_value()) {
            entries.push_back({QStringLiteral("username"), BridgeValue::string(*profileName)});
        }
        const BridgeValue *defaultFlag = raw.entry(QStringLiteral("isDefault"));
        const bool isActive = defaultFlag != nullptr
            && defaultFlag->type() == BridgeValue::Type::Boolean
            && defaultFlag->toBoolean();
        entries.push_back({QStringLiteral("isActive"), BridgeValue::boolean(isActive)});
        normalized.push_back(BridgeValue::map(std::move(entries)));
    }
    m_window->applyAccounts(BridgeValue::array(std::move(normalized)));
}

BridgeValue RuntimeController::normalizeAuraCoreInstance(const BridgeValue &raw) const
{
    std::vector<BridgeValueMapEntry> entries;
    const auto copyString = [&raw, &entries](const QString &from, const QString &to) {
        const std::optional<QString> value = raw.optionalString(from);
        if (value.has_value() && !value->isEmpty()) {
            entries.push_back({to, BridgeValue::string(*value)});
        }
    };
    copyString(QStringLiteral("id"), QStringLiteral("id"));
    copyString(QStringLiteral("name"), QStringLiteral("name"));
    copyString(QStringLiteral("gameVersion"), QStringLiteral("version"));
    copyString(QStringLiteral("loader"), QStringLiteral("loader"));
    copyString(QStringLiteral("loaderVersion"), QStringLiteral("loaderVersion"));
    const BridgeValue *modCount = raw.entry(QStringLiteral("modCount"));
    if (modCount != nullptr
        && (modCount->type() == BridgeValue::Type::Integer
            || modCount->type() == BridgeValue::Type::Float)) {
        entries.push_back({QStringLiteral("modCount"), *modCount});
    }
    const BridgeValue *lastLaunch = raw.entry(QStringLiteral("lastLaunch"));
    if (lastLaunch != nullptr
        && (lastLaunch->type() == BridgeValue::Type::Integer
            || lastLaunch->type() == BridgeValue::Type::Float)
        && lastLaunch->toReal() > 0.0) {
        const QDateTime lastPlayed = QDateTime::fromMSecsSinceEpoch(
            static_cast<qint64>(lastLaunch->toReal()));
        entries.push_back({QStringLiteral("lastPlayed"),
                           BridgeValue::string(QLocale().toString(lastPlayed, QLocale::ShortFormat))});
    }
    const std::optional<QString> group = raw.optionalString(QStringLiteral("group"));
    if (group.has_value() && !group->isEmpty()) {
        entries.push_back({QStringLiteral("description"),
                           BridgeValue::string(tr("AuraCore group: %1").arg(*group))});
    }
    return BridgeValue::map(std::move(entries));
}

void RuntimeController::failSession(const QString &reason)
{
    if (m_failed || m_shutdownComplete) {
        return;
    }
    m_failed = true;
    m_exitCode = 1;
    m_window->showNotification(tr("Transport failure"), reason);
    QTimer::singleShot(0, qApp, &QCoreApplication::quit);
}

void RuntimeController::finishSession()
{
    m_shutdownComplete = true;
    m_exitCode = 0;
    QTimer::singleShot(0, qApp, &QCoreApplication::quit);
}
