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


int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QTimer timer;
    NwImageProvider *imageProvider = new NwImageProvider;

    QQuickView *viewer = new QQuickView();
    viewer->rootContext()->engine()->addImageProvider(QLatin1String("NwImageProvider"), imageProvider);
    viewer->rootContext()->setContextProperty("NwImageProvider", imageProvider);
    viewer->setSource(QStringLiteral("qrc:main.qml"));
    viewer->show();

    QObject::connect(&timer, SIGNAL(timeout()), imageProvider, SLOT(slotNewFrameReady()));
    timer.setInterval(100);
    timer.setSingleShot(false);
    timer.start();

    return app.exec();
}
