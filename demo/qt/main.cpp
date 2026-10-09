#include "demo_window.hpp"

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

    window.show();
    return app.exec();
}
