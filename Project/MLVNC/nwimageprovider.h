#ifndef NWIMAGEPROVIDER_H
#define NWIMAGEPROVIDER_H

#include <QObject>
#include <QQuickImageProvider>

class NwImageProvider : public QObject, public QQuickImageProvider
{
    Q_OBJECT
public:
    NwImageProvider( int width, int height, QImage::Format format );
    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize);
    unsigned char* getFrameBuffer(){ return reinterpret_cast<unsigned char*>( mRawData.data() ); }

public slots:
    void slotNewFrameReady();

signals:
    Q_SIGNAL void signalNewFrameReady(int frameNumber);

private:
    QByteArray mRawData;
    QImage mImage;
};


#endif // NWIMAGEPROVIDER_H
