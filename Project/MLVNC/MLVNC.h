//****************************************************************************
//
//  HEADER NAME:
//      MLVNC.h
//
// Copyright 2014-2015 by Garmin Ltd. or its subsidiaries.
//****************************************************************************

#ifndef MLLibrary_MLVNC_h
#define MLLibrary_MLVNC_h

#include "MLLibraryBase.h"
#include <ggi/ggi.h>
#include <functional>
#include <string>
#include <boost/signals2/signal.hpp>
#include <boost/signals2/connection.hpp>

namespace MLLibrary {

class MLVNC: virtual public MLLibraryBase {


    //------------------------------------------------------------------------
    // Types 
    //------------------------------------------------------------------------
public:    
    typedef boost::signals2::signal <void()> VNCSignalType;
    typedef boost::function<void()> VNCHandler;

    enum MLVNCColorDepth
    {
        MLVNC_1BIT,
        MLVNC_2BIT,
        MLVNC_4BIT,
        MLVNC_8BIT,
        MLVNC_15BIT,
        MLVNC_16BIT,
        MLVNC_24BIT,
        MLVNC_32BIT
    };

    enum MLVNCColorFormat
    {
       RGB16,   // RGB565
       RGB888,
       RGB32    // XRGB8888
    };

    //------------------------------------------------------------------------
    // Functions
    //------------------------------------------------------------------------

public:
    virtual void pwrp();
    virtual void init();
    virtual void pwrdn();
    static MLVNC* getInstance()
    {
        if( !mInstance )
        {
            mInstance = new MLVNC;
        }
        return mInstance;
    }

public:
    MLVNC();
    virtual ~MLVNC();
    void connect( const std::string& aHost, int aPort = 5900 );
    void disconnect();
    void startRender();
    void stopRender();
    void setUpdateFPS(int frame_per_second);
    void repaint();
    void setFrameBufWidth( int width );
    void setFrameBufHeight( int height );
    void setScreenWidth( int width );
    void setScreenHeight( int height );
    void setColorDepth( MLVNCColorDepth color_depth );
    void setColorFormat( MLVNCColorFormat color_format );
    void setFrameBufferPtr( unsigned char* buffer );
    //void sendKeyEvents(int key_down, int key_code, int key_extra = 0);
    //void sendPointerEvents(int buttons, int x, int y);
    void onHandleGgivncSignal();
    boost::signals2::connection connectToMlvncEvent( const VNCSignalType::slot_type& aSlot );
    
private:

    //! callback variable for connectToMlvncEvent to report vnc event ids
    // std::function<void(int)> mVncEventCb;
    static MLVNC* mInstance;
    unsigned char* mFrameBuffer;
    VNCSignalType mVncEvent;
    int mFrameBufferWidth;
    int mFrameBufferHeight;
    int mScreenWidth;
    int mScreenHeight;
    MLVNCColorDepth mColorDepth;
    MLVNCColorFormat mColorFormat;
    std::string mHost;
    int mPort;
    int mFps;
};

} /* End of namespace MLLibrary */

#endif // MLLibrary_MLVNC_h
