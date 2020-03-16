#ifndef ___FLAGBUILDER____
#define ___FLAGBUILDER____

#include <string>
#include <stdint.h>

class FlagBuilder
{
    public:
    static std::string getFlag();
    static int64_t getTime();
};

#endif