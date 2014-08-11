#include "../ggivnc/config.h"
#include "MLVNC.h"
#include <QDebug>
#include "../ggivnc/MLVNCBuffer.h"

extern int ggivnc_main(int argc, char **argv);
extern void register_signal_handle_function( boost::function<void (const ggi_directbuffer*)> f );

namespace MLLibrary {

MLVNC* MLVNC::mInstance = NULL;


void MLVNC::startRender()
{
    qDebug() << "startFly";
    char* aa[] = {"ggivnc","10.128.60.135"};
    // MLLibrary::MLVNC* vnc = new MLLibrary::MLVNC;
    // set environment
    ggivnc_main( 2, aa);
}

void MLVNC::onHandleSignal( const ggi_directbuffer* db )
{
    //qDebug() << msg.c_str();
    
     qDebug() << "onHandleSignal";
    //int frameno = db->frame;
    //int ggiStride = db->buffer.plb.stride;
    //printf("frameno,stride,pixelsize = [%d,%d,%d]\n", frameno, ggiStride, db->buffer.plb.pixelformat->size );
    //qDebug() << "frameno,stride,pixelsize = " << frameno << "," << ggiStride << "," << db->buffer.plb.pixelformat->size;
     memcpy( mFrameBuffer, db->read, 1920*2*1080 );
    // ds.writeRawData( (const char*)db->read, ggiStride * 1080 );
    // Flyggi::instance()->getImage().loadFromData( (const uchar*)db->read, ggiStride*1080 );
    //QImage img( ( const uchar*)db->read, 1920, 1080, ggiStride, QImage::Format_RGB16 );

    //Flyggi::instance()->assignImage( img );
    //Flyggi::instance()->ggiReady("hello");
     qDebug() << "Calling mVNCEvent";
    mVNCEvent();
}

boost::signals2::connection MLVNC::register_vnc_events
    (
    const VNCSignalType::slot_type& aSlot
    )
{
    return mVNCEvent.connect( aSlot );
}

void MLVNC::setFrameBufferPtr( unsigned char* buffer )
{
    mFrameBuffer = buffer;
}

MLVNC::~MLVNC()
{
}

MLVNC::MLVNC()
{

}

void MLVNC::pwrp()
// don't delete the following line as it's needed to preserve source code of this autogenerated element
// section 127-0-1-1-584d88fa:144b573379b:-8000:0000000000000B84 begin
{
}
// section 127-0-1-1-584d88fa:144b573379b:-8000:0000000000000B84 end
// don't delete the previous line as it's needed to preserve source code of this autogenerated element

void MLVNC::init()
// don't delete the following line as it's needed to preserve source code of this autogenerated element
// section 127-0-1-1-584d88fa:144b573379b:-8000:0000000000000B86 begin
{

    BufferChangedHandler f = boost::bind( &MLVNC::onHandleSignal,this, _1 );
    register_signal_handle_function( f );

        //  boost::bind( &MLLibrary::MLVNC::onHandleSignal,MLLibrary::MLVNC::getInstance(), _1 ) );

}
// section 127-0-1-1-584d88fa:144b573379b:-8000:0000000000000B86 end
// don't delete the previous line as it's needed to preserve source code of this autogenerated element

void MLVNC::pwrdn()
// don't delete the following line as it's needed to preserve source code of this autogenerated element
// section 127-0-1-1-584d88fa:144b573379b:-8000:0000000000000B88 begin
{
}
// section 127-0-1-1-584d88fa:144b573379b:-8000:0000000000000B88 end
// don't delete the previous line as it's needed to preserve source code of this autogenerated element


} /* End of namespace MLLibrary */