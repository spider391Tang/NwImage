#include "VncThread.h"
#include <QDebug>
#include "MLVNC.h"

VncThread* VncThread::mInstance = 0;

VncThread::VncThread(QObject *parent) :
    QObject(parent)
{
}

void VncThread::startRender()
{
    MLLibrary::MLVNC::getInstance()->startRender();
}

void VncThread::stopRender()
{
    qDebug() << "[VncThread] stopRender";
    MLLibrary::MLVNC::getInstance()->stopRender();
}
