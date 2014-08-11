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
#include <MLVNC.h>

void foo( const std::string& a )
{
    qDebug("%s", a.c_str() );
}


extern int ggi_main(int argc, char **argv);
extern void setFrameBuffer( unsigned char* buf );

int main(int argc, char *argv[])
{
    // Test MLVNC signal
    // MLLibrary::MLVNC* vnc = new MLLibrary::MLVNC;
    // vnc->connectToTestSignal( boost::bind( foo, _1 ) );
    // vnc->init();
    MLLibrary::MLVNC::getInstance()->init();

    QGuiApplication app(argc, argv);
    NwImageProvider *imageProvider = new NwImageProvider( 1920, 1080, QImage::Format_RGB16 );


    MLLibrary::MLVNC::getInstance()->register_vnc_events( boost::bind( &NwImageProvider::slotNewFrameReady, imageProvider ) );
    MLLibrary::MLVNC::getInstance()->setFrameBufferPtr( imageProvider->getFrameBuffer() );
    setFrameBuffer( imageProvider->getFrameBuffer() );
    

    QQuickView *viewer = new QQuickView();
    viewer->rootContext()->engine()->addImageProvider(QLatin1String("NwImageProvider"), imageProvider);
    viewer->rootContext()->setContextProperty("NwImageProvider", imageProvider);
    viewer->setSource(QStringLiteral("qrc:main.qml"));
    viewer->show();

    // QObject::connect(Flyggi::instance(), SIGNAL(ggiReady(const QString&)), imageProvider, SLOT(slotNewFrameReady()));

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
