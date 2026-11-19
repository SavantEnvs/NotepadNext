// Shared setup for the Qt-based harnesses: one offscreen QApplication for the whole process.
// The editor widgets need a QApplication; it is created once and holds no per-input state.
#pragma once

#include <QApplication>
#include <QtGlobal>

inline void nnFuzzInitQt() {
    static int argc = 1;
    static char arg0[] = "notepadnext-fuzz";
    static char *argv[] = {arg0, nullptr};
    qputenv("QT_QPA_PLATFORM", "offscreen");
    // The app logs with qInfo/qDebug/qWarning on every file read; silence it so the fuzzer's output
    // stays readable (logging is not under test).
    qputenv("QT_LOGGING_RULES", "*.debug=false;*.info=false;*.warning=false");
    static QApplication *app = new QApplication(argc, argv);
    (void)app;
}
