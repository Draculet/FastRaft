#include "FlagBuilder.h"
#include <sys/time.h>
using namespace std;

string FlagBuilder::getFlag()
{
    struct timeval tval;
    gettimeofday(&tval, nullptr);
    const int million = 1000000;
    int64_t n = tval.tv_sec * million + tval.tv_usec;
    char buf[20] = {0};
    snprintf(buf, sizeof(buf), "%ld", n);
    return string(buf + 12, 4);
}

int64_t FlagBuilder::getTime()
{
    struct timeval tval;
    gettimeofday(&tval, nullptr);
    const int million = 1000000;
    int64_t n = tval.tv_sec * million + tval.tv_usec;
    return n;
}