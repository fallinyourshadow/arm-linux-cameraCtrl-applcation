#include "functions.h"
#define uchar unsigned char
#define uint unsigned int
int video_width = 160;
int video_height = 120;
int screen_width  = 0;
int screen_height = 0;
int logo_width  = 100;
int logo_height = 84;

struct buffer * buffers = NULL;//用于映射缓存队列
static struct fb_var_screeninfo screen_info;//用于保存lcd屏幕信息
static struct fb_fix_screeninfo finfo;//用于保存屏幕安装信息
static char * dev_name = "/dev/video0";
static unsigned int n_buffers = 0;
static int time_in_sec_capture=5;

int fd = -1;
int fbfd = -1;
char *fbp = NULL;
volatile int isStop = 0;

static long screensize = 0;
unsigned long *pdwSrcYTable = NULL;
unsigned long *pdwSrcXTable = NULL; //[480];


void errno_exit (const char * s)
{
	fprintf(stderr, "%s error %d, %s\n", s, errno, strerror(errno));
	exit(EXIT_FAILURE);
}

int xioctl (int fd,int request,void * arg)
{
    int r;
    do r = ioctl (fd, request, arg);
	while (-1 == r && EINTR == errno);
    return r;
}
/*取0~255*/
inline int clip(int value, int min, int max) {
	return (value > max ? max : value < min ? min : value);
}
/*yuv4:2:2格式转换为rgb24格式*/
inline int convert_yuv_to_rgb_pixel(int y, int u, int v)
{
    uint pixel32 = 0;
    uchar *pixel = (uchar *)&pixel32;
    int r, g, b;
    r = y +1.3707 * v-175.4502;
    g = y - 0.6980 * v  - 0.3376 * u + 132.5611;
    b = y + 1.7324 * u-221.7530;

    r = clip(r,0,255);
    g = clip(g,0,255);
    b = clip(b,0,255);

    pixel[0] = r * 0.859375;
    pixel[1] = g * 0.859375;
    pixel[2] = b * 0.859375;

    return pixel32;
}
inline unsigned short RGB888toRGB565(unsigned short red, unsigned short green, unsigned short blue)
{
    unsigned short B = (blue >> 3) & 0x001F;//屏蔽后11位
    unsigned short G = (green << 3) & 0x07E0;//屏蔽前后5位
    unsigned short R = (red << 8) & 0xF800;//屏蔽前11位
    return (unsigned short) (R | G | B);
}

int convert_yuv_to_rgb_buffer(uchar *yuv, uchar *rgb, uint width,uint height)
{
    uint in, out = 0;
    uint pixel_16;
    uchar pixel_24[3];
    uint pixel32;
    int y0, u, y1, v;

    for(in = 0; in < width * height * 2; in += 4) {
        pixel_16 =
        yuv[in + 3] << 24 |
        yuv[in + 2] << 16 |
        yuv[in + 1] <<  8 |
        yuv[in + 0];
	//YUV422每个像素2字节，每两个像素共用一个Cr,Cb值，即u和v，RGB24每个像素3个字节
        y0 = (pixel_16 & 0x000000ff);
        u  = (pixel_16 & 0x0000ff00) >>  8;
        y1 = (pixel_16 & 0x00ff0000) >> 16;
        v  = (pixel_16 & 0xff000000) >> 24;

        pixel32 = convert_yuv_to_rgb_pixel(y0, u, v);
        pixel_24[0] = (pixel32 & 0x000000ff);
        pixel_24[1] = (pixel32 & 0x0000ff00) >> 8;
        pixel_24[2] = (pixel32 & 0x00ff0000) >> 16;
        rgb[out++] = pixel_24[0];
        rgb[out++] = pixel_24[1];
        rgb[out++] = pixel_24[2];

        pixel32 = convert_yuv_to_rgb_pixel(y1, u, v);
        pixel_24[0] = (pixel32 & 0x000000ff);
        pixel_24[1] = (pixel32 & 0x0000ff00) >> 8;
        pixel_24[2] = (pixel32 & 0x00ff0000) >> 16;
        rgb[out++] = pixel_24[0];
        rgb[out++] = pixel_24[1];
        rgb[out++] = pixel_24[2];
    }
    return out;
}

int read_frame (unsigned char *rgbbuf)
{
    struct v4l2_buffer buf;
	int rgb_len =0;
    CLEAR (buf);
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;

	ioctl(fd, VIDIOC_DQBUF, &buf); //取出采集的帧缓冲
	rgb_len = convert_yuv_to_rgb_buffer(buffers[buf.index].start, rgbbuf, video_width, video_height);//初步处理写入rgbbuf临时存储
	ioctl(fd, VIDIOC_QBUF, &buf);//再将其入列

    return rgb_len;
}

//放大图像数据以适应屏幕
int zoom(unsigned short * ptOriginPic,unsigned short * ptZoomPic)
{

	unsigned long x=0;
	unsigned long y=0;

    for (y = 0; y < screen_height; ++y)
    {
        for (x = 0; x <screen_width; ++x)
        {
			ptZoomPic[y * screen_width + x] = ptOriginPic[pdwSrcYTable[y] + pdwSrcXTable[x]];
        }
    }
	return 0;
}

int rgb_convert(unsigned short *vout,unsigned char  *rgbbuf)
{
   int i =0, j =0;
   int len = 0;
   for(i=0; i<video_height; i++)//处理行
    	{
	    for(j=0; j< video_width; j++)//处理列
	    {
		//RGB888转RGB565，存到vout中
			vout[i*video_width + j] = RGB888toRGB565(rgbbuf[len + 0], rgbbuf[len + 1], rgbbuf[len + 2]);
			len = len + 3;
	    }
	}

	return len;
}

void screenClean()
{
	unsigned short *v_buf;
	int i;
	v_buf = (unsigned short*)fbp;//引用fbp
	for(i=0;i<screen_width*screen_height;i++)
	{
		v_buf[i]=0x0000;//拷贝到fbp
	}
}

void run (void)
{
	int i = 0;
	int ret = 0;
	int frames = 0;
	fd_set fds;
	struct timeval tv;
	int signal = 0;
    	unsigned char *rgbbuf = NULL;
	unsigned short *convert_rgbbuf = NULL;
	unsigned short *v_buf= (unsigned short*)fbp;//引用fbp
	unsigned short vout[screensize];
        void * shmptr;
	int shmid;
	key_t key = ftok("signal",1);
	shmid = shmget(key,4,SHM_R |SHM_W);
	shmptr = shmat(shmid,0,0);
	rgbbuf = malloc(sizeof(unsigned char)*(video_width*video_height*3));
    if( rgbbuf == NULL )
    {
        printf("malloc faile!\n");
        exit(1);
    }

	convert_rgbbuf = malloc(sizeof(unsigned short)*(video_width*video_height*2));
    if( convert_rgbbuf == NULL )
    {
        printf("malloc faile!\n");
        exit(1);
    }

    frames = 30 * time_in_sec_capture;

    while (frames-- > 0)
	{
        for (;;) {
	memcpy(&signal,shmptr,4);//检测信号丢失
	if(signal == 1)
	{
		screenClean();
		signal = 0;
		memcpy(shmptr,&signal,4);

		while(1)
		{
			memcpy(shmptr,&signal,4);//持续发送信号
		}
	}
#if 1
            ret = 0;
            FD_ZERO (&fds);
            FD_SET (fd, &fds);

            tv.tv_sec = 2;
            tv.tv_usec = 0;

            ret = select (fd + 1, &fds, NULL, NULL, &tv);
            if (-1 == ret) {
                if (EINTR == errno)
                    continue;
                errno_exit ("select");
            }

            if (0 == ret) {
                fprintf (stderr, "select timeout/n");
                exit (EXIT_FAILURE);
            }
#endif
            else if (read_frame (rgbbuf)&&signal == 0)
            {

				rgb_convert(convert_rgbbuf,rgbbuf);//转码

				zoom(convert_rgbbuf,vout);

				insert_logo(vout);

				for(i=0;i<screen_width*screen_height;i++)
				{
					v_buf[i]=vout[i];//拷贝到fbp
				}
            }
            else if(signal == 2)
            {
                while(signal == 2)
                {
                    memcpy(&signal,shmptr,4);//等待信号发生变化
                }
            }
        }
    }
}

void stop_capturing (void)
{
    enum v4l2_buf_type type;

    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (-1 == xioctl (fd, VIDIOC_STREAMOFF, &type))
        errno_exit ("VIDIOC_STREAMOFF");
}

int start_capturing (void)
{
    unsigned int i,x,y;
    enum v4l2_buf_type type;
    //索引表
    pdwSrcXTable = malloc(sizeof(unsigned long) * (screen_width));
    pdwSrcYTable = malloc(sizeof(unsigned long) * (screen_height));

    struct v4l2_buffer buf;

    if (NULL == pdwSrcXTable)
    {
        printf("malloc error!\n");
        return -1;
    }
    if (NULL == pdwSrcYTable)
    {
        printf("malloc error!\n");
        return -1;
    }
    for (x = 0; x < screen_width; ++x)
    {
        pdwSrcXTable[x] = x * video_width / screen_width;
    }
    for (y = 0; y < screen_height; ++y)
    {
        pdwSrcYTable[y] = y * video_height / screen_height * video_width;
    }

    for (i = 0; i < n_buffers; ++i)
    {
        CLEAR (buf);

        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

        if (-1 == xioctl (fd, VIDIOC_QBUF, &buf))
            errno_exit ("VIDIOC_QBUF");
    }

    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (-1 == xioctl (fd, VIDIOC_STREAMON, &type))
        errno_exit ("VIDIOC_STREAMON");
    printf("start_capturing!\n");
}

void uninit_device (void)
{
    unsigned int i;

    for (i = 0; i < n_buffers; ++i)
    {
    	if (-1 == munmap (buffers[i].start, buffers[i].length))
        	errno_exit ("munmap");
    	else
		printf("unmap sucessful buffer%d \n",i);}
    	if (-1 == munmap(fbp, screensize)) {
        	printf(" Error: framebuffer device munmap() failed.\n");
    	exit (EXIT_FAILURE);
    }
    free (buffers);
    free (pdwSrcXTable);
    free (pdwSrcYTable);
}

void init_mmap (void)
{
    struct v4l2_requestbuffers req;
#ifdef _ARM_
    //mmap framebuffer
    fbp = (char *)mmap(NULL,screensize,PROT_READ | PROT_WRITE,MAP_SHARED ,fbfd, 0);
    if ((int)fbp == -1)
    {
        printf("Error: failed to map framebuffer device to memory.\n");
        exit (EXIT_FAILURE) ;
    }
    memset(fbp, 0, screensize);
    CLEAR (req);
#endif
    req.count = 4;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;

    if (-1 == xioctl (fd, VIDIOC_REQBUFS, &req))
    {
        if (EINVAL == errno)
	{
            fprintf (stderr, "%s does not support memory mapping\n", dev_name);
            exit (EXIT_FAILURE);
        }
	else
	{
            errno_exit ("VIDIOC_REQBUFS");
        }
    }

    if (req.count < 4)
    {
        fprintf (stderr, "Insufficient buffer memory on %s\n",dev_name);
        exit (EXIT_FAILURE);
    }

    buffers = calloc (req.count, sizeof (*buffers));

    if (!buffers)
    {
        fprintf (stderr, "Out of memory\n");
        exit (EXIT_FAILURE);
    }

    for (n_buffers = 0; n_buffers < req.count; ++n_buffers)
    {
        struct v4l2_buffer buf;

        CLEAR (buf);

        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = n_buffers;

        if (-1 == xioctl (fd, VIDIOC_QUERYBUF, &buf))
            errno_exit ("V IDIOC_QUERYBUF");

        buffers[n_buffers].length = buf.length;
        buffers[n_buffers].start =mmap (NULL,buf.length,PROT_READ | PROT_WRITE ,MAP_SHARED,fd, buf.m.offset);

        if (MAP_FAILED == buffers[n_buffers].start)
            errno_exit ("mmap");
    }
}



void init_device (void)
{
    struct v4l2_capability cap;//设备能力
    struct v4l2_cropcap cropcap;//与驱动修剪的能力相关结构体
    struct v4l2_crop crop;//用于设置视频的采集窗口参数
    struct v4l2_format fmt;
    struct v4l2_fmtdesc fmtdesc;
    unsigned int min;
#ifdef _ARM_
    // Get fixed screen information
    if (-1==xioctl(fbfd, FBIOGET_FSCREENINFO, &finfo)) {
        printf("Error reading fixed information.\n");
        exit (EXIT_FAILURE);
    }
    // Get variable screen information
    if (-1==xioctl(fbfd, FBIOGET_VSCREENINFO, &screen_info)) {
        printf("Error reading variable information.\n");
        exit (EXIT_FAILURE);
    }
	screen_width  = screen_info.xres;
	screen_height = screen_info.yres;
        screensize = screen_info.xres * screen_info.yres * screen_info.bits_per_pixel / 8;

    //printf("screen_width=%d\n", screen_width);
    //printf("screen_height=%d\n",screen_height);
    //printf("screen_info.bits_per_pixel=%d\n",screen_info.bits_per_pixel);
    //printf("screensize=%ld\n",screensize);
#endif
    if (-1 == xioctl (fd, VIDIOC_QUERYCAP, &cap)) {
        if (EINVAL == errno) {
            fprintf (stderr, "%s is no V4L2 device/n",dev_name);
            exit (EXIT_FAILURE);
        } else {
            errno_exit ("VIDIOC_QUERYCAP");
        }
    }

    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
        fprintf (stderr, "%s is no video capture device\n",dev_name);
        exit (EXIT_FAILURE);
    }

    if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
        fprintf (stderr, "%s does not support streaming i/o\n",dev_name);
        exit (EXIT_FAILURE);
    }

    fmt.type=V4L2_BUF_TYPE_VIDEO_CAPTURE;
    //获取当前摄像头的宽高
    ioctl(fd, VIDIOC_G_FMT, &fmt);
    //printf("Current data format information:\n\twidth:%d\n\theight:%d\n",fmt.fmt.pix.width,fmt.fmt.pix.height);

    fmtdesc.index=0;
    fmtdesc.type=V4L2_BUF_TYPE_VIDEO_CAPTURE;
    //获取当前摄像头支持的格式
    /*while(ioctl(fd,VIDIOC_ENUM_FMT,&fmtdesc)!=-1)
    {
        if(fmtdesc.pixelformat & fmt.fmt.pix.pixelformat)
        {
            printf("\tformat:%s\n",fmtdesc.description);
            break;
        }
        fmtdesc.index++;
    }*/

    CLEAR (cropcap);

    cropcap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (0 == xioctl (fd, VIDIOC_CROPCAP, &cropcap)) {
        crop.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        crop.c = cropcap.defrect;

        if (-1 == xioctl (fd, VIDIOC_S_CROP, &crop)) {
            switch (errno) {
            case EINVAL:
            break;
            default:
            break;
            }
        }
    }else {    }

    CLEAR (fmt);

    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width  = video_width;
    fmt.fmt.pix.height = video_height;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;//V4L2_PIX_FMT_MJPEG;
    fmt.fmt.pix.field = V4L2_FIELD_INTERLACED;

    if (-1 == xioctl (fd, VIDIOC_S_FMT, &fmt))
        errno_exit ("VIDIOC_S_FMT");

    init_mmap ();

}

void close_device (void)
{

    if (-1 == close(fd))
		errno_exit("close");
    fd = -1;
//#ifdef _ARM_
    close(fbfd);
//#endif
}

void open_device (void)
{
    struct stat st;

    if (-1 == stat (dev_name, &st)) {
    fprintf (stderr, "Cannot identify '%s': %d, %s\n",dev_name, errno, strerror (errno));
    exit (EXIT_FAILURE);
    }

//检查是否是字符设备文件
    if (!S_ISCHR (st.st_mode)) {
        fprintf (stderr, "%s is no device\n", dev_name);
        exit (EXIT_FAILURE);
    }

#ifdef _ARM_
    //open framebuffer
    fbfd = open("/dev/fb0", O_RDWR);
    if (-1== fbfd) {
        printf("Error: cannot open framebuffer device.\n");
        exit (EXIT_FAILURE);
    }
#endif

    //open camera
    fd = open (dev_name, O_RDWR| O_NONBLOCK, 0);

    if (-1 == fd) {//打开相机失败
        fprintf (stderr, "Cannot open '%s': %d, %s\n",dev_name, errno, strerror (errno));
        exit (EXIT_FAILURE);
    }
}

void insert_logo(unsigned short vout[])
{
	int x=logo_width*logo_height;
	int filter_value = 200;
	int a=0, h=0, j=0, y=0;
	while(x>0)
	{
		a=0;
		for(h=screen_width*y;h<=screen_width*y+logo_width;h++)
		{
			j=((x-logo_width+1+a))*3;
			if((logo_bmp[j+0]>filter_value)&&
			   (logo_bmp[j+1]>filter_value)&&
			   (logo_bmp[j+2]>filter_value))
			{}
			else if((logo_bmp[j+0]+logo_bmp[j+1]+logo_bmp[j+2])==510)
			{}
			else
			{
					if(vout[h]<RGB888toRGB565(50,50,50))
						 vout[h]=RGB888toRGB565(255,255,255);
					else
						 vout[h]=RGB888toRGB565(0,0,0);
			}
			a++;
		}
		x=x-logo_width;
		y++;
	}
}

