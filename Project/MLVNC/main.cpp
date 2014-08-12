#include <QtGui/QGuiApplication>
#include <QQmlContext>
#include <QTimer>
#include "VncImageProvider.h"
#include <QApplication>
#include <qquickimageprovider.h>
#include <QImage>
#include <QtQuick/QQuickView>
#include <QThread>
#include <QDebug>
#include "VncThread.h"
#include <MLVNC.h>

int main( int argc, char *argv[] )
{
    MLLibrary::MLVNC::getInstance()->init();

    QGuiApplication app(argc, argv);
    VncImageProvider *imageProvider = new VncImageProvider( 1920, 1080, QImage::Format_RGB16 );


    MLLibrary::MLVNC::getInstance()->connectToMlvncEvent( boost::bind( &VncImageProvider::slotNewFrameReady, imageProvider ) );
    MLLibrary::MLVNC::getInstance()->setFrameBufferPtr( imageProvider->getFrameBuffer() );
    

    QQuickView *viewer = new QQuickView();
    viewer->rootContext()->engine()->addImageProvider(QLatin1String("VncImageProvider"), imageProvider);
    viewer->rootContext()->setContextProperty("VncImageProvider", imageProvider);
    viewer->setSource(QStringLiteral("qrc:main.qml"));
    viewer->show();

    QTimer::singleShot( 10, VncThread::getInstance(), SLOT( startRender() ) );

    QThread workerThread;
    VncThread::getInstance()->moveToThread( &workerThread );
    workerThread.start();

    return app.exec();
}
