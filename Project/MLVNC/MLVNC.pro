TEMPLATE = app

QT += qml quick widgets

# CONFIG += c++11

SOURCES += main.cpp \
    MLLibraryBase.cpp \
    MLVNC.cpp \
    VncThread.cpp \
    VncImageProvider.cpp \
    vncstop.cpp

SOURCES += ../ggivnc/encoding/copyrect.c \
    ../ggivnc/encoding/corre.c \
    ../ggivnc/encoding/desktop-size.c \
    ../ggivnc/encoding/hextile.c \
    ../ggivnc/encoding/lastrect.c \
    ../ggivnc/encoding/raw.c \
    ../ggivnc/encoding/rre.c \
    ../ggivnc/encoding/tight.c \
    ../ggivnc/encoding/trle.c \
    ../ggivnc/encoding/wmvi.c \
    ../ggivnc/encoding/zlib.c \
    ../ggivnc/encoding/zlibhex.c \
    ../ggivnc/encoding/zrle.c \
    ../ggivnc/security/none.c \
    ../ggivnc/security/securitytight.c \
    ../ggivnc/security/vencrypt.c \
    ../ggivnc/security/vnc-auth.c \
    ../ggivnc/lib/giiEventPoll.c \
    ../ggivnc/lib/ggiCrossBlit.c \
    ../ggivnc/bandwidth.c \
    ../ggivnc/conn_none.c \
    ../ggivnc/handshake.c \
    ../ggivnc/option.c \
    ../ggivnc/pass_getpass.c \
    ../ggivnc/vnc.cpp


RESOURCES += qml.qrc

# Additional import path used to resolve QML modules in Qt Creator's code model
QML_IMPORT_PATH =

# Default rules for deployment.
include(deployment.pri)

HEADERS += \
    MLLibraryBase.h \
    MLVNC.h \
    ../ggivnc/config.h \
    ../ggivnc/d3des.h \
    ../ggivnc/vnc.h \
    VncThread.h \
    VncImageProvider.h \
    vncstop.h


INCLUDEPATH += /opt/local/include
# LIBS += -L../Hello -lHello
#LIBS += -L/Users/spider391tang/Projects/Mirrorlink-130/NwImage3/build-Project-Desktop_Qt_5_3_clang_64bit-Debug/ggivnc -lggivnc
#LIBS += /Users/spider391tang/Projects/Mirrorlink-130/NwImage3/build-Project-Desktop_Qt_5_3_clang_64bit-Debug/ggivnc/libggivnc.a
#LIBS += /Users/spider391tang/Projects/Mirrorlink-130/ggi-2.2.2-bundle/ggiconf/lib/libgg.a
#LIBS += /Users/spider391tang/Projects/Mirrorlink-130/ggi-2.2.2-bundle/ggiconf/lib/libgii.a
#LIBS += /Users/spider391tang/Projects/Mirrorlink-130/ggi-2.2.2-bundle/ggiconf/lib/libggi.a

macx: LIBS += -L/opt/local/lib -lgg -lgii -lggi -lz -lssl -lcrypto

INCLUDEPATH += ../ggivnc/
INCLUDEPATH += $$PWD/../../../ggi-2.2.2-bundle/ggiconf/lib/
INCLUDEPATH += $$PWD/../../../ggi-2.2.2-bundle/libggi-2.2.2/include
INCLUDEPATH += $$PWD/../../../ggi-2.2.2-bundle/libgii-1.0.2/include
DEPENDPATH += $$PWD/../../../ggi-2.2.2-bundle/ggiconf/lib
