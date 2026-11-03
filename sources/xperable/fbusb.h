// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * fbusb.h - helper functions for fastboot usb protocol
 *
 * Copyright (C) 2025 j4nn at xdaforums
 */

#ifndef FBUSB_H
#define FBUSB_H

struct fbusb;

struct fbusb *fbusb_init(int vid, int pid, int iface, int ep_in, int ep_out);
void fbusb_exit(struct fbusb *dev);

void fbusb_set_maxsize(struct fbusb *dev, int maxsize);
void fbusb_set_timeout(struct fbusb *dev, int timeout_ms);
void fbusb_inc_verbosity(struct fbusb *dev);
void fbusb_dec_verbosity(struct fbusb *dev);

int fbusb_send(struct fbusb *dev, void *buff, int size);
int fbusb_recv(struct fbusb *dev, void *buff, int size);

enum {
    FASTBOOT_OKAY,
    FASTBOOT_FAIL,
    FASTBOOT_DATA,
    FASTBOOT_INFO,
    FASTBOOT_UNKNOWN
};

int fbusb_bufcmd(struct fbusb *dev, void *req, int reqsz, void *rsp, int *rspsz);
int fbusb_bufrcv(struct fbusb *dev, void *rcv, int rcvsz, void *rsp, int *rspsz);
int fbusb_bufcmd_resp(struct fbusb *dev, void *rsp, int *rspsz);
int fbusb_strcmd(struct fbusb *dev, const char *req, char *rsp, int rspmaxsize);
int fbusb_strcmd_resp(struct fbusb *dev, char *rsp, int rspmaxsize);

#endif
