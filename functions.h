#ifndef FUNCTIONS_H
#define FUNCTIONS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <getopt.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <malloc.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <asm/types.h>
#include <linux/videodev2.h>
#include <linux/fb.h>
#include "logo.h"

#define CLEAR(x) memset (&(x), 0, sizeof (x))
#define xR 3
#define yR 8
#define _ARM_ 1// «∑Ò «ARM

struct buffer {
    void * start;
    size_t length;
};

void errno_exit (const char * s);
int xioctl (int fd,int request,void * arg);
inline int clip(int value, int min, int max);
inline int convert_yuv_to_rgb_pixel(int y, int u, int v);
inline unsigned short RGB888toRGB565(unsigned short red, unsigned short green, unsigned short blue);
int convert_yuv_to_rgb_buffer(unsigned char *yuv, unsigned char *rgb, unsigned int width, unsigned int height);
int read_frame (unsigned char *rgbbuf);
void delay();
int zoom(unsigned short * ptOriginPic,unsigned short * ptZoomPic);
int rgb_convert(unsigned short *vout,unsigned char  *rgbbuf);
int normal_convert(unsigned short *vout,unsigned char  *rgbbuf);
void run (void);
void stop_capturing (void);
int start_capturing (void);
void uninit_device (void);
void init_mmap (void);
void init_device (void);
void close_device (void);
void open_device (void);
void usage (FILE * fp,int argc,char ** argv);
void screenClean();
void insert_logo(unsigned short vout[]);

#endif // FUNCTIONS_H
