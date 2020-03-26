#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <getopt.h>             /* getopt_long() */

#include <fcntl.h>              /* low-level i/o */
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>

#include <linux/videodev2.h>

#define CLEAR(x) memset(&(x), 0, sizeof(x))
#define MMAP_BUF_CNT 3
#define SHOT_SAVE_DIR "/data/shot/"
#define CAMSTREAM_PIPE_FILE "/camStream_fifo"

enum io_method {
    IO_METHOD_READ,
    IO_METHOD_MMAP,
    IO_METHOD_USERPTR,
};

struct buffer {
    void   *start;
    size_t  length;
};

static enum io_method   io = IO_METHOD_MMAP;
static int              fd[2] = {-1, -1};
struct buffer          *buffers[2];
static unsigned int     n_buffers[2];
static int              stream_buf;
static int              force_format;
static int              list_frame_rate = 0;
static int cam_width = 0;
static int cam_height = 0;
static int mjpeg_frame = 0;
static int exit_flag = 0;

#define CAM_WIDTH 640
#define CAM_HEIGHT 480

static unsigned char camera_read_flag[2] = {0,0};
static unsigned char shot_flag[2] = {0,0};
static unsigned char shot_cnt[2] = {0, 0};


static unsigned char yuyvframe[CAM_HEIGHT*CAM_WIDTH*2*2];

unsigned char* compressYUV422toJPEG(unsigned char* src, int width, int height, int* olen);
int YUYVToBGR24_Native ( unsigned char* pYUV,unsigned char* pBGR24,int width,int height );
void bmp_write ( char *bmpFile, unsigned char *image_in, int width, int height );

static void errno_exit(const char *s)
{
    fprintf(stderr, "%s error %d, %s\\n", s, errno, strerror(errno));
    exit(EXIT_FAILURE);
}

static int xioctl(int fh, int request, void *arg)
{
    int r;

    do {
	    r = ioctl(fh, request, arg);
    } while (-1 == r && EINTR == errno);

    return r;
}

static void shot_store(const void *p , int size, char* file, int idx){
    char filename[40];
    if (file == NULL)
        sprintf(filename, "%s%d/%d.bmp", SHOT_SAVE_DIR, idx, shot_cnt[idx]++);
    else
        sprintf(filename, "%s", file);
    
    unsigned char *rgb24 = malloc(cam_width*cam_height*3);
    YUYVToBGR24_Native ( (unsigned char*)p, rgb24, cam_width, cam_height);
    bmp_write ( filename, rgb24, cam_width, cam_height );
    free(rgb24);
}

//process_image(数据指针，大小)
static void process_image(const void *p, int size, int idx)
{

//     unsigned char *tmp = (unsigned char*)p;
//     for (int i = 0; i < CAM_HEIGHT; i++){
//         for (int j = 0; j < CAM_WIDTH*2; j++) {
//             yuyvframe[i][j + idx * CAM_WIDTH] = *(tmp++);
//         }
//     }
    if (shot_flag[idx]) {
        shot_flag[idx] = 0;
        shot_store(p , size, NULL, idx);
    }
    memcpy(yuyvframe+(CAM_WIDTH*CAM_HEIGHT*2*idx), p, CAM_WIDTH*CAM_HEIGHT*2);
    camera_read_flag[idx] = 1;
}

static int read_frame(int fd, int idx)
{
    struct v4l2_buffer buf;
    unsigned int i;

    switch (io) {
    case IO_METHOD_READ:
	    if (-1 == read(fd, buffers[idx][0].start, buffers[idx][0].length)) {
		    switch (errno) {
		    case EAGAIN:
			    return 0;

		    case EIO:
			    /* Could ignore EIO, see spec. */

			    /* fall through */

		    default:
			    errno_exit("read");
		    }
	    }

	    process_image(buffers[idx][0].start, buffers[idx][0].length, idx);
	    break;

    case IO_METHOD_MMAP:
	    CLEAR(buf);

	    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	    buf.memory = V4L2_MEMORY_MMAP;

	    if (-1 == xioctl(fd, VIDIOC_DQBUF, &buf)) {
		    switch (errno) {
		    case EAGAIN:
			    return 0;

		    case EIO:
			    /* Could ignore EIO, see spec. */

			    /* fall through */

		    default:
			    errno_exit("VIDIOC_DQBUF");
		    }
	    }

	    assert(buf.index < n_buffers[idx]);

	    process_image(buffers[idx][buf.index].start, buf.bytesused, idx);

	    if (-1 == xioctl(fd, VIDIOC_QBUF, &buf))
		    errno_exit("VIDIOC_QBUF");
	    break;

    case IO_METHOD_USERPTR:
	    CLEAR(buf);

	    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	    buf.memory = V4L2_MEMORY_USERPTR;

	    if (-1 == xioctl(fd, VIDIOC_DQBUF, &buf)) {
		    switch (errno) {
		    case EAGAIN:
			    return 0;

		    case EIO:
			    /* Could ignore EIO, see spec. */

			    /* fall through */

		    default:
			    errno_exit("VIDIOC_DQBUF");
		    }
	    }

	    for (i = 0; i < n_buffers[idx]; ++i)
		    if (buf.m.userptr == (unsigned long)buffers[idx][i].start
			&& buf.length == buffers[idx][i].length)
			    break;

	    assert(i < n_buffers[idx]);

	    process_image((void *)buf.m.userptr, buf.bytesused, idx);

	    if (-1 == xioctl(fd, VIDIOC_QBUF, &buf))
		    errno_exit("VIDIOC_QBUF");
	    break;
    }

    return 1;
}

static void mainloop(void)
{
//     if (outfile){
//         if (mjpeg_frame){
//            shot_store(p , size, outfile, 1);
//         }else{
//             int olen = 0;
//             unsigned char *jpegBuf = compressYUV422toJPEG((unsigned char*)p, cam_width, cam_height, &olen);
//             if (jpegBuf) free(jpegBuf);
//             shot_store(p , size, outfile, 0);
//         }
//     }else if (stream_buf){
//         if (mjpeg_frame) {
//             fwrite(p, size, 1, stdout);
//             if (shot_flag)
//                 shot_store(p , size, NULL, 1);
//         } else {
//             int olen = 0;
//             unsigned char *jpegBuf = compressYUV422toJPEG((unsigned char*)p, cam_width, cam_height, &olen);
//             if (jpegBuf){
//                 fwrite(jpegBuf, olen, 1, stdout);
//                 free(jpegBuf);
//             }
//             if (shot_flag)
//             shot_store(p , size, NULL, 0);
//         }
//         shot_flag = 0;
//     }
    enum{
      SHORT_WAIT = 10*1000, //10ms
      LONG_WAIT = 30*1000, //30ms
    };
    
    int wait_flag = LONG_WAIT;
    while (1) {
        if (exit_flag) break;
        
        if (camera_read_flag[0] == 0 && camera_read_flag[1] == 0){
            wait_flag = LONG_WAIT;
            usleep(wait_flag);
        }else{
            if ((camera_read_flag[0] && camera_read_flag[1]) || wait_flag == SHORT_WAIT){
                camera_read_flag[0] = 0;
                camera_read_flag[1] = 0;
                int olen = 0;
                unsigned char *jpegBuf = compressYUV422toJPEG((unsigned char*)yuyvframe, CAM_WIDTH, CAM_HEIGHT*2, &olen);
                if (jpegBuf){
                    fwrite(jpegBuf, olen, 1, stdout);
                    free(jpegBuf);
                }
                fflush(stdout);
                wait_flag = LONG_WAIT;
                usleep(wait_flag);    
                
            }else {
                wait_flag = SHORT_WAIT;
                usleep(wait_flag);
            }
        }
            
    }
}


//VIDIOC_STREAMOFF
static void stop_capturing(int fd, int idx)
{
    enum v4l2_buf_type type;

    switch (io) {
    case IO_METHOD_READ:
	    /* Nothing to do. */
	    break;

    case IO_METHOD_MMAP:
    case IO_METHOD_USERPTR:
	    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	    if (-1 == xioctl(fd, VIDIOC_STREAMOFF, &type))
		    errno_exit("VIDIOC_STREAMOFF");
	    break;
    }
}


//VIDIOC_STREAMON
static void start_capturing(int fd, int idx)
{
    unsigned int i;
    enum v4l2_buf_type type;

    switch (io) {
    case IO_METHOD_READ:
	    /* Nothing to do. */
	    break;

    case IO_METHOD_MMAP:
	    for (i = 0; i < n_buffers[idx]; ++i) {
		    struct v4l2_buffer buf;

		    CLEAR(buf);
		    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		    buf.memory = V4L2_MEMORY_MMAP;
		    buf.index = i;

		    if (-1 == xioctl(fd, VIDIOC_QBUF, &buf))
			    errno_exit("VIDIOC_QBUF");
	    }
	    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	    if (-1 == xioctl(fd, VIDIOC_STREAMON, &type))
		    errno_exit("VIDIOC_STREAMON");
	    break;

    case IO_METHOD_USERPTR:
	    for (i = 0; i < n_buffers[idx]; ++i) {
		    struct v4l2_buffer buf;

		    CLEAR(buf);
		    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		    buf.memory = V4L2_MEMORY_USERPTR;
		    buf.index = i;
		    buf.m.userptr = (unsigned long)buffers[idx][i].start;
		    buf.length = buffers[idx][i].length;

		    if (-1 == xioctl(fd, VIDIOC_QBUF, &buf))
			    errno_exit("VIDIOC_QBUF");
	    }
	    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	    if (-1 == xioctl(fd, VIDIOC_STREAMON, &type))
		    errno_exit("VIDIOC_STREAMON");
	    break;
    }
}


//释放申请的内存
static void uninit_device(int fd, int idx)
{
    unsigned int i;

    switch (io) {
    case IO_METHOD_READ:
	    free(buffers[idx][0].start);
	    break;

    case IO_METHOD_MMAP:
	    for (i = 0; i < n_buffers[idx]; ++i)
		    if (-1 == munmap(buffers[idx][i].start, buffers[idx][i].length))
			    errno_exit("munmap");
	    break;

    case IO_METHOD_USERPTR:
	    for (i = 0; i < n_buffers[idx]; ++i)
		    free(buffers[idx][i].start);
	    break;
    }

    free(buffers[idx]);
}

static void init_read(int fd, int idx, unsigned int buffer_size)
{
    buffers[idx] = calloc(1, sizeof(*buffers[idx]));

    if (!buffers[idx]) {
	    fprintf(stderr, "Out of memory\\n");
	    exit(EXIT_FAILURE);
    }

    buffers[idx][0].length = buffer_size;
    buffers[idx][0].start = malloc(buffer_size);

    if (!buffers[idx][0].start) {
	    fprintf(stderr, "Out of memory\\n");
	    exit(EXIT_FAILURE);
    }
}

static void init_mmap(int fd, int idx)
{
    struct v4l2_requestbuffers req;

    CLEAR(req);

    req.count = MMAP_BUF_CNT;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;

    if (-1 == xioctl(fd, VIDIOC_REQBUFS, &req)) {
	    if (EINVAL == errno) {
		    fprintf(stderr, "video%d does not support "
			     "memory mappingn", idx);
		    exit(EXIT_FAILURE);
	    } else {
		    errno_exit("VIDIOC_REQBUFS");
	    }
    }
    
    if (req.count < 2) {
	    fprintf(stderr, "Insufficient buffer memory on video%d\\n",
		     idx);
	    exit(EXIT_FAILURE);
    }

    buffers[idx] = calloc(req.count, sizeof(*buffers[idx]));
    
    if (!buffers[idx]) {
	    fprintf(stderr, "Out of memory\\n");
	    exit(EXIT_FAILURE);
    }
    
    for (n_buffers[idx] = 0; n_buffers[idx] < req.count; ++n_buffers[idx]) {
	    struct v4l2_buffer buf;

	    CLEAR(buf);

	    buf.type        = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	    buf.memory      = V4L2_MEMORY_MMAP;
	    buf.index       = n_buffers[idx];
	    
        if (-1 == xioctl(fd, VIDIOC_QUERYBUF, &buf))
		    errno_exit("VIDIOC_QUERYBUF");

        buffers[idx][n_buffers[idx]].length = buf.length;
	    buffers[idx][n_buffers[idx]].start =
		    mmap(NULL /* start anywhere */,
			  buf.length,
			  PROT_READ | PROT_WRITE /* required */,
			  MAP_SHARED /* recommended */,
			  fd, buf.m.offset);
	    if (MAP_FAILED == buffers[idx][n_buffers[idx]].start)
		    errno_exit("mmap");
    }
}

static void init_userp(int fd, int idx, unsigned int buffer_size)
{
    struct v4l2_requestbuffers req;

    CLEAR(req);

    req.count  = 4;
    req.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_USERPTR;

    if (-1 == xioctl(fd, VIDIOC_REQBUFS, &req)) {
	    if (EINVAL == errno) {
		    fprintf(stderr, "video%d does not support "
			     "user pointer i/on", idx);
		    exit(EXIT_FAILURE);
	    } else {
		    errno_exit("VIDIOC_REQBUFS");
	    }
    }

    buffers[idx] = calloc(4, sizeof(*buffers[idx]));

    if (!buffers[idx]) {
	    fprintf(stderr, "Out of memory\\n");
	    exit(EXIT_FAILURE);
    }

    for (n_buffers[idx] = 0; n_buffers[idx] < 4; ++n_buffers[idx]) {
	    buffers[idx][n_buffers[idx]].length = buffer_size;
	    buffers[idx][n_buffers[idx]].start = malloc(buffer_size);

	    if (!buffers[idx][n_buffers[idx]].start) {
		    fprintf(stderr, "Out of memory\\n");
		    exit(EXIT_FAILURE);
	    }
    }
}
static void get_device_info(int fd, int idx){
    struct v4l2_capability cap;
    struct v4l2_fmtdesc fmt_desc;

    if (-1 == xioctl(fd, VIDIOC_QUERYCAP, &cap)) {
	    if (EINVAL == errno) {
		    fprintf(stderr, "video%d is no V4L2 device\\n",
			     idx);
		    exit(EXIT_FAILURE);
	    } else {
		    errno_exit("VIDIOC_QUERYCAP");
	    }
    }
    // Print capability infomations
    fprintf(stderr, "Capability Informations:\n");
    fprintf(stderr, " driver: %s\n", cap.driver);
    fprintf(stderr, " card: %s\n", cap.card);
    fprintf(stderr, " bus_info: %s\n", cap.bus_info);
    fprintf(stderr, " version: 0x%08X\n", cap.version);
    fprintf(stderr, " capabilities: 0x%08X\n", cap.capabilities);

    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
	    fprintf(stderr, "video%d is no video capture device\\n",
		     idx);
	    exit(EXIT_FAILURE);
    }
    
        
    switch (io) {
    case IO_METHOD_READ:
	    if (!(cap.capabilities & V4L2_CAP_READWRITE)) {
		    fprintf(stderr, "video%d does not support read i/o\\n",
			     idx);
		    exit(EXIT_FAILURE);
	    }
	    break;

    case IO_METHOD_MMAP:
    case IO_METHOD_USERPTR:
	    if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
		    fprintf(stderr, "video%d does not support streaming i/o\\n",
			     idx);
		    exit(EXIT_FAILURE);
	    }
	    break;
    }

    fprintf(stderr, "=======================\n");
    memset(&fmt_desc, 0, sizeof(struct v4l2_fmtdesc));
    fmt_desc.index=0;    
    fmt_desc.type=V4L2_BUF_TYPE_VIDEO_CAPTURE;
    while (-1 != xioctl(fd, VIDIOC_ENUM_FMT, &fmt_desc))
    {
    	fprintf(stderr, "fmt_desc.index=%d\n", fmt_desc.index);
    	fprintf(stderr, "fmt_desc.type=%d\n", fmt_desc.type);
    	fprintf(stderr,"fmt_desc.flags=%d\n", fmt_desc.flags);
    	fprintf(stderr,"fmt_desc.description:[%s]\n", fmt_desc.description);
    	fprintf(stderr,"fmt_desc.pixelformat=0x%x\n", fmt_desc.pixelformat);
        fprintf(stderr,"+++++++\n");
        struct v4l2_frmivalenum frmival;
        CLEAR(frmival);
        frmival.index = 0;
        frmival.pixel_format = fmt_desc.pixelformat;
        frmival.width = 640;
        frmival.height = 480;
        while (-1 != xioctl(fd, VIDIOC_ENUM_FRAMEINTERVALS, &frmival)){
            fprintf(stderr,"\tfrmival.index=%d\n", frmival.index);
            fprintf(stderr,"\tfrmival.pixel_format=%x\n", frmival.pixel_format);
            fprintf(stderr,"\tfrmival.width=%d\n", frmival.width);
            fprintf(stderr,"\tfrmival.height=%d\n", frmival.height);
            fprintf(stderr,"\tfrmival.type=%d\n", frmival.type);
            fprintf(stderr,"\tfrmival.discrete.numerator=%d\n", frmival.discrete.numerator);
            fprintf(stderr,"\tfrmival.discrete.denominator=%d\n", frmival.discrete.denominator);
            frmival.index++;
            fprintf(stderr,"+++++++\n");
        }
    	fprintf(stderr,"\n");
        fmt_desc.index++;
    }
	fprintf(stderr,"=======================\n");
    
}


static void init_device(int fd, int idx)
{
    struct v4l2_cropcap cropcap;
    struct v4l2_crop crop;
    struct v4l2_format fmt;
    unsigned int min;
    /* Select video input, video standard and tune here. */


    CLEAR(cropcap);
    
    struct v4l2_input inp;

    inp.index = 0;
    if (-1 == ioctl(fd, VIDIOC_S_INPUT, &inp))
    {
        fprintf(stderr, "VIDIOC_S_INPUT %d error!\n", 0);
        errno_exit("VIDIOC_S_INPUT");
    }

    /* set v4l2_captureparm.timeperframe */
    struct v4l2_streamparm stream_parm;
    stream_parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (-1 == xioctl(fd, VIDIOC_G_PARM, &stream_parm)){
        fprintf(stderr, "VIDIOC_G_PARM error\n");
    }else{
        stream_parm.parm.capture.capturemode = 0x0002;
        stream_parm.parm.capture.timeperframe.numerator = 1;
        stream_parm.parm.capture.timeperframe.denominator = 20;
        if (-1 == xioctl(fd, VIDIOC_S_PARM, &stream_parm)){
            fprintf(stderr, "VIDIOC_S_PARM error\n");
        }else{
            fprintf(stderr, "stream_parm.capture.timeperframe.denominator=%d\n", stream_parm.parm.capture.timeperframe.denominator);
            fprintf(stderr, "stream_parm.capture.timeperframe.numerator=%d\n", stream_parm.parm.capture.timeperframe.numerator);
        }
    }

    cropcap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (0 == xioctl(fd, VIDIOC_CROPCAP, &cropcap)) {
	    crop.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	    crop.c = cropcap.defrect; /* reset to default */

	    if (-1 == xioctl(fd, VIDIOC_S_CROP, &crop)) {
		    switch (errno) {
		    case EINVAL:
			    /* Cropping not supported. */
			    break;
		    default:
			    /* Errors ignored. */
			    break;
		    }
	    }
    } else {
	    /* Errors ignored. */
    }
    
    CLEAR(fmt);

    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (force_format) {
	    fmt.fmt.pix.width       = 640;
	    fmt.fmt.pix.height      = 480;
	    fmt.fmt.pix.pixelformat = mjpeg_frame?V4L2_PIX_FMT_MJPEG:V4L2_PIX_FMT_YUYV; //V4L2_PIX_FMT_MJPEG;
        fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        fmt.fmt.pix.field = V4L2_FIELD_NONE;           //V4L2_FIELD_INTERLACED;//V4L2_FIELD_NONE;
        fmt.fmt.pix.colorspace = V4L2_COLORSPACE_SRGB; //V4L2_COLORSPACE_SRGB;  //V4L2_COLORSPACE_JPEG;
//         fmt.fmt.pix.rot_angle = 0;
//         fmt.fmt.pix.subchannel = NULL;
        
	    if (-1 == xioctl(fd, VIDIOC_S_FMT, &fmt))
		    errno_exit("VIDIOC_S_FMT");

	    /* Note VIDIOC_S_FMT may change width and height. */
    }
    
    {
	    /* Preserve original settings as set by v4l2-ctl for example */
	    if (-1 == xioctl(fd, VIDIOC_G_FMT, &fmt))
		    errno_exit("VIDIOC_G_FMT");
    }
    cam_width = fmt.fmt.pix.width;
    cam_height = fmt.fmt.pix.height;
 	// Print Stream Format
    fprintf(stderr,"Stream Format Informations:\n");
    fprintf(stderr," type: %d\n", fmt.type);
    fprintf(stderr," width: %d\n", fmt.fmt.pix.width);
    fprintf(stderr," height: %d\n", fmt.fmt.pix.height);
    char fmtstr[8];
    memset(fmtstr, 0, 8);
    memcpy(fmtstr, &fmt.fmt.pix.pixelformat, 4);
    fprintf(stderr," pixelformat: %08x[%s]\n", fmt.fmt.pix.pixelformat,  fmtstr);
    fprintf(stderr," field: %d\n", fmt.fmt.pix.field);
    fprintf(stderr," bytesperline: %d\n", fmt.fmt.pix.bytesperline);
    fprintf(stderr," sizeimage: %d\n", fmt.fmt.pix.sizeimage);
    fprintf(stderr," colorspace: %d\n", fmt.fmt.pix.colorspace);
    fprintf(stderr," priv: %d\n", fmt.fmt.pix.priv);
    fprintf(stderr,"====================\n");
    if (list_frame_rate)
        get_device_info(fd, idx);
    
    /* Buggy driver paranoia. */
    min = fmt.fmt.pix.width * 2;
    if (fmt.fmt.pix.bytesperline < min)
	    fmt.fmt.pix.bytesperline = min;
    min = fmt.fmt.pix.bytesperline * fmt.fmt.pix.height;
    if (fmt.fmt.pix.sizeimage < min)
	    fmt.fmt.pix.sizeimage = min;
    
    switch (io) {
    case IO_METHOD_READ:
	    init_read(fd, idx, fmt.fmt.pix.sizeimage);
	    break;

    case IO_METHOD_MMAP:
	    init_mmap(fd, idx);
	    break;

    case IO_METHOD_USERPTR:
	    init_userp(fd, idx, fmt.fmt.pix.sizeimage);
	    break;
    }
    
}

static void close_device(int fd, int idx)
{
    if (-1 == close(fd))
	    errno_exit("close");

    fd = -1;
}

static void open_device(int *fd, int idx)
{
    struct stat st;
    char videoName[20] = {0};
    snprintf(videoName, sizeof(videoName), "/dev/video%d", idx);    
    
    if (-1 == stat(videoName, &st)) {
	    fprintf(stderr, "Cannot identify '%s': %d, %s\\n",
		     videoName, errno, strerror(errno));
	    exit(EXIT_FAILURE);
    }

    if (!S_ISCHR(st.st_mode)) {
	    fprintf(stderr, "%s is no devicen", videoName);
	    exit(EXIT_FAILURE);
    }

    *fd = open(videoName, O_RDWR /* required */ | O_NONBLOCK, 0);
    fprintf(stderr, "open %s fd=%d\n", videoName, *fd);
    
    if (-1 == *fd) {
	    fprintf(stderr, "Cannot open '%s': %d, %s\\n",
		     videoName, errno, strerror(errno));
	    exit(EXIT_FAILURE);
    }
}

static void *fifo_read_thread_func(void *arg){
    mkfifo(CAMSTREAM_PIPE_FILE, 777);
    mkdir(SHOT_SAVE_DIR, 777);
    FILE *fp = fopen(CAMSTREAM_PIPE_FILE, "r+");
    if (fp == NULL) {
        fprintf(stderr, "open %s failed", CAMSTREAM_PIPE_FILE);
	    exit(EXIT_FAILURE);
    }
    char tbuf[32];
    
    while(1){
        if (fgets(tbuf, sizeof(tbuf), fp) == NULL){
            fprintf(stderr, "fifo read error, exit");
            continue;
        }
        if (strncmp(tbuf, "shot ", strlen("shot ")) == 0) {
            fprintf(stderr, "get cmd:%s\n", tbuf);
            char *p = strchr(tbuf, ' ');
            shot_flag[0] = atoi(p+1) != 0;
            
            p = strchr(p+1, ' ');
            shot_flag[1] = atoi(p+1) != 0;
        }else if (strncmp(tbuf, "exit", strlen("exit")) == 0) {
            fprintf(stderr, "get cmd:%s\n", tbuf);
            exit_flag = 1;
        }else{
            fprintf(stderr, "get fifo :%s", tbuf);
        }
    }
    pthread_exit(NULL);
    return 0;
}

static void *camFrameRead_thread_func(void *arg){
    int idx = (int)arg;

    while (1) {
        if (exit_flag)  break;
	    for (;;) {
		    fd_set fds;
		    struct timeval tv;
		    int r;
            //gettimeofday(&tv, NULL);
            //fprintf(stderr, "%f\n", tv.tv_sec+tv.tv_usec/1000000.0);
		    FD_ZERO(&fds);
		    FD_SET(fd[idx], &fds);
		    /* Timeout. */
		    tv.tv_sec = 2;
		    tv.tv_usec = 0;

		    r = select(fd[idx] + 1, &fds, NULL, NULL, &tv);

		    if (-1 == r) {
			    if (EINTR == errno)
				    continue;
			    errno_exit("select");
		    }

		    if (0 == r) {
			    fprintf(stderr, "select timeout\\n");
			    exit(EXIT_FAILURE);
		    }

		    if (read_frame(fd[idx], idx)){
                break;
		    }
		    /* EAGAIN - continue select loop. */
	    }
    }
    return 0;
}

static void usage(FILE *fp, int argc, char **argv)
{
    fprintf(fp,
	     "Usage: %s [options]\n\n"
	     "Version 1.3\n"
	     "Options:\n"
	     "-h | --help          Print this message\n"
	     "-m | --mmap          Use memory mapped buffers [default]\n"
	     "-r | --read          Use read() calls\n"
	     "-u | --userp         Use application allocated buffers\n"
         "-l | --list          list camera infomations\n"
	     "",
	     argv[0]);
}

static const char short_options[] = "hmrul";

static const struct option
long_options[] = {
    { "help",   no_argument,       NULL, 'h' },
    { "mmap",   no_argument,       NULL, 'm' },
    { "read",   no_argument,       NULL, 'r' },
    { "userp",  no_argument,       NULL, 'u' },
    { "list",   no_argument, NULL, 'l' },
    { 0, 0, 0, 0 }
};

int main(int argc, char **argv)
{
    stream_buf++;
    force_format++;

    for (;;) {
	    int idx;
	    int c;

	    c = getopt_long(argc, argv,
			    short_options, long_options, &idx);

	    if (-1 == c)
		    break;

	    switch (c) {
	    case 0: /* getopt_long() flag */
		    break;
            
	    case 'h':
		    usage(stdout, argc, argv);
		    exit(EXIT_SUCCESS);

	    case 'm':
		    io = IO_METHOD_MMAP;
		    break;

	    case 'r':
		    io = IO_METHOD_READ;
		    break;

	    case 'u':
		    io = IO_METHOD_USERPTR;
		    break;
        case 'l':
            list_frame_rate++;;
            break;
	    default:
		    usage(stderr, argc, argv);
		    exit(EXIT_FAILURE);
	    }
    }

        
    open_device(&fd[1], 1);
    init_device(fd[1], 1);
    
    open_device(&fd[0], 0);
    init_device(fd[0], 0);
    
    start_capturing(fd[0], 0);
    start_capturing(fd[1], 1);
    pthread_t mPthread;
    pthread_create(&mPthread, NULL, fifo_read_thread_func, NULL);
    
    pthread_t cPthread[2];
    pthread_create(&cPthread[0], NULL, camFrameRead_thread_func, (void*)0);
    pthread_create(&cPthread[1], NULL, camFrameRead_thread_func, (void*)1);
    
    mkdir(SHOT_SAVE_DIR"0", 777);
    mkdir(SHOT_SAVE_DIR"1", 777);
    
    mainloop();
    sleep(1);
    stop_capturing(fd[0], 0);
    stop_capturing(fd[1], 1);
    uninit_device(fd[0], 0);
    uninit_device(fd[1], 1);
    close_device(fd[0], 0);
    close_device(fd[1], 1);
    fprintf(stderr, "\\n");
    return 0;
}

