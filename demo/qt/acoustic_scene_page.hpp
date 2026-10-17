#pragma once
#include <cstdint>
class QWidget;
class QDialog;
class QString;

// Shares the exact interactive playback widget with the headless GUI acceptance fixture.
QDialog* createRecordingPlaybackDialog(QWidget* parent, std::uint64_t id,
                                        const QString& sourcePath,
                                        std::uint64_t sampleCount);

// An ODAS Studio-inspired 3D direction viewer. No dependency on Electron.
QWidget* createAcousticScenePage(QWidget* parent);
