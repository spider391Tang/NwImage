#include "vncstop.h"
#include <QDebug>
#include <MLVNC.h>

VncStop::VncStop(QObject *parent) :
    QObject(parent)
{
    
}


void VncStop::stopRender()
{
    qDebug() << "VncStop::stopRender";
    MLLibrary::MLVNC::getInstance()->stopRender();
    
}
