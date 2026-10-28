#include <fcntl.h>

class util
{
public:
    static void setNonBlock(int fd)
    {
        int fl = fcntl(fd,F_GETFL);
        if (fl < 0) return;
        fcntl(fd,F_SETFL,fl | O_NONBLOCK);
    }
};