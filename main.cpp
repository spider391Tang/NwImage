#include <QtGui/QGuiApplication>
#include <QQmlContext>
#include <QTimer>
//#include "qtquick2applicationviewer.h"
#include "NwImageProvider.h"
#include <QApplication>
#include <QQmlEngine>
#include <qqmlengine.h>
#include <qquickimageprovider.h>
#include <QImage>
#include <QPainter>
#include <QScreen>
#include <QtQuick/QQuickView>
#include <QThread>
#include <QDebug>
#include "flyggi.h"


extern int ggi_main(int argc, char **argv);

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    NwImageProvider *imageProvider = new NwImageProvider;

    QQuickView *viewer = new QQuickView();
    viewer->rootContext()->engine()->addImageProvider(QLatin1String("NwImageProvider"), imageProvider);
    viewer->rootContext()->setContextProperty("NwImageProvider", imageProvider);
    viewer->setSource(QStringLiteral("qrc:main.qml"));
    viewer->show();

    //Flyggi* fly = new Flyggi;
    QObject::connect(Flyggi::instance(), SIGNAL(ggiReady(const QString&)), imageProvider, SLOT(slotNewFrameReady()));

    //QTimer timer;
    //QObject::connect(&timer, SIGNAL(timeout()), fly, SLOT(startFly()));
    //timer.start( 1000 );
    QTimer::singleShot( 1000, Flyggi::instance(), SLOT(startFly()));

    //timer.setInterval(100);
    //timer.setSingleShot(false);
    //timer.start();
    QThread workerThread;
    Flyggi::instance()->moveToThread( &workerThread );
    workerThread.start();

    // ggi_main( argc, argv );

    return app.exec();
}
