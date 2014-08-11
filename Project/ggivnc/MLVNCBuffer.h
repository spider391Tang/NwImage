#ifndef MLVNCBUFFER_H
#define MLVNCBUFFER_H

extern "C" {
#include "ggi/ggi.h"
}
#include <boost/signals2/signal.hpp>
#include <boost/signals2/connection.hpp>
#include <boost/function.hpp>

typedef boost::signals2::signal <void()> BufferRenderedSignalType;

#endif // MLVNCBUFFER_H
