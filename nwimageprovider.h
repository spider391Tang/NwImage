#ifndef NWIMAGEPROVIDER_H
#define NWIMAGEPROVIDER_H

#include <QObject>
#include <QQuickImageProvider>

class NwImageProvider : public QObject, public QQuickImageProvider
{
    Q_OBJECT
public:
    NwImageProvider();
    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize);

public slots:
    void slotNewFrameReady();

signals:
    Q_SIGNAL void signalNewFrameReady(int frameNumber);

};


#endif // NWIMAGEPROVIDER_H
