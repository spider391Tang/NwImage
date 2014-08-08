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

#include <functional>
#include <string>
#include <boost/signals2/signal.hpp>
#include <boost/signals2/connection.hpp>

namespace MLLibrary {

class MLVNC: virtual public MLLibraryBase {


    //------------------------------------------------------------------------
    // Types 
    //------------------------------------------------------------------------
    

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
    //void connect(std::string& host, int port);
    //void disconnect();
    void startRender();
    //void stopRender();
    //void setUpdateFPS(int frame_per_second);
    //void repaint();
    //void setFrameBufWidth(int width);
    //void setFrameBufHeight(int height);
    //void setScreenWidth(int width);
    //void setScreenHeight(int height);
    //void setColorDepth(int color_depth);
    //void setColorFormat(int color_format);
    //void setFrameBufferPtr(unsigned char*buffer);
    //void sendKeyEvents(int key_down, int key_code, int key_extra = 0);
    //void sendPointerEvents(int buttons, int x, int y);
    void onHandleSignal( const std::string& msg );
    // void register_vnc_events(std::function<void(int)>);

private:

    //! callback variable for register_vnc_events to report vnc event ids
    // std::function<void(int)> mVNCEventCb;
    static MLVNC* mInstance;
};

} /* End of namespace MLLibrary */

#endif // MLLibrary_MLVNC_h
