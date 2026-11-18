#pragma once
#include "protocolparser.h"

// Incremental named numeric text, independent of serial/GUI transports.
inline constexpr qsizetype MaxDictionaryFields = 128;
std::unique_ptr<ProtocolParser> makeKeyValueParser();
