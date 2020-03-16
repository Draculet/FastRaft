#ifndef ___FLAGBUILDER_H___
#define ___FLAGBUILDER_H___

#include <string>
#include <stdint.h>

class FlagBuilder
{
    public:
    static std::string getFlag();
    static int64_t getTime();
};

#endif