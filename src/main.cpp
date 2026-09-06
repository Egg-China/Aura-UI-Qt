// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Aura Launcher contributors

#include "frame.h"
#include "main_window.h"
#include "runtime_controller.h"

#include <QApplication>
#include <QStringList>

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("Aura Launcher"));
    QApplication::setApplicationName(QStringLiteral("Aura UI Qt"));
    QApplication::setApplicationVersion(QStringLiteral("0.1.0"));

    const QStringList arguments = QCoreApplication::arguments();
    if (arguments.size() != 2 || arguments.at(1) != QStringLiteral("--stdio")) {
        return 2;
    }

    AuraFrame::initializeStandardHandles();
    MainWindow window;
    RuntimeController controller(&window);
    controller.start();
    const int status = application.exec();
    return controller.shutdownComplete() ? controller.exitCode() : status == 0 ? 1 : status;
}
