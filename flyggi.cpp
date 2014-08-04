#include "flyggi.h"
#include <QDebug>

extern int ggi_main(int argc, char **argv);
Flyggi* Flyggi::m_Instance = 0;

Flyggi::Flyggi(QObject *parent) :
    QObject(parent)
{
}

void Flyggi::startFly()
{
    qDebug() << "startFly";
    char* aa[] = {"i","i"};
    ggi_main( 2, aa);
}

void Flyggi::emitFly( const QByteArray& data )
{
    qDebug() << "emitFly";
    emit Flyggi::instance()->ggiReady("emit");
}

