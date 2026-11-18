#pragma once
#include "receivebuffer.h"
#include <QString>

// Presentation of retained bytes; never mutates the raw history.
class ByteFormatter {
public:
    struct Options {
        bool hex = false;
        bool timestamps = true;
        bool showRx = true;
        bool showTx = true;
    };
    struct Display {
        QString text;
        bool limited = false;
    };
    static Display format(const ReceiveBuffer &buffer, const Options &options);
};
