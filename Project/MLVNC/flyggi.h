#ifndef FLYGGI_H
#define FLYGGI_H

#include <QObject>
#include <QMutex>
#include <QImage>

class Flyggi : public QObject
{
    Q_OBJECT
public:
    explicit Flyggi(QObject *parent = 0);
    static void emitFly( const QByteArray& data );
    static Flyggi* instance()
        {
            static QMutex mutex;
            if (!m_Instance)
            {
                mutex.lock();

                if (!m_Instance)
                    m_Instance = new Flyggi;

                mutex.unlock();
            }

            return m_Instance;
        }
    void assignImage(const QImage& img ) { mImage = img; }
    QImage& getImage() { return mImage; }
signals:
    void ggiReady( const QString& result);
public slots:
    void startFly();

private:
    QImage mImage;
private:
    static Flyggi* m_Instance;
};

#endif // FLYGGI_H
