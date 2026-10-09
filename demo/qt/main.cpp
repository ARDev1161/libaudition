#include "demo_window.hpp"
#include "model_fixture_selftest.hpp"

#include <QApplication>

#include <iostream>

int main(int argc, char** argv) {
    QApplication app{argc, argv};

    DemoWindow window;

    if (app.arguments().contains("--self-test")) {
        QString report;
        const bool ok = window.runSelfTest(&report);
        std::cout << report.toStdString() << std::endl;
        return ok ? 0 : 1;
    }

    for (const auto& argument : app.arguments()) {
        if (argument.startsWith("--model-self-test=")) {
            QString report;
            const bool ok = runQtModelFixtureSelfTest(
                window,
                argument.mid(QStringLiteral("--model-self-test=").size()),
                &report);
            std::cout << report.toStdString() << std::endl;
            return ok ? 0 : 1;
        }
    }

    window.show();
    return app.exec();
}
