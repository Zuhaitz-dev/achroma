#include "src/splash.h"
#include "src/window.h"
#include <QApplication>
#include <QTimer>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setOrganizationName("achroma");
    app.setApplicationName("achroma");
    app.setDesktopFileName("Achroma");
    app.setWindowIcon(QIcon(":/achroma.svg"));

    auto* splash = new SplashScreen();
    auto* timer = new QTimer(&app);
    timer->setSingleShot(true);
    QObject::connect(
        timer,
        &QTimer::timeout,
        splash,
        [splash, &app]()
        {
            splash->showSplash(
                [&app]()
                {
                    auto* w = new AchromaWindow();
                    w->show();
                }
            );
        }
    );
    timer->start(50);

    return app.exec();
}
