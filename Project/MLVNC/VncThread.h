#ifndef VNCTHREAD_H
#define VNCTHREAD_H

#include <QObject>
#include <QMutex>

class VncThread : public QObject
{
    Q_OBJECT
public:
    explicit VncThread( QObject *parent = 0 );
    static VncThread* getInstance()
        {
            static QMutex mutex;
            if (!mInstance)
            {
                mutex.lock();

                if (!mInstance)
                    mInstance = new VncThread;

                mutex.unlock();
            }

            return mInstance;
        }

public slots:
    void startRender();

private:
    static VncThread* mInstance;
};

#endif // VNCTHREAD_H
