#ifndef VNCSTOP_H
#define VNCSTOP_H

#include <QObject>

class VncStop : public QObject
{
    Q_OBJECT
public:
    explicit VncStop(QObject *parent = 0);

signals:

public slots:
    void stopRender();

};

#endif // VNCSTOP_H
