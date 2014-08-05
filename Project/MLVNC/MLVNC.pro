TEMPLATE = app

QT += qml quick widgets

CONFIG += c++11

SOURCES += main.cpp \
    nwimageprovider.cpp \
    flyggi.cpp \
    MLLibraryBase.cpp \
    MLVNC.cpp

RESOURCES += qml.qrc

# Additional import path used to resolve QML modules in Qt Creator's code model
QML_IMPORT_PATH =

# Default rules for deployment.
include(deployment.pri)

HEADERS += \
    nwimageprovider.h \
    flyggi.h \
    MLLibraryBase.h \
    MLVNC.h

# LIBS += -L../Hello -lHello
LIBS += /Users/spider391tang/Projects/Mirrorlink-130/NwImage3/build-Project-Desktop_Qt_5_3_clang_64bit-Debug/Hello/libHello.a
LIBS += -L/Users/spider391tang/Projects/Mirrorlink-130/NwImage3/build-Project-Desktop_Qt_5_3_clang_64bit-Debug/ggivnc -lggivnc
#LIBS += /Users/spider391tang/Projects/Mirrorlink-130/NwImage3/build-Project-Desktop_Qt_5_3_clang_64bit-Debug/ggivnc/libggivnc.a
#LIBS += /Users/spider391tang/Projects/Mirrorlink-130/ggi-2.2.2-bundle/ggiconf/lib/libgg.a
#LIBS += /Users/spider391tang/Projects/Mirrorlink-130/ggi-2.2.2-bundle/ggiconf/lib/libgii.a
#LIBS += /Users/spider391tang/Projects/Mirrorlink-130/ggi-2.2.2-bundle/ggiconf/lib/libggi.a

macx: LIBS += -L/Users/spider391tang/Projects/Mirrorlink-130/ggi-2.2.2-bundle/ggiconf/lib/ -lgg -lgii -lggi -lz

INCLUDEPATH += $$PWD/../../../ggi-2.2.2-bundle/ggiconf/lib/
INCLUDEPATH += $$PWD/../../../ggi-2.2.2-bundle/libggi-2.2.2/include
INCLUDEPATH += $$PWD/../../../ggi-2.2.2-bundle/libgii-1.0.2/include
DEPENDPATH += $$PWD/../../../ggi-2.2.2-bundle/ggiconf/lib
