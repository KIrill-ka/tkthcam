#include <string.h>
#include <stdint.h>
#include <tk.h>

enum therm_cvt_mode {
  THERM_CVT_CLIP,
  THERM_CVT_SCALE,
  THERM_CVT_MAP
};

extern DLLEXPORT int Thermimg_Init(Tcl_Interp *interp);

static void debug_print(Tcl_Interp *interp, const char *fmt, ...)
{
	va_list ap;
	char b[512];
	Tcl_Channel chan;

	va_start(ap, fmt);
	vsprintf(b, fmt, ap);
	va_end(ap);

	chan = Tcl_GetChannel(interp, "stdout", NULL);
	Tcl_WriteChars(chan, b, -1);
}

#define xSTR(x) #x
#define STR(x) xSTR(x)

/* FLIR lepton 3 */
#define IMG_W 160
#define IMG_H 120
#define MAX_RAW_TEMP 16383
#define GET_PIXEL get_pixel_lepton3
static inline uint16_t
get_pixel_lepton3(const uint8_t *buf, int x, int y) {
      int addr = y*(4+IMG_W+4+IMG_W)+4+x*2;
      if(x >= 80) addr += 4;
      return (buf[addr] << 8) + buf[addr+1];
}
#define PREFIX lepton3
#define SENSOR_SPECIFIC(name) lepton3 ## _ ## name
#include "thermimg_impl.c"

#undef IMG_W
#undef IMG_H
#undef MAX_RAW_TEMP
#undef GET_PIXEL
#undef PREFIX
#undef SENSOR_SPECIFIC

/* i3system TEQ1 */
#define IMG_W 384
#define IMG_H 288
#define MAX_RAW_TEMP 65535
#define GET_PIXEL get_pixel_teq1
static inline uint16_t
get_pixel_teq1(const uint8_t *buf, int x, int y) {
      int addr = (y*IMG_W+x)*2;
      return (buf[addr+1] << 8) + buf[addr];
}
#define PREFIX teq1
#define SENSOR_SPECIFIC(name) teq1 ## _ ## name
#include "thermimg_impl.c"

#undef IMG_W
#undef IMG_H
#undef MAX_RAW_TEMP
#undef GET_PIXEL
#undef PREFIX
#undef SENSOR_SPECIFIC

int
Thermimg_Init(Tcl_Interp *interp)
{
	if (!Tcl_InitStubs(interp, "8.3", 0)) {
		return TCL_ERROR;
	}
	if (!Tk_InitStubs(interp, "8.3", 0)) {
		return TCL_ERROR;
	}

   Tk_CreatePhotoImageFormat(&lepton3_img_format);
   Tk_CreatePhotoImageFormat(&teq1_img_format);
   Tcl_CreateObjCommand (interp, "thermImg::lepton3::setPalette", lepton3_set_palette, NULL, (Tcl_CmdDeleteProc *)NULL);
   Tcl_CreateObjCommand (interp, "thermImg::lepton3::remapPalette", lepton3_optimize_palette, NULL, (Tcl_CmdDeleteProc *)NULL);
   Tcl_CreateObjCommand (interp, "thermImg::lepton3::fillPaletteImg", lepton3_fill_gradient_img, NULL, (Tcl_CmdDeleteProc *)NULL);
   Tcl_CreateObjCommand (interp, "thermImg::lepton3::getRange", lepton3_get_range, NULL, (Tcl_CmdDeleteProc *)NULL);
   Tcl_CreateObjCommand (interp, "thermImg::lepton3::fillImg", lepton3_fill_img, NULL, (Tcl_CmdDeleteProc *)NULL);
   Tcl_CreateObjCommand (interp, "thermImg::teq1::setPalette", teq1_set_palette, NULL, (Tcl_CmdDeleteProc *)NULL);
   Tcl_CreateObjCommand (interp, "thermImg::teq1::remapPalette", teq1_optimize_palette, NULL, (Tcl_CmdDeleteProc *)NULL);
   Tcl_CreateObjCommand (interp, "thermImg::teq1::fillPaletteImg", teq1_fill_gradient_img, NULL, (Tcl_CmdDeleteProc *)NULL);
   Tcl_CreateObjCommand (interp, "thermImg::teq1::getRange", teq1_get_range, NULL, (Tcl_CmdDeleteProc *)NULL);
   Tcl_CreateObjCommand (interp, "thermImg::teq1::fillImg", teq1_fill_img, NULL, (Tcl_CmdDeleteProc *)NULL);

   if (Tcl_PkgProvide(interp, "ThermImg", "1.0") == TCL_ERROR) {
         return TCL_ERROR;
   }

   return TCL_OK;
}
