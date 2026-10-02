#include <QApplication>
#include <QDir>
#include <QQmlApplicationEngine>
#include <QQuickStyle>

// True if the KDE desktop Quick Controls style (KF6 QQC2 Desktop Style) can be
// found on the QML import path.
static bool kdeDesktopStyleInstalled(const QQmlApplicationEngine& engine) {
    for (const QString& path : engine.importPathList()) {
        if (QDir(path).exists(QStringLiteral("org/kde/desktop/qmldir")))
            return true;
    }
    return false;
}

int main(int argc, char* argv[]) {
    // QApplication (not QGuiApplication): the KDE desktop Quick Controls style
    // needs the widgets platform integration.
    QApplication app(argc, argv);
    app.setApplicationName("UnnamedGpl3QtApp");

    QQmlApplicationEngine engine;

    // Same look as Genexis: the KDE desktop style, falling back to Qt's default
    // style when it is not installed.
    if (qEnvironmentVariableIsEmpty("QT_QUICK_CONTROLS_STYLE") && kdeDesktopStyleInstalled(engine))
        QQuickStyle::setStyle(QStringLiteral("org.kde.desktop"));

    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadFromModule("org.exposuremg.unnamed", "Main");
    return app.exec();
}
