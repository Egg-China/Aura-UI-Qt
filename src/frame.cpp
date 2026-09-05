// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Aura Launcher contributors

#include "frame.h"

#include <QtEndian>

#include <cstdio>
#include <cstring>
#include <mutex>

#if defined(Q_OS_WIN)
#include <fcntl.h>
#include <io.h>
#endif

namespace AuraFrame {
namespace {
std::mutex outputMutex;

bool readExact(unsigned char *target, std::size_t size, QString *error)
{
    std::size_t total = 0;
    while (total < size) {
        const std::size_t count = std::fread(target + total, 1, size - total, stdin);
        total += count;
        if (count == 0) {
            if (std::feof(stdin) != 0) {
                break;
            }
            if (error != nullptr) {
                *error = QStringLiteral("failed reading the aura.ui.v1 stdin transport");
            }
            return false;
        }
    }
    if (total != size && error != nullptr) {
        *error = QStringLiteral("aura.ui.v1 frame was truncated");
    }
    return total == size;
}
} // namespace

void initializeStandardHandles()
{
#if defined(Q_OS_WIN)
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stderr), _O_BINARY);
#endif
}

bool write(const QByteArray &body, QString *error)
{
    if (body.isEmpty() || static_cast<quint64>(body.size()) > kMaximumFrameBytes) {
        if (error != nullptr) {
            *error = QStringLiteral("outgoing aura.ui.v1 frame length is outside bounds");
        }
        return false;
    }
    const quint32 length = qToBigEndian(static_cast<quint32>(body.size()));
    std::lock_guard<std::mutex> guard(outputMutex);
    const bool header = std::fwrite(&length, sizeof(length), 1, stdout) == 1;
    const bool payload = header
        && std::fwrite(body.constData(), static_cast<std::size_t>(body.size()), 1, stdout) == 1;
    const bool flushed = payload && std::fflush(stdout) == 0;
    if (!flushed && error != nullptr) {
        *error = QStringLiteral("failed writing the aura.ui.v1 stdout transport");
    }
    return flushed;
}

std::optional<QByteArray> read(QString *error)
{
    unsigned char header[4] = {};
    if (!readExact(header, sizeof(header), error)) {
        return std::nullopt;
    }
    quint32 rawLength = 0;
    std::memcpy(&rawLength, header, sizeof(rawLength));
    const quint32 length = qFromBigEndian(rawLength);
    if (length == 0 || length > kMaximumFrameBytes) {
        if (error != nullptr) {
            *error = QStringLiteral("incoming aura.ui.v1 frame length is outside bounds");
        }
        return std::nullopt;
    }
    QByteArray body(static_cast<qsizetype>(length), Qt::Uninitialized);
    if (!readExact(reinterpret_cast<unsigned char *>(body.data()), length, error)) {
        return std::nullopt;
    }
    return body;
}
} // namespace AuraFrame
