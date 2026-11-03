// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * fbusb.c - helper functions for fastboot usb protocol
 *
 * Copyright (C) 2025 j4nn at xdaforums
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <string.h>
#include <ctype.h>
#include <libusb.h>

#include "fbusb.h"
#include "logging.h"

static int verbosity = LOG_ERR;

static const char *fastboot_results[] = {
    [FASTBOOT_OKAY] = "OKAY",
    [FASTBOOT_FAIL] = "FAIL",
    [FASTBOOT_DATA] = "DATA",
    [FASTBOOT_INFO] = "INFO",
    [FASTBOOT_UNKNOWN] = "UNKN",
};


struct fbusb {
    libusb_device_handle *h;
    int iface;
    int epi;
    int epo;
    int maxsize;
    int timeout;
};


int fastboot_parse_result(const char *status)
{
    int i;
    for (i = 0; i < FASTBOOT_UNKNOWN; i++)
        if (strncmp(status, fastboot_results[i], 4) == 0)
            return i;
    return i;
}

struct fbusb *fbusb_init(int vid, int pid, int iface, int epi, int epo)
{
    int res;
    struct fbusb *dev;
    libusb_device_handle *h;

    res = libusb_init(NULL);
    if (res < 0) {
        PERR("libusb_init failed: %s\n", libusb_strerror(res));
        return NULL;
    }

    h = libusb_open_device_with_vid_pid(NULL, vid, pid);
    if (h == NULL) {
        PERR("libusb_open_device_with_vid_pid (%04x:%04x) failed\n",
               vid, pid);
        goto err_exit1;
    }

#if !defined(__APPLE__)
    if (libusb_kernel_driver_active(h, 0) == 1) {
        if (libusb_detach_kernel_driver(h, 0) != 0) {
            PERR("libusb_detach_kernel_driver failed\n");
            goto err_exit2;
        }
    }
#endif

    res = libusb_claim_interface(h, iface);
    if (res < 0) {
        PERR("libusb_claim_interface failed: %s\n", libusb_strerror(res));
        goto err_exit2;
    }

    dev = calloc(1, sizeof(struct fbusb));
    if (dev != NULL) {
        dev->h = h;
        dev->iface = iface;
        dev->epi = epi;
        dev->epo = epo;
        dev->maxsize = 16 * 1024 * 1024;
        dev->timeout = 5000;
        return dev;
    }

    libusb_release_interface(h, iface);
  err_exit2:
    libusb_close(h);
  err_exit1:
    libusb_exit(NULL);
    return NULL;
}

void fbusb_exit(struct fbusb *dev)
{
    libusb_release_interface(dev->h, dev->iface);
    libusb_close(dev->h);
    libusb_exit(NULL);
    free(dev);
}

void fbusb_set_maxsize(struct fbusb *dev, int maxsize)
{
    dev->maxsize = maxsize;
}

void fbusb_set_timeout(struct fbusb *dev, int timeout_ms)
{
    dev->timeout = timeout_ms;
}

void fbusb_inc_verbosity(struct fbusb *dev)
{
    if (verbosity < LOG_DBG)
        verbosity++;
}

void fbusb_dec_verbosity(struct fbusb *dev)
{
    if (verbosity > 0)
        verbosity--;
}

static void fbusb_log(struct fbusb *dev, int ep, void *buff, int len, int done,
                      int res)
{
    int i;
    const char *dir;
    const char *status;
    const unsigned char *ptr = buff;

    dir = (ep & 0x80) ? "<-" : "->";

    switch (res) {
    case LIBUSB_SUCCESS:                status = "OK"; break;
    case LIBUSB_ERROR_IO:               status = "IO"; break;
    case LIBUSB_ERROR_INVALID_PARAM:    status = "IP"; break;
    case LIBUSB_ERROR_ACCESS:           status = "AC"; break;
    case LIBUSB_ERROR_NO_DEVICE:        status = "ND"; break;
    case LIBUSB_ERROR_NOT_FOUND:        status = "NF"; break;
    case LIBUSB_ERROR_BUSY:             status = "BS"; break;
    case LIBUSB_ERROR_TIMEOUT:          status = "TO"; break;
    case LIBUSB_ERROR_OVERFLOW:         status = "OF"; break;
    case LIBUSB_ERROR_PIPE:             status = "PI"; break;
    case LIBUSB_ERROR_INTERRUPTED:      status = "IN"; break;
    case LIBUSB_ERROR_NO_MEM:           status = "NM"; break;
    case LIBUSB_ERROR_NOT_SUPPORTED:    status = "NS"; break;
    case LIBUSB_ERROR_OTHER:            status = "OT"; break;
    default:                            status = "??"; break;
    }

    PNFO_CONT("      {%08x%s%08x:%s}", len, dir, done, status);

    if (verbosity >= LOG_DBG) {
        for (i = 0; i < 16; i++)
            if (i < done)
                PDBG_CONT(" %02x", ptr[i]);
            else
                PDBG_CONT("   ");
    }

    PNFO_CONT(" \"");
    for (i = 0; i < 64 && i < done; i++)
        PNFO_CONT("%c", isprint(ptr[i]) ? ptr[i] : '.');
    PNFO_CONT("\"\n");
}

static int fbusb_transfer(struct fbusb *dev, void *buff, int size, int ep)
{
    int res, idx;
    int len, done;
    int transferred = 0;

    for (idx = 0; idx < size; idx += done) {
        len = size - idx;
        if (dev->maxsize > 0) {
            if (len > dev->maxsize)
                len = dev->maxsize;
        }

        done = 0;
        res = libusb_bulk_transfer(dev->h, ep, buff + idx, len, &done,
                                   dev->timeout);
        transferred += done;
        fbusb_log(dev, ep, buff + idx, len, done, res);

        if (res != 0 && transferred == 0) {
            PERR("libusb_bulk_transfer failed: %s ep=0x%02x "
                 "len=0x%04x size=0x%04x\n",
                 libusb_strerror(res), ep, len, size);
            return -1;
        }
        if (done < len)
            break;
    }

    return transferred;
}

int fbusb_send(struct fbusb *dev, void *buff, int size)
{
    return fbusb_transfer(dev, buff, size, dev->epo);
}

int fbusb_recv(struct fbusb *dev, void *buff, int size)
{
    return fbusb_transfer(dev, buff, size, dev->epi);
}

int fbusb_bufcmd_resp(struct fbusb *dev, void *rsp, int *rspsz)
{
    int res;
    int received;
    char *s = rsp;
    if (rspsz == NULL || *rspsz < 4)
        return -1;
    memset(rsp, 0, *rspsz);
    received = fbusb_recv(dev, rsp, *rspsz);
    if (received >= 4) {
        res = fastboot_parse_result(rsp);
        if (res < FASTBOOT_UNKNOWN) {
            received -= 4;
            memmove(rsp, rsp + 4, received);
            memset(s + received, 0, 4);
            *rspsz = received;
            return res;
        }
        PDBG("fbusb_bufcmd_resp recv unknown fastboot response: "
             "'%c%c%c%c' (rspsz=0x%04x received=0x%04x)\n",
             s[0], s[1], s[2], s[3], *rspsz, received);
        *rspsz = received;
        return FASTBOOT_UNKNOWN;
    }
    if (received < 0) {
        PERR("fbusb_bufcmd_resp recv failed (rspsz=0x%04x)\n", *rspsz);
        *rspsz = 0;
        return received;
    }
    PERR("fbusb_bufcmd_resp recv invalid fastboot response: "
         "received=0x%04x (rspsz=0x%04x)\n", received, *rspsz);
    *rspsz = received;
    return FASTBOOT_UNKNOWN;
}

int fbusb_bufcmd(struct fbusb *dev, void *req, int reqsz, void *rsp, int *rspsz)
{
    int res = fbusb_send(dev, req, reqsz);
    if (res != reqsz) {
        *rspsz = 0;
        if (res > 0) {
            PERR("fbusb_bufcmd send incomplete: "
                 "reqsz=0x%02x res=0x%04x\n", reqsz, res);
            return -1;
        }
        PERR("fbusb_bufcmd send failed: reqsz=0x%02x res=0x%04x\n", reqsz, res);
        return -1;
    }
    return fbusb_bufcmd_resp(dev, rsp, rspsz);
}

int fbusb_bufrcv(struct fbusb *dev, void *rcv, int rcvsz, void *rsp, int *rspsz)
{
    int res = fbusb_recv(dev, rcv, rcvsz);
    if (res != rcvsz) {
        *rspsz = 0;
        if (res > 0) {
            PDBG("fbusb_bufrcv recv incomplete: rcvsz=0x%02x res=0x%04x\n",
                 rcvsz, res);
            return -1;
        }
        PERR("fbusb_bufrcv recv failed: rcvsz=0x%02x res=0x%04x\n", rcvsz, res);
        return -1;
    }
    return fbusb_bufcmd_resp(dev, rsp, rspsz);
}

int fbusb_strcmd(struct fbusb *dev, const char *req, char *rsp, int rspmaxsize)
{
    int res;
    int rspsz = rspmaxsize - 1;
    if (rspsz < 4)
        return -1;
    res = fbusb_bufcmd(dev, (void *)req, strlen(req), rsp, &rspsz);
    if (res >= 0)
        if (rspsz >= 0 && rspsz < rspmaxsize)
            rsp[rspsz] = '\0';
    return res;
}

int fbusb_strcmd_resp(struct fbusb *dev, char *rsp, int rspmaxsize)
{
    int res;
    int rspsz = rspmaxsize - 1;
    if (rspsz < 4)
        return -1;
    res = fbusb_bufcmd_resp(dev, rsp, &rspsz);
    if (res >= 0)
        if (rspsz >= 0 && rspsz < rspmaxsize)
            rsp[rspsz] = '\0';
    return res;
}

