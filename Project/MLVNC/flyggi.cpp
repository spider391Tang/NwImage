#include "flyggi.h"
#include <QDebug>
#include "MLVNC.h"

extern int ggi_main(int argc, char **argv);
extern int ggivnc_main(int argc, char **argv);
extern int foo();

// ggivnc_main(int argc, char * const argv[])

Flyggi* Flyggi::m_Instance = 0;

Flyggi::Flyggi(QObject *parent) :
    QObject(parent)
{
}

void Flyggi::startFly()
{
    qDebug() << "startFly";
    char* aa[] = {"ggivnc","10.128.60.135"};
    // MLLibrary::MLVNC* vnc = new MLLibrary::MLVNC;
    // set environment
    ggivnc_main( 2, aa);
    //ggi_main( 2, aa);
    //foo();
}

void Flyggi::emitFly( const QByteArray& data )
{
    qDebug() << "emitFly";
    emit Flyggi::instance()->ggiReady("emit");
}

