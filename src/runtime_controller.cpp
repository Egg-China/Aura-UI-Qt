// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Aura Launcher contributors

#include "runtime_controller.h"
#include "frame.h"
#include "main_window.h"

#include <QCoreApplication>
#include <QPointer>
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
        return;
    }
    m_window->showNotification(tr("Command complete"), method);
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
