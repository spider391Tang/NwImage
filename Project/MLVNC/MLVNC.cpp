#include "../ggivnc/config.h"
#include "MLVNC.h"
#include <QDebug>
#include "../ggivnc/MLVNCBuffer.h"
#include <stdlib.h>
#include <boost/lexical_cast.hpp>

#define COUNT_OF_ARRAY( arr ) ( sizeof( arr ) / sizeof( arr[0] ) )

extern int ggivnc_main( int argc, char *argv[] );
extern int ggi_main(int argc, char **argv);
extern void setGgivncTargetFrameBuffer( unsigned char* buf );
extern void setFlyggiTargetFrameBuffer( unsigned char* buf );
extern void setGgivncPixFormat( const std::string& pixformat );
extern void setFlyggiPixFormat( const std::string& pixformat );
extern void setGgivncColorDepth( const std::string& colorDepth );

extern boost::signals2::connection connectToGgivncBufferRenderedSignal
    (
    const BufferRenderedSignalType::slot_type& aSlot
    );

extern boost::signals2::connection connectToFlyggiBufferRenderedSignal
    (
    const BufferRenderedSignalType::slot_type& aSlot
    );

namespace MLLibrary {

MLVNC* MLVNC::mInstance = NULL;

void MLVNC::startRender()
{
    // set color format
    switch( mColorFormat )
    {
    case RGB16:
        setGgivncPixFormat( "r5g6b5" );
        break;
    case RGB888:
        // Note: libggi is not endian-independent as RGB888
        // Need to reverse the order and may has endian order problem.
        // TODO: add cobra to determnie the order
        setGgivncPixFormat( "b8g8r8" );
        // setFlyggiPixFormat( "r8g8b8" );
        break;
    case RGB32:
        setGgivncPixFormat( "p8r8g8b8" );
        // setFlyggiPixFormat( "p8r8g8b8" );
        break;
    default:
        setGgivncPixFormat( "r5g6b5" );
        break;
    }

    // Set GGI_DEFMODE environment
    // Set visible size of the visual
    std::string ggiDefmode = "S ";
    ggiDefmode += " ";
    ggiDefmode += boost::lexical_cast<std::string>( mScreenWidth );
    ggiDefmode += "x";
    ggiDefmode += boost::lexical_cast<std::string>( mScreenHeight );

    ggiDefmode += " V ";
    ggiDefmode += boost::lexical_cast<std::string>( mFrameBufferWidth );
    ggiDefmode += "x";
    ggiDefmode += boost::lexical_cast<std::string>( mFrameBufferHeight );
    ggiDefmode += " [";

    switch( mColorDepth )
    {
    case MLVNC_1BIT:
        ggiDefmode += "GT_1BIT";
        break;
    case MLVNC_2BIT:
        ggiDefmode += "GT_2BIT";
        break;
    case MLVNC_4BIT:
        ggiDefmode += "GT_4BIT";
        break;
    case MLVNC_8BIT:
        ggiDefmode += "GT_8BIT";
        break;
    case MLVNC_15BIT:
        ggiDefmode += "GT_15BIT";
        break;
    case MLVNC_16BIT:
        ggiDefmode += "GT_16BIT";
        break;
    case MLVNC_24BIT:
        ggiDefmode += "GT_24BIT";
        break;
    case MLVNC_32BIT:
        ggiDefmode += "GT_32BIT";
        break;
    default:
        ggiDefmode += "GT_16BIT";
        break;
    }
    ggiDefmode += "]";
    
    setenv( "GGI_DEFMODE", ggiDefmode.c_str() , 1 );

    qDebug() << "[MLVNC] startRender: " << ggiDefmode.c_str();


    std::string serverAddr( mHost + "::" + boost::lexical_cast<std::string>( mPort ) );
    char* ggivncArgv[] =
    { 
        "ggivnc",
        const_cast<char*>( serverAddr.c_str() )
    };
    ggivnc_main( COUNT_OF_ARRAY( ggivncArgv ) , ggivncArgv );
    // set environment
    //ggi_main( 2, aa);
}

void MLVNC::stopRender()
{

}

void MLVNC::onHandleGgivncSignal()
{
     qDebug() << "Calling mVncEvent";
     mVncEvent();
}

void MLVNC::setFrameBufWidth( int width )
{
    mFrameBufferWidth = width;
}

void MLVNC::setFrameBufHeight( int height )
{
    mFrameBufferHeight = height;
}

void MLVNC::setScreenWidth( int width )
{
    mScreenWidth = width;
}

void MLVNC::setScreenHeight( int height )
{
    mScreenHeight = height;
}
boost::signals2::connection MLVNC::connectToMlvncEvent
    (
    const VNCSignalType::slot_type& aSlot
    )
{
    return mVncEvent.connect( aSlot );
}

void MLVNC::connect( const std::string& aHost, int aPort )
{
    mHost = aHost;
    mPort = aPort;
}

void MLVNC::setUpdateFPS( int frame_per_second )
{
    mFps = frame_per_second;
}


void MLVNC::repaint()
{
    
}

void MLVNC::disconnect()
{

}

void MLVNC::setFrameBufferPtr( unsigned char* buffer )
{
    setGgivncTargetFrameBuffer( buffer );
    setFlyggiTargetFrameBuffer( buffer );
}

void MLVNC::setColorDepth( MLVNCColorDepth color_depth )
{
    mColorDepth = color_depth;
}

void MLVNC::setColorFormat( MLVNCColorFormat color_format )
{
    mColorFormat = color_format;
}

MLVNC::~MLVNC()
{

}

MLVNC::MLVNC()
    : mColorFormat( RGB16 )
    , mColorDepth( MLVNC_16BIT )
    , mScreenWidth( 1920 )
    , mScreenHeight( 1080 )
    , mFrameBufferWidth( 1920 )
    , mFrameBufferHeight( 1080 )
{

}

void MLVNC::pwrp()
{
}

void MLVNC::init()
{
    connectToGgivncBufferRenderedSignal( boost::bind( &MLVNC::onHandleGgivncSignal,this ) );
    // connectToFlyggiBufferRenderedSignal( boost::bind( &MLVNC::onHandleGgivncSignal,this ) );
}

void MLVNC::pwrdn()
{
}

} /* End of namespace MLLibrary */
