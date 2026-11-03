// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * logging.h - macros for debug and errors logging
 *
 * Copyright (C) 2025 j4nn at xdaforums
 */

#ifndef _LOGGING_H_
#define _LOGGING_H_

#include <errno.h>
#include <stdio.h>
#include <string.h>

enum {
    LOG_ERR = 1,
    LOG_NFO,
    LOG_DBG,
};

#define eprintf(lev, fmt, args...) \
    do { \
        if (verbosity >= lev) \
            fprintf(stderr, fmt, ##args); \
    } while(0)

#define oprintf(lev, fmt, args...) \
    do { \
        if (verbosity >= lev) \
            fprintf(stdout, fmt, ##args); \
    } while(0)

#define PERR(fmt, args...) eprintf(LOG_ERR, "[!] " fmt, ##args)
#define PNFO(fmt, args...) eprintf(LOG_NFO, "[+] " fmt, ##args)
#define PDBG(fmt, args...) eprintf(LOG_DBG, "[.] " fmt, ##args)

#define PERR_CONT(fmt, args...) eprintf(LOG_ERR, fmt, ##args)
#define PNFO_CONT(fmt, args...) eprintf(LOG_NFO, fmt, ##args)
#define PDBG_CONT(fmt, args...) eprintf(LOG_DBG, fmt, ##args)

#define PRNO(fmt, args...) \
    do { \
        PERR(fmt ": %s\n", ##args, strerror(errno)); \
    } while(0)

#define POUT(fmt, args...) oprintf(LOG_NFO, fmt, ##args)

#endif
