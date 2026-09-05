// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Aura Launcher contributors

#ifndef AURA_UI_QT_FRAME_H
#define AURA_UI_QT_FRAME_H

#include <QByteArray>
#include <QString>

#include <optional>

namespace AuraFrame {
constexpr quint32 kMaximumFrameBytes = 16U * 1024U * 1024U;

void initializeStandardHandles();
bool write(const QByteArray &body, QString *error = nullptr);
std::optional<QByteArray> read(QString *error = nullptr);
} // namespace AuraFrame

#endif // AURA_UI_QT_FRAME_H
