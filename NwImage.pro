TEMPLATE = app

QT += qml quick widgets

SOURCES += main.cpp \
    nwimageprovider.cpp \
    flying_ggi.cpp \
    flyggi.cpp


RESOURCES += qml.qrc

# Additional import path used to resolve QML modules in Qt Creator's code model
QML_IMPORT_PATH =

# Default rules for deployment.
include(deployment.pri)

HEADERS += \
    nwimageprovider.h \
    flyggi.h

macx: LIBS += -L$$PWD/../ggi-2.2.2-bundle/ggiconf/lib/ -lgg -lgii -lggi

INCLUDEPATH += $$PWD/../ggi-2.2.2-bundle/ggiconf/lib/
INCLUDEPATH += $$PWD/../ggi-2.2.2-bundle/libggi-2.2.2/include
INCLUDEPATH += $$PWD/../ggi-2.2.2-bundle/libgii-1.0.2/include
DEPENDPATH += $$PWD/../ggi-2.2.2-bundle/ggiconf/lib
