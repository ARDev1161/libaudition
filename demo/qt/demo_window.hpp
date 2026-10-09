#pragma once

#include <QMainWindow>
#include <QString>

class QWidget;

class DemoWindow final : public QMainWindow {
public:
    explicit DemoWindow(QWidget* parent = nullptr);

    [[nodiscard]] bool runSelfTest(QString* report = nullptr);

private:
    QWidget* createOverviewTab();
    QWidget* createAudioDspTab();
    QWidget* createSpatialTab();
    QWidget* createSmoothingTab();
    QWidget* createIdentityTab();
    QWidget* createEventsTab();
    QWidget* createPipelineTab();
    QWidget* createCapabilitiesTab();
};
