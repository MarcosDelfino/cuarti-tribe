#ifndef _FREESTANDING_SYS_STAT_H
#define _FREESTANDING_SYS_STAT_H

#define S_IFCHR 0020000

struct stat {
    int st_mode;
};

#endif
