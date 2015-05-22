#include <QtGui/QGuiApplication>
#include <QQmlContext>
#include <QTimer>
#include "VncImageProvider.h"
#include <QApplication>
#include <qquickimageprovider.h>
#include <QImage>
#include <QtQuick/QQuickView>
#include <QtQuick/QQuickItem>
#include <QThread>
#include <QDebug>
#include "VncThread.h"
#include <MLVNC.h>
#include "vncstop.h"


int main( int argc, char *argv[] )
{
    QGuiApplication app(argc, argv);
    const int w = 1920;
    const int h = 1080;


    VncImageProvider *imageProvider = new VncImageProvider( w, h, 3, QImage::Format_RGB888 );

    MLLibrary::MLVNC::getInstance()->init();
    MLLibrary::MLVNC::getInstance()->connect( "10.128.61.206" );
    MLLibrary::MLVNC::getInstance()->setScreenWidth( w );
    MLLibrary::MLVNC::getInstance()->setScreenHeight( h );
    MLLibrary::MLVNC::getInstance()->setFrameBufWidth( w );
    MLLibrary::MLVNC::getInstance()->setFrameBufHeight( h );
    MLLibrary::MLVNC::getInstance()->setColorFormat( MLLibrary::MLVNC::RGB888 );
    MLLibrary::MLVNC::getInstance()->setColorDepth( MLLibrary::MLVNC::MLVNC_24BIT );
    MLLibrary::MLVNC::getInstance()->connectToMlvncEvent( boost::bind( &VncImageProvider::slotNewFrameReady, imageProvider ) );
    MLLibrary::MLVNC::getInstance()->setFrameBufferPtr( imageProvider->getFrameBuffer() );
    
    QQuickView *viewer = new QQuickView();
    viewer->rootContext()->engine()->addImageProvider(QLatin1String("VncImageProvider"), imageProvider);
    viewer->rootContext()->setContextProperty("VncImageProvider", imageProvider);
    viewer->setSource(QStringLiteral("qrc:main.qml"));

    VncStop vncStop;
    QObject *rect = dynamic_cast<QObject*>(viewer->rootObject());
    QObject::connect( rect, SIGNAL(qmlSignal()),
                     &vncStop, SLOT( stopRender() ) );

    viewer->show();

    QThread workerThread;
    VncThread::getInstance()->moveToThread( &workerThread );
    QObject::connect( &workerThread, SIGNAL( started() ), VncThread::getInstance(), SLOT( startRender() ) );
    //QObject::connect( &workerThread, SIGNAL( finished() ), VncThread::getInstance(), SLOT( stopRender() ) );
    workerThread.start();

    //workerThread.terminate();


    return app.exec();
}
