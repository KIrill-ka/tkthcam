#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <pthread.h>
//#include <sigaction.h>
#include <i3system_TE.h>

static pthread_mutex_t teb_lock = PTHREAD_MUTEX_INITIALIZER;
static volatile int img_type = 1;
static volatile int want_exit = 0;
static void*
output_thread(void *arg)
{
 i3::TE_B *teb = (i3::TE_B*) arg;
 uint16_t img[384*288];
 float imgf[384*288];
 int r;

 while(!want_exit) {
  
  switch(img_type) {
          case 3:
                  pthread_mutex_lock(&teb_lock);
                  teb->RecvImage(imgf);
                  pthread_mutex_unlock(&teb_lock);
                  { //float mi = 1e50; float ma = 1e-50;
                  for(int i = 0; i < 384*288; i++) {
                   //if(imgf[i] < mi) mi = imgf[i];
                   //if(imgf[i] > ma) ma = imgf[i];
                   img[i] = (imgf[i]+1000.0)*(65535.0/2000);
                  }
                  //fprintf(stderr, "min=%f max=%f\n", mi, ma);
                  }
                  break;
          case 4:
          case 5:
                  pthread_mutex_lock(&teb_lock);
                  teb->RecvImage(imgf);
                  teb->CalcEntireTemp(imgf);
                  pthread_mutex_unlock(&teb_lock);
                  //fprintf(stderr, "%f\n", teb->CalcTemp(100,100));
                  for(int i = 0; i < 384*288; i++) {
                   img[i] = (imgf[i]+50.0)*(65535/400);
                  }
                  break;
          case 6:
                  pthread_mutex_lock(&teb_lock);
                  teb->RecvImage(img);
                  teb->CalcEntireTemp(imgf);
                  pthread_mutex_unlock(&teb_lock);
                  for(int i = 0; i < 384*288; i++) {
                   img[i] = (imgf[i]+50.0)*(65535/400);
                  }

                  break;
          default:
                  pthread_mutex_lock(&teb_lock);
                  teb->RecvImage(img);
                  pthread_mutex_unlock(&teb_lock);
  }
  r = write(1, img, sizeof(img));
  if(r != sizeof(img)) return 0;
 }
 return 0;
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

static void do_cmd(const char *cmd, i3::TE_B *teb)
{
     
 int arg;
 int s;
 if(!strcmp(cmd, "r")) {
#if 0
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
#endif
 } else if(!strncmp(cmd, "s ", 2) && get_int_arg(cmd+2, &arg)) {
    img_type = arg;
    fprintf(stderr, "ok\n");
 } else if(!strncmp(cmd, "e ", 2) && get_int_arg(cmd+2, &arg)) {
    pthread_mutex_lock(&teb_lock);
    teb->SetEmissivity((float)arg/100.0);
    pthread_mutex_unlock(&teb_lock);
    fprintf(stderr, "ok\n");
 } else if(!strncmp(cmd, "d", 1)) {
  pthread_mutex_lock(&teb_lock);
  s=teb->UpdateDead();
  pthread_mutex_unlock(&teb_lock);
  if(s == 1) {
   fprintf(stderr, "ok\n");
  } else {
   fprintf(stderr, "update dead failed: %d\n", s);
  }
 } else if(!strcmp(cmd, "f 1")
       || !strcmp(cmd, "f") || !strcmp(cmd, "f 0")) {
  pthread_mutex_lock(&teb_lock);
  s=teb->ShutterCalibrationOn();
  pthread_mutex_unlock(&teb_lock);
  if(s == 1) {
   fprintf(stderr, "ok\n");
  } else {
   fprintf(stderr, "shutter calibration failed: %d\n", s);
  }
                 
                 
#if 0
 } else if(!strcmp(cmd, "f") || !strcmp(cmd, "f 0")) {
	r = LEP_RunSysFFCNormalization(&lepton_i2c);
    if(r != LEP_OK) {
         fprintf(stderr, "do_cmd: run FFC error: %d\n", r);
    } else {
         fprintf(stderr, "ok\n");
    }

#endif
 } else {
         fprintf(stderr, "do_cmd: unknown command \"%s\"\n", cmd);
 }
}

void term(int) {
 want_exit = 1;
}

struct sigaction terminate_on_signal = {
 term
};

extern "C"  void exit(int code) {
 while(1) sleep(1);
}
extern "C"  void _exit(int code) {
 while(1) sleep(1);
}

int
main()
{
    pthread_t thr1;
#define CMD_SIZE 127
    char cmdbuf[CMD_SIZE + 1];
    unsigned pos = 0;
    int r;
    i3::TE_B* teb;
    sigset_t set;

    sigemptyset(&set);
    sigaddset(&set, SIGPIPE);
    sigaddset(&set, SIGINT);
    pthread_sigmask(SIG_BLOCK, &set, NULL);

    teb = i3::OpenTE_B(I3_TE_Q1, 0);
    if(teb == 0) {
     fprintf(stderr, "couldn't open camera\n");
     exit(1);
    }
    r = teb->ReadFlashData();
    if(r != 1) {
     fprintf(stderr, "read flash data failed: %d\n", r);
     exit(1);
    }
    teb->SetEmissivity(1.0);

    pthread_create(&thr1, 0, output_thread, teb);

    pthread_sigmask(SIG_UNBLOCK, &set, NULL);
    sigaction(SIGPIPE, &terminate_on_signal, 0); /* not required */
    sigaction(SIGINT, &terminate_on_signal, 0);

    while(!want_exit) {
     char c;

     ssize_t n;
     n = read(0, &c, 1);
     if(n != 1) break;
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
                      do_cmd(cmdbuf, teb);
                      pos = 0;
                     }
                     break;

     }

    }
    pthread_join(thr1, 0);
    delete teb;
    return 0;
}
