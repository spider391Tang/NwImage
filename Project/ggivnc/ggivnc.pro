TEMPLATE = lib

SOURCES += encoding/copyrect.c \
    encoding/corre.c \
    encoding/desktop-size.c \
    encoding/hextile.c \
    encoding/lastrect.c \
    encoding/raw.c \
    encoding/rre.c \
    encoding/tight.c \
    encoding/wmvi.c \
    encoding/zlib.c \
    encoding/zlibhex.c \
    encoding/zrle.c \
    bandwidth.c \
    conn_none.c \
    d3des.c \
    pass_getpass.c \
    vnc.cpp

HEADERS += config.h \
    d3des.h \
    vnc.h

macx: LIBS += -L$$PWD/../../../ggi-2.2.2-bundle/ggiconf/lib/ -lgg -lgii -lggi -lz

INCLUDEPATH += $$PWD/../../../ggi-2.2.2-bundle/ggiconf/lib/
INCLUDEPATH += $$PWD/../../../ggi-2.2.2-bundle/libggi-2.2.2/include
INCLUDEPATH += $$PWD/../../../ggi-2.2.2-bundle/libgii-1.0.2/include
DEPENDPATH += $$PWD/../../../ggi-2.2.2-bundle/ggiconf/lib
