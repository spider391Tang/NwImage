TEMPLATE = app

QT += qml quick widgets

SOURCES += main.cpp \
    nwimageprovider.cpp \
    flying_ggi.cpp \
    flyggi.cpp \
    ggivnc/encoding/copyrect.c \
    ggivnc/encoding/corre.c \
    ggivnc/encoding/desktop-size.c \
    ggivnc/encoding/hextile.c \
    ggivnc/encoding/lastrect.c \
    ggivnc/encoding/raw.c \
    ggivnc/encoding/rre.c \
    ggivnc/encoding/tight.c \
    ggivnc/encoding/wmvi.c \
    ggivnc/encoding/zlib.c \
    ggivnc/encoding/zlibhex.c \
    ggivnc/encoding/zrle.c \
    ggivnc/bandwidth.c \
    ggivnc/conn_none.c \
    ggivnc/d3des.c \
    ggivnc/pass_getpass.c \
    ggivnc/vnc.cpp


RESOURCES += qml.qrc

# Additional import path used to resolve QML modules in Qt Creator's code model
QML_IMPORT_PATH =

# Default rules for deployment.
include(deployment.pri)

HEADERS += \
    nwimageprovider.h \
    flyggi.h \
    ggivnc/config.h \
    ggivnc/d3des.h \
    ggivnc/vnc.h

macx: LIBS += -L$$PWD/../ggi-2.2.2-bundle/ggiconf/lib/ -lgg -lgii -lggi -lz

INCLUDEPATH += $$PWD/../ggi-2.2.2-bundle/ggiconf/lib/
INCLUDEPATH += $$PWD/../ggi-2.2.2-bundle/libggi-2.2.2/include
INCLUDEPATH += $$PWD/../ggi-2.2.2-bundle/libgii-1.0.2/include
INCLUDEPATH += $$PWD/ggivnc
DEPENDPATH += $$PWD/../ggi-2.2.2-bundle/ggiconf/lib
