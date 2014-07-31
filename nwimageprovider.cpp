#include "NwImageProvider.h"

NwImageProvider::NwImageProvider()
    : QQuickImageProvider(QQmlImageProviderBase::Image)
{
}

QImage NwImageProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    int width = 100;
    int height = 50;
    int requestNumber;
    QString strId = id;
    bool bIsOk;

    this->blockSignals(true);   // To allow debugging

    requestNumber = strId.toInt(&bIsOk);
    if (bIsOk)
    {
        switch (requestNumber % 4)
        {
        case 0: strId = "yellow"; break;
        case 1: strId = "red"; break;
        case 2: strId = "green"; break;
        case 3: strId = "blue"; break;
        default: strId = "black"; break;
        }
    }
    if (size)
        *size = QSize(width, height);
    QImage image(requestedSize.width() > 0 ? requestedSize.width() : width,
                   requestedSize.height() > 0 ? requestedSize.height() : height, QImage::Format_RGB32);
    image.fill(QColor(strId).rgba());

    this->blockSignals(false);

    return image;
}

void NwImageProvider::slotNewFrameReady()
{
    static int nextFrameNumber = 0;
    emit signalNewFrameReady(nextFrameNumber++);
}
