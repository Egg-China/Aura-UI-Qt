// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Aura Launcher contributors

#include "frame.h"
#include "main_window.h"
#include "runtime_controller.h"

#include <QApplication>
#include <QCommandLineParser>

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("Aura Launcher"));
    QApplication::setApplicationName(QStringLiteral("Aura UI Qt"));
    QApplication::setApplicationVersion(QStringLiteral("0.1.0"));

    QCommandLineParser parser;
    parser.setOptionsAfterPositionalArgumentsMode(QCommandLineParser::ParseAsPositionalArguments);
    parser.addHelpOption();
    parser.process(application.arguments());
    if (parser.positionalArguments() != QStringList{QStringLiteral("--stdio")}) {
        return 2;
    }

    AuraFrame::initializeStandardHandles();
    MainWindow window;
    RuntimeController controller(&window);
    controller.start();
    const int status = application.exec();
    return controller.shutdownComplete() ? controller.exitCode() : status == 0 ? 1 : status;
}
