#include "VncImageProvider.h"

VncImageProvider::VncImageProvider( int width, int height, int depth ,QImage::Format format )
    : QQuickImageProvider(QQmlImageProviderBase::Image)
    , mRawData( width*height*depth, '/0' )
    , mImage( reinterpret_cast<uchar*>( mRawData.data() ), width, height, width*depth, format )
    , mWidth( width )
    , mHeight( height )
    , mFormat( format )
{

}

QImage VncImageProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    return mImage;
}

void VncImageProvider::slotNewFrameReady()
{
    static int nextFrameNumber = 0;
    emit signalNewFrameReady( nextFrameNumber++ );
}
