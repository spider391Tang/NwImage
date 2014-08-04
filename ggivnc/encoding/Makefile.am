ggivnc_SOURCES += src/encoding/copyrect.c
ggivnc_SOURCES += src/encoding/corre.c
if HAVE_WMH
ggivnc_SOURCES += src/encoding/desktop-name.c
endif
ggivnc_SOURCES += src/encoding/desktop-size.c
ggivnc_SOURCES += src/encoding/hextile.c
if HAVE_GIISETNERVE
ggivnc_SOURCES += src/encoding/gii.c
endif
ggivnc_SOURCES += src/encoding/lastrect.c
ggivnc_SOURCES += src/encoding/raw.c
ggivnc_SOURCES += src/encoding/rre.c

if HAVE_ZLIB
ggivnc_SOURCES += src/encoding/tight.c
endif

ggivnc_SOURCES += src/encoding/wmvi.c

if HAVE_ZLIB
ggivnc_SOURCES += src/encoding/zlib.c
ggivnc_SOURCES += src/encoding/zlibhex.c
ggivnc_SOURCES += src/encoding/zrle.c
ggivnc_LDADD += -lz
if HAVE_JPEG
ggivnc_LDADD += -ljpeg
endif
endif
