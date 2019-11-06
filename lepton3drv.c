#include <stdint.h>
#include <poll.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>

#include <leptonSDKEmb32PUB/crc16.h>

#include "leptonSDKEmb32PUB/LEPTON_SDK.h"
#include "leptonSDKEmb32PUB/LEPTON_RAD.h"
#include "leptonSDKEmb32PUB/LEPTON_SYS.h"
#include "leptonSDKEmb32PUB/LEPTON_OEM.h"
#include "leptonSDKEmb32PUB/LEPTON_Types.h"

/* CONFIG: */
//#define RESYNC_TIMEOUT 185000
#define RESYNC_TIMEOUT 19890
#define SPI_SPEED 20000000
#define SPI_DEV "/dev/spidev0.0"
#define SEG_RING_SIZE 20
#define OUT_FD 1
/* END OF CONFIG */

#define PACKET_SIZE 164
#define PACKETS_PER_SEG 60

#define SEG_SIZE (PACKET_SIZE*PACKETS_PER_SEG)

static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t lck = PTHREAD_MUTEX_INITIALIZER;
static int spi_fd;
static int gpio_fd;
LEP_CAMERA_PORT_DESC_T lepton_i2c;

static void 
wait_vsync(struct pollfd *pfd)
{
  char c[2];

  lseek(pfd->fd, 0, SEEK_SET);
  poll(pfd, 1, -1);
  read(pfd->fd, c, 2);
}

#define PKT_OK        0
#define PKT_DUMMY  -1
#define PKT_BAD_CRC -2
#define PKT_BAD_READ -3

#define SEG_0 0
#define SEG_1 1
#define SEG_N 2
#define SEG_DUMMY_ERR  -1
#define SEG_CRC_ERR    -2
#define SEG_PKTN_ERR   -3
#define SEG_SEGN_ERR   -4
static int check_packet(uint8_t *p)
{
   uint8_t b, b2, b3;
   uint16_t crc, crc1;
   b = p[0];
   if((b & 0x0f) == 0x0f) return PKT_DUMMY;

   p[0] = b&0x0f;
   b2 = p[2]; b3 = p[3];
   crc = (b2<<8) | b3;
   p[2] = 0; p[3] = 0;
   crc1 = CalcCRC16Bytes(PACKET_SIZE, (char*)p);
   p[0] = b;
   p[2] = b2;
   p[3] = b3;
   if(crc1 != crc) return PKT_BAD_CRC;
   return PKT_OK;
}

static int 
read_seg(uint8_t *seg_buf)
{
  uint8_t *p = seg_buf;
  int e;
  int n = 0;

  do {
   int e;
   n++;
   if((e=read(spi_fd, p, PACKET_SIZE)) != PACKET_SIZE) {
    if(e < 0) fprintf(stderr, "read_seg: #%u read error %d\n", n, errno);
    else fprintf(stderr, "read_seg: #%u short read %d, but %d expected\n", n, e, PACKET_SIZE);
    return PKT_BAD_READ;

   }
  } while(p[1] != 0 || (p[0] & 0x0f) == 0x0f);

 if((e=read(spi_fd, p+PACKET_SIZE, PACKET_SIZE*(PACKETS_PER_SEG-1))) != PACKET_SIZE*(PACKETS_PER_SEG-1)) {
    if(e < 0) fprintf(stderr, "read_seg: #%u read error %d\n", n, errno);
    else fprintf(stderr, "read_seg: #%u short read %d, but %d expected\n", n, e, PACKET_SIZE*(PACKETS_PER_SEG-1));
    return PKT_BAD_READ;
 }


  return PKT_OK;

}

uint32_t seg_ring_w = 0;
uint32_t seg_ring_r = 0;
uint8_t seg_ring_buf[20][SEG_SIZE];
int resync = 0;

static void*
spi_thread(void *arg)
{
	char c[2];
    struct pollfd pfd;
    uint32_t x = 0;
    int r = 0;
    struct sched_param p = {};
   
    p.sched_priority = sched_get_priority_max(SCHED_FIFO);
    if(pthread_setschedparam(pthread_self(), SCHED_FIFO, &p) != 0) {
     fprintf(stderr, "spi_thread: can't set priority\n");
     exit(1);
    }

    pfd.fd = gpio_fd;
    read(pfd.fd, c, 2);
    pfd.events = POLLPRI;


	while(true) {

     if(r) {
      usleep(RESYNC_TIMEOUT);
      r = 0;
     }
     wait_vsync(&pfd);
     if(read_seg(seg_ring_buf[x]) != PKT_OK) exit(1);
     pthread_mutex_lock(&lck);
     if(resync) {
      resync = 0;
      r = 1;
      x = seg_ring_w = seg_ring_r;
      pthread_mutex_unlock(&lck); 
      continue;
     }
     if(++x == SEG_RING_SIZE) x = 0;
     if(x != seg_ring_r) seg_ring_w = x;
     else {
      fprintf(stderr, "spi_thread: seg ring overrun\n");
      x = seg_ring_w;
     }
     pthread_cond_signal(&cond);
     pthread_mutex_unlock(&lck); 
    }
}

static int
verify_seg(int expected_seg, uint8_t *p, int dump)
{
 int i;
 uint8_t *p1;
 int seg;

 for(i = 0; i < PACKETS_PER_SEG; i++) {
  int err = 0;
  const char *msg;

  p1 = p+PACKET_SIZE*i;
  switch(check_packet(p1)) {
          case PKT_BAD_CRC:
                  err = SEG_CRC_ERR;
                  msg = "bad crc";
                  break;
          case PKT_DUMMY:
                  err = SEG_DUMMY_ERR;
                  msg = "dummy packet";
                  break;
  }
  if(err) goto report_error;
  if(p1[1] != i) {
   err = SEG_PKTN_ERR;
   msg = "wrong pkt number";
  }
  if(i == 20) {
   seg = (p1[0]>>4) & 7;
   if(seg != expected_seg && expected_seg != 1 || seg > 4) {
    err = SEG_SEGN_ERR;
    msg = "wrong seg number";
   }
  }
report_error:
  if(err) {
   fprintf(stderr, "verify_seg(%d, %d): %s %02x%02x%02x%02x\n", expected_seg, i, msg, p1[0], p1[1], p1[2], p1[3]);
   if(dump & (1<<(-err-1))) {
    int x;
    for(x = 0; x < PACKET_SIZE*2 && i*PACKET_SIZE+x < SEG_SIZE; x++) fprintf(stderr, "%02x ", p1[x]);
    fprintf(stderr, "\n");
   }
   return err;
  }
 }
 if(seg == 0) return SEG_0;
 if(seg == 1) return SEG_1;
 return SEG_N;
}

static void*
output_thread(void *arg)
{
 uint32_t x = 0;
 uint32_t seg = 1;
 int prev_seg_ok = 1;
 int first_seg;

 while(1) {
  uint32_t pos = x;
  pthread_mutex_lock(&lck);
  while(resync || seg_ring_w == x) pthread_cond_wait(&cond, &lck);
  pthread_mutex_unlock(&lck);

  pos = x;
  if(++x == SEG_RING_SIZE) x = 0;

  switch(verify_seg(seg, seg_ring_buf[pos], 0)) {
          case SEG_0:
                  seg_ring_r = x;
                  prev_seg_ok = 1;
                  break;
          case SEG_1:
                  first_seg = pos;
                  prev_seg_ok = 1;
                  seg = 2;
//printf("seg1\n");
                  break;
          case SEG_CRC_ERR:
          case SEG_SEGN_ERR:
          case SEG_PKTN_ERR:
          case SEG_DUMMY_ERR:
                  seg = 1;
                  if(prev_seg_ok) {
                   prev_seg_ok = 0;
                   seg_ring_r = x;
                   break;
                  }
//printf("resync\n");
                  resync = 1;
                  break;
          case SEG_N:
//printf("seg%d\n", seg);
                  if(seg != 4) seg++;
                  else {
                   int i;
                   seg = 1;
                   for(i = 0; i < 4; i++) {
                    if(write(OUT_FD, seg_ring_buf[first_seg], SEG_SIZE) != SEG_SIZE) {
                     perror("write error");
                     exit(1);
                    }
                    if(++first_seg == SEG_RING_SIZE) first_seg = 0;
                   }
                   seg_ring_r = x;
                  }
  }
 }
}

static int get_int_arg(const char *p, int* arg)
{
 int i = 0;
 if(*p == 0) return 0;
 for(; *p != 0; p++) {
   if(*p < '0' || *p > '9') return 0;
   i = i*10 + (*p - '0');
 }
 *arg = i;
 return 1;
}

static void do_cmd(const char *cmd)
{
     
 LEP_RESULT r;
 int arg;
 if(!strcmp(cmd, "r")) {
    	r = LEP_RunOemReboot(&lepton_i2c);
        if(r != LEP_OK && r != LEP_ERROR_I2C_FAIL) {
         fprintf(stderr, "do_cmd: reset error %d\n", r);
        }
        sleep(3);
        r = LEP_SetOemGpioMode(&lepton_i2c, LEP_OEM_GPIO_MODE_VSYNC);
        if(r != LEP_OK) {
         fprintf(stderr, "do_cmd: enable vsync error: %d\n", r);
        } else {
         fprintf(stderr, "ok\n");
        }
 } else if(!strcmp(cmd, "l 0") || !strcmp(cmd, "l 1")) {
    LEP_RAD_ENABLE_E x;
    x = cmd[2] == '0' ? LEP_RAD_DISABLE : LEP_RAD_ENABLE;
	r = LEP_SetRadEnableState(&lepton_i2c, x);
    if(r != LEP_OK) {
         fprintf(stderr, "do_cmd: radiometry enable(%d) error: %d\n", cmd[2] == '1',  r);
    } else {
         fprintf(stderr, "ok\n");
    }
 } else if(!strcmp(cmd, "g 0") || !strcmp(cmd, "g 1") || !strcmp(cmd, "g 2")) {
    LEP_SYS_GAIN_MODE_E x;
    x = cmd[2] == '0' ? LEP_SYS_GAIN_MODE_HIGH 
                      : ( cmd[2] == '1' ? LEP_SYS_GAIN_MODE_LOW : LEP_SYS_GAIN_MODE_AUTO);
	r = LEP_SetSysGainMode(&lepton_i2c, x);
    if(r != LEP_OK) {
         fprintf(stderr, "do_cmd: sys gain (%d) error: %d\n", cmd[2] - '0',  r);
    } else {
         fprintf(stderr, "ok\n");
    }
 } else if(!strncmp(cmd, "s ", 2) && get_int_arg(cmd+2, &arg)) {
    /* LEP_OEM_VIDEO_OUTPUT_SOURCE_E x;
    x = cmd[2] == '0' ? LEP_VIDEO_OUTPUT_SOURCE_COOKED : LEP_VIDEO_OUTPUT_SOURCE_RAW; */
	r = LEP_SetOemVideoOutputSource(&lepton_i2c, arg);
    if(r != LEP_OK) {
         fprintf(stderr, "do_cmd: vo src (%d) error: %d\n", arg,  r);
    } else {
         fprintf(stderr, "ok\n");
    }
 } else if(!strcmp(cmd, "f 1")) {
    r = LEP_RunRadFFC(&lepton_i2c);
    if(r != LEP_OK) {
         fprintf(stderr, "do_cmd: run radiometry FFC error: %d\n", r);
    } else {
         fprintf(stderr, "ok\n");
    }
 } else if(!strcmp(cmd, "f") || !strcmp(cmd, "f 0")) {
	r = LEP_RunSysFFCNormalization(&lepton_i2c);
    if(r != LEP_OK) {
         fprintf(stderr, "do_cmd: run FFC error: %d\n", r);
    } else {
         fprintf(stderr, "ok\n");
    }

 } else {
         fprintf(stderr, "do_cmd: unknown command \"%s\"\n", cmd);
 }
}

int
main()
{
    int s;
    unsigned char spi_mode = SPI_MODE_3;
    unsigned char spi_bits = 8;
    unsigned spi_speed = SPI_SPEED;
    pthread_t thr1, thr2;
#define CMD_SIZE 127
    char cmdbuf[CMD_SIZE + 1];
    unsigned pos = 0;
    
	spi_fd = open(SPI_DEV, O_RDWR);

	if (spi_fd < 0) {
		perror("could not open SPI device");
		exit(1);
	}

	s = ioctl(spi_fd, SPI_IOC_WR_MODE, &spi_mode);
	if(s < 0) {
		perror("could not set spimode (wr)");
		exit(1);
	}

	s = ioctl(spi_fd, SPI_IOC_RD_MODE, &spi_mode);
	if(s < 0) {
		perror("could not set spimode (rd)");
		exit(1);
	}

	s = ioctl(spi_fd, SPI_IOC_WR_BITS_PER_WORD, &spi_bits);
	if(s < 0) {
		perror("could not set spi bitsperword (wr)");
		exit(1);
	}

	s = ioctl(spi_fd, SPI_IOC_RD_BITS_PER_WORD, &spi_bits);
	if(s < 0)
	{
		perror("could not set spi bitsperword (rd)");
		exit(1);
	}

	s = ioctl(spi_fd, SPI_IOC_WR_MAX_SPEED_HZ, &spi_speed);
	if(s < 0) {
		perror("could not set spi speed (wr)");
		exit(1);
	}

	s = ioctl(spi_fd, SPI_IOC_RD_MAX_SPEED_HZ, &spi_speed);
	if(s < 0) {
		perror("could not set spi speed (rd)");
		exit(1);
	}

    gpio_fd = open("/sys/class/gpio/gpio25/value", O_RDONLY);
    if(gpio_fd < 0) {
     perror("can't open gpio file");
     exit(1);
    }

    pthread_create(&thr1, 0, spi_thread, 0);
    pthread_create(&thr2, 0, output_thread, 0);

    {
     LEP_RESULT r;
	 r = LEP_OpenPort(1, LEP_CCI_TWI, 400, &lepton_i2c);
     if(r != LEP_OK) {
      fprintf(stderr, "i2c open error: %d\n", r);
      exit(1);
     }
     r = LEP_SetOemGpioMode(&lepton_i2c, LEP_OEM_GPIO_MODE_VSYNC);
     if(r != LEP_OK) {
      fprintf(stderr, "enable vsync error: %d\n", r);
      exit(1);
     }
    }

    while(1) {
     char c;

     ssize_t n;
     n = read(0, &c, 1);
     if(n != 1) exit(0);
     switch(c) {
             case 'a'... 'z':
             case 'A'... 'Z':
             case '0'... '9':
             case ' ':
                     if(pos < CMD_SIZE) 
                      cmdbuf[pos++] = c;
                     else {
                      pos = 0;
                      fprintf(stderr, "cmd: the command is too long\n");
                     }
                     break;
             case '\r':
             case '\n':
                     if(pos != 0) {
                      cmdbuf[pos] = 0;
                      do_cmd(cmdbuf);
                      pos = 0;
                     }
                     break;

     }

    }

}
