#define COLORMAP SENSOR_SPECIFIC(colormap)
static struct {uint8_t r; uint8_t g; uint8_t b; uint8_t a;}  COLORMAP[MAX_RAW_TEMP+1];
static uint16_t SENSOR_SPECIFIC(palette_map)[MAX_RAW_TEMP+1];

static int SENSOR_SPECIFIC(ChnMatch)(
    Tcl_Channel chan,		/* The image channel, open for reading. */
    const char *fileName,	/* The name of the image file. */
    Tcl_Obj *format,		/* User-specified format object, or NULL. */
    int *widthPtr,        	/* The dimensions of the image are */
	int *heightPtr,			/* returned here if the file is a valid
				 * raw XPM file. */
    Tcl_Interp *interp
) {
  *widthPtr = IMG_W;
  *heightPtr = IMG_H;
    return 1;
}

static int SENSOR_SPECIFIC(ObjMatch)(
    Tcl_Obj *data,		/* The data supplied by the image */
    Tcl_Obj *format,		/* User-specified format object, or NULL. */
    int *widthPtr,		/* The dimensions of the image are */
	int *heightPtr,			/* returned here if the file is a valid
				 * raw XPM file. */
    Tcl_Interp *interp
) {
  *widthPtr = IMG_W;
  *heightPtr = IMG_H;
  return 1;
}

static int 
SENSOR_SPECIFIC(set_palette)
(ClientData clientData, Tcl_Interp *interp,
                       int objc, Tcl_Obj *CONST objv[])
{
 const char *imgname;
 Tk_PhotoHandle ph;
 Tk_PhotoImageBlock block;
 int x, i, p, prev_i;
 int w;

 if (objc != 2) {
  Tcl_WrongNumArgs (interp, 1, objv, "img_name");
  return TCL_ERROR;
 }
 imgname = Tcl_GetString(objv[1]);
 ph = Tk_FindPhoto(interp, imgname);
 //printf("%s %x\n", imgname, ph);
 Tk_PhotoGetImage(ph, &block);
 w = block.width-2;
 if(w < 2 || w > MAX_RAW_TEMP) return TCL_ERROR;
 x = 0;
 prev_i = 0;
 p = 0;
 for(i = 0; i <= MAX_RAW_TEMP+1; i++) {
  x += w;
  if(x >= MAX_RAW_TEMP || i == MAX_RAW_TEMP+1) {
   int di, j;
   int r1, g1, b1, a1;
   int r2, g2, b2, a2;
   di = i-prev_i;
   r1 = block.pixelPtr[block.pixelSize*p+block.offset[0]];
   g1 = block.pixelPtr[block.pixelSize*p+block.offset[1]];
   b1 = block.pixelPtr[block.pixelSize*p+block.offset[2]];
   a1 = block.pixelPtr[block.pixelSize*p+block.offset[3]];
   r2 = block.pixelPtr[block.pixelSize*(p+1)+block.offset[0]];
   g2 = block.pixelPtr[block.pixelSize*(p+1)+block.offset[1]];
   b2 = block.pixelPtr[block.pixelSize*(p+1)+block.offset[2]];
   a2 = block.pixelPtr[block.pixelSize*(p+1)+block.offset[3]];
   for(j = i-1; j >= prev_i; j--) {
    COLORMAP[j].r = r1+(r2-r1)*(j-prev_i)/di;
    COLORMAP[j].g = g1+(g2-g1)*(j-prev_i)/di;
    COLORMAP[j].b = b1+(b2-b1)*(j-prev_i)/di;
    COLORMAP[j].a = a1+(a2-a1)*(j-prev_i)/di;
    /*if(j > 16300) {
     printf("%d %d %d %d %d\n", j,  COLORMAP[j].r,  COLORMAP[j].g,  COLORMAP[j].b, p+1);
    }*/
   }
   x -= MAX_RAW_TEMP;
   p++;
   prev_i = i;
  }
 }
 return TCL_OK;
}

static void
SENSOR_SPECIFIC(map_palette)
(Tcl_Interp *interp, const uint8_t *buf, int linear, 
	    uint16_t min_clip, uint16_t max_clip,
	    uint16_t *min_out, uint16_t *max_out)
{
 static uint8_t pp[MAX_RAW_TEMP+1];
 int x, y, i, prev_i;
 uint16_t npoints = 0;
 double pos;
 uint16_t min, max;

 min = MAX_RAW_TEMP;
 max = 0;
 memset(pp, 0, sizeof(pp));

    for(y = 0; y < IMG_H; y++) {
     for(x = 0; x < IMG_W; x++) {
      uint16_t v;
      v = GET_PIXEL(buf, x, y);
     
      if(v < min) min = v;
      if(v < min_clip) v = min_clip;
      if(v > max) max = v;
      if(v > max_clip) v = max_clip;

      if(pp[v] == 0) {
       pp[v] = 1;
       npoints ++;
      }
     }
    }

    pos = 0; /* TODO: use int arithmetic */
    if(!linear) {
     if(pp[MAX_RAW_TEMP]) npoints--;
     for(i = 0; i <= MAX_RAW_TEMP; i++) {
      uint16_t v = (uint16_t) pos;

      SENSOR_SPECIFIC(palette_map)[i] = v;
      if(pp[i]) pos += (double)MAX_RAW_TEMP/npoints;
     }
    } else {
     if(!pp[MAX_RAW_TEMP]) npoints++;
     prev_i = 0;
     for(i = 0; i <= MAX_RAW_TEMP; i++) {
      if(!pp[i] && i != MAX_RAW_TEMP) continue;
      int j;
      for(j = prev_i; j < i; j++) {
	      /*if(i == prev_i) {
           debug_print(interp, "error at %d\n", i);
           break;
	      }*/
       SENSOR_SPECIFIC(palette_map)[j] = (uint16_t)(pos+((double)MAX_RAW_TEMP/npoints)*(j-prev_i)/(i-prev_i));
       //debug_print(interp, "%d %d\n", j, SENSOR_SPECIFIC(palette_map)[j]);
      }
      prev_i = i + 1;
      pos += (double)MAX_RAW_TEMP/npoints;
      SENSOR_SPECIFIC(palette_map)[i] = (uint16_t)pos;
     }
    }
    //debug_print(interp, "npoints = %d, pos = %lf\n", npoints, pos);
    if(min_out) *min_out = min;
    if(max_out) *max_out = max;
}


static int SENSOR_SPECIFIC(optimize_palette)(ClientData clientData, Tcl_Interp *interp,
                       int objc, Tcl_Obj *CONST objv[])
{
 Tcl_Obj *list;
 Tcl_Obj *minmax[2];
 unsigned char *buf;
 int buf_len;
 int min, max;
 uint16_t min_out, max_out;
 const char *subcmd;
 int linear;
    
 if (objc != 5) {
  Tcl_WrongNumArgs (interp, 1, objv, "alg frame clipmin clipmax");
  return TCL_ERROR;
 }
 subcmd = Tcl_GetString(objv[1]);
 if(!strcmp(subcmd, "linear")) {
  linear = 1;
 } else if(!strcmp(subcmd, "stepped")) {
  linear = 0;
 } else {
  return TCL_ERROR;
 }
 
 buf = Tcl_GetByteArrayFromObj(objv[2], &buf_len);
 if(Tcl_GetIntFromObj(interp, objv[3], &min) != TCL_OK) return TCL_ERROR;
 if(Tcl_GetIntFromObj(interp, objv[4], &max) != TCL_OK) return TCL_ERROR;
 SENSOR_SPECIFIC(map_palette)(interp, buf, linear, min, max, &min_out, &max_out);

 minmax[0] = Tcl_NewIntObj(min_out);
 minmax[1] = Tcl_NewIntObj(max_out);
 
 list = Tcl_NewListObj(2, minmax);
 Tcl_SetObjResult(interp, list);
 return TCL_OK;
}

static void
SENSOR_SPECIFIC(find_minmax)(uint8_t *buf, uint16_t *min_out, uint16_t *max_out)
{
    int x, y; 
    uint16_t min, max; 

	    min = MAX_RAW_TEMP; max = 0;
	    for(y = 0; y < IMG_H; y++) {
		    for(x = 0; x < IMG_W; x++) {
			    uint16_t v;
                v = GET_PIXEL(buf, x, y);
			    if(v < min) min = v;
			    if(v > max) max = v;
		    }
	    }
        if(min_out) *min_out = min;
        if(max_out) *max_out = max;
}

static int SENSOR_SPECIFIC(get_range)(ClientData clientData, Tcl_Interp *interp,
                       int objc, Tcl_Obj *CONST objv[])
{
 Tcl_Obj *list;
 Tcl_Obj *minmax[2];
 uint8_t *buf;
 int buf_len;
    
 uint16_t min, max; 

 if (objc != 2) {
  Tcl_WrongNumArgs (interp, 1, objv, "");
  return TCL_ERROR;
 }
    
 buf = Tcl_GetByteArrayFromObj(objv[1], &buf_len);
 SENSOR_SPECIFIC(find_minmax)(buf, &min, &max);

 minmax[0] = Tcl_NewIntObj(min);
 minmax[1] = Tcl_NewIntObj(max);
 
 list = Tcl_NewListObj(2, minmax);
 Tcl_SetObjResult(interp, list);
 // FIXME: release objects?
 return TCL_OK;
}

static int 
SENSOR_SPECIFIC(fill_gradient_img)
(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
 Tk_PhotoHandle img;
 Tk_PhotoImageBlock block;
 unsigned x, y;
 const char *s;
 int scale_from_b, scale_from_e, scale_to_b, scale_to_e;

 if (objc < 3) {
  Tcl_WrongNumArgs (interp, 1, objv, "");
  return TCL_ERROR;
 }

 s = Tcl_GetString(objv[1]);

 img = Tk_FindPhoto(interp, Tcl_GetString(objv[2]));
 Tk_PhotoGetImage(img, &block);
 block.pixelPtr = (unsigned char *) ckalloc(block.height * block.pitch);
 if(!strcmp(s, "map")) {
  for(y = 0; y < block.height; y++) {
   unsigned i = MAX_RAW_TEMP-y*(MAX_RAW_TEMP+1)/block.height;
   for(x = 0; x < block.width; x++) {
    memcpy(block.pixelPtr+y*block.pitch+x*block.pixelSize, &COLORMAP[SENSOR_SPECIFIC(palette_map)[i]], block.pixelSize);
   }
  }
 } else if(!strcmp(s, "direct")) {
  for(y = 0; y < block.height; y++) {
   int i = MAX_RAW_TEMP-y*(MAX_RAW_TEMP+1)/block.height;
   for(x = 0; x < block.width; x++) {
    memcpy(block.pixelPtr+y*block.pitch+x*block.pixelSize, &COLORMAP[i], block.pixelSize);
   }
  }
 } else if(!strcmp(s, "scale")) {
  if (objc != 7) {
   Tcl_WrongNumArgs (interp, 2, objv, "");
   return TCL_ERROR;
  }
  if(Tcl_GetIntFromObj(interp, objv[3], &scale_from_b) != TCL_OK) return TCL_ERROR;
  if(Tcl_GetIntFromObj(interp, objv[4], &scale_from_e) != TCL_OK) return TCL_ERROR;
  if(Tcl_GetIntFromObj(interp, objv[5], &scale_to_b) != TCL_OK) return TCL_ERROR;
  if(Tcl_GetIntFromObj(interp, objv[6], &scale_to_e) != TCL_OK) return TCL_ERROR;
  for(y = 0; y < block.height; y++) {
   unsigned i = scale_from_e - y*(scale_from_e-scale_from_b)/block.height;
    if(i < scale_to_b) i = 0;
    else if(i >= scale_to_e) i = MAX_RAW_TEMP;
    else i = (i-scale_to_b)*(MAX_RAW_TEMP+1)/(scale_to_e-scale_to_b);
   for(x = 0; x < block.width; x++) {
    memcpy(block.pixelPtr+y*block.pitch+x*block.pixelSize, &COLORMAP[i], block.pixelSize);
   }
  }
 } else if(!strcmp(s, "scalemap")) {
  if (objc != 7) {
   Tcl_WrongNumArgs (interp, 2, objv, "");
   return TCL_ERROR;
  }
  if(Tcl_GetIntFromObj(interp, objv[3], &scale_from_b) != TCL_OK) return TCL_ERROR;
  if(Tcl_GetIntFromObj(interp, objv[4], &scale_from_e) != TCL_OK) return TCL_ERROR;
  if(Tcl_GetIntFromObj(interp, objv[5], &scale_to_b) != TCL_OK) return TCL_ERROR;
  if(Tcl_GetIntFromObj(interp, objv[6], &scale_to_e) != TCL_OK) return TCL_ERROR;
  for(y = 0; y < block.height; y++) {
   unsigned i = scale_from_e - y*(scale_from_e-scale_from_b)/block.height;
    if(i < scale_to_b) i = scale_to_b;
    else if(i >= scale_to_e) i = scale_to_e;
    else i = (i-scale_to_b)*(MAX_RAW_TEMP+1)/(scale_to_e-scale_to_b);
   for(x = 0; x < block.width; x++) {
    memcpy(block.pixelPtr+y*block.pitch+x*block.pixelSize, &COLORMAP[SENSOR_SPECIFIC(palette_map)[i]], block.pixelSize);
   }
  }
 }
     
 Tk_PhotoPutBlock_Panic(img, &block, 0, 0, block.width, block.height, TK_PHOTO_COMPOSITE_SET);
 ckfree(block.pixelPtr);
 return TCL_OK;
}

static int
SENSOR_SPECIFIC(ObjRead)(interp, data, format, imageHandle, destX, destY,
	width, height, srcX, srcY)
    Tcl_Interp *interp;		/* Interpreter to use for reporting errors. */
    Tcl_Obj *data;
    Tcl_Obj *format;		/* User-specified format object, or NULL. */
    Tk_PhotoHandle imageHandle;	/* The photo image to write into. */
    int destX, destY;		/* Coordinates of top-left pixel in
				 * photo image to be written to. */
    int width, height;		/* Dimensions of block of photo image to
				 * be written to. */
    int srcX, srcY;		/* Coordinates of top-left pixel to be used
				 * in image being read. */
{

 unsigned char *buf;
 int buf_len;
    Tk_PhotoImageBlock block;
    int nchan, x, y;
    uint16_t min, max;
    enum therm_cvt_mode mode;

    buf = Tcl_GetByteArrayFromObj(data, &buf_len)+1;
// printf("%u %u %u\n", width, height, buf_len);
    Tk_PhotoGetImage(imageHandle, &block);
//Tk_PhotoExpand_Panic(imageHandle, destX+IMG_W, destY+IMG_H);
    min = 0;
    max = MAX_RAW_TEMP;
    mode = THERM_CVT_SCALE;
    if(format != NULL) {
     Tcl_Obj* obj;
     int min_int, max_int;
     if(Tcl_ListObjIndex(interp, format, 1, &obj) == TCL_OK && obj != NULL) {
       const char *s = Tcl_GetString(obj);
       if(!strcmp(s, "clip")) mode = THERM_CVT_CLIP;
       else if(!strcmp(s, "scale")) mode = THERM_CVT_SCALE;
       else if(!strcmp(s, "map")) mode = THERM_CVT_MAP;
     }
     if(mode != THERM_CVT_MAP) {
      if(Tcl_ListObjIndex(interp, format, 2, &obj) == TCL_OK && obj != NULL) 
       if(Tcl_GetIntFromObj(interp, obj, &min_int) == TCL_OK) min = min_int;
      if(Tcl_ListObjIndex(interp, format, 3, &obj) == TCL_OK && obj != NULL) 
       if(Tcl_GetIntFromObj(interp, obj, &max_int) == TCL_OK) max = max_int;
     }
    }
    
    nchan = block.pixelSize;
    block.pitch = nchan * IMG_W;
    block.width = width;
    block.height = 1;
    block.offset[0] = 0;
    block.offset[1] = 1;
    block.offset[2] = 2;
    block.offset[3] = 3;
    block.pixelPtr = (unsigned char *) ckalloc(nchan * width);


    for(y = 0; y < IMG_H && y < height; y++) {
     for(x = 0; x < IMG_W && x < width; x++) {
      uint16_t v;
      v = GET_PIXEL(buf, x, y);
      if(mode == THERM_CVT_MAP) v = SENSOR_SPECIFIC(palette_map)[v];
      else {
      	if (v <= min) v = 0;
      	else if(v >= max) v = MAX_RAW_TEMP;
      	else if(mode == THERM_CVT_SCALE) v = (uint32_t)(v - min) * MAX_RAW_TEMP / (max-min);
      }
      memcpy(block.pixelPtr+x*nchan, &COLORMAP[v], nchan);
     /* printf("%04x #%02x%02x%02x\n", (buf[addr] << 8) + buf[addr+1],
                      COLORMAP[(buf[addr] << 8) + buf[addr+1]].r,
                      COLORMAP[(buf[addr] << 8) + buf[addr+1]].g,
                      COLORMAP[(buf[addr] << 8) + buf[addr+1]].b
                      );*/
     }
     Tk_PhotoPutBlock_Panic(imageHandle, &block, destX, destY+y, width, 1, TK_PHOTO_COMPOSITE_SET);
    }
    ckfree(block.pixelPtr);
    return TCL_OK;
}

static int 
SENSOR_SPECIFIC(fill_img)
(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{

 unsigned char *buf;
 int buf_len;
 Tk_PhotoHandle img;
 Tk_PhotoImageBlock block;
 int nchan, x, y, width, height;
 uint16_t min, max;
 int min_int, max_int;
 enum therm_cvt_mode mode;
 const char *mode_str;

 if (objc < 4) {
  Tcl_WrongNumArgs (interp, 1, objv, "");
  return TCL_ERROR;
 }

 buf = Tcl_GetByteArrayFromObj(objv[3], &buf_len);
 mode_str = Tcl_GetString(objv[2]);
 img = Tk_FindPhoto(interp, Tcl_GetString(objv[1]));
//Tk_PhotoExpand_Panic(img, IMG_W, IMG_H);
 Tk_PhotoGetImage(img, &block);
 width = block.width < IMG_W ? block.width : IMG_W;
 height = block.height < IMG_H ? block.height : IMG_H;

    min = 0;
    max = MAX_RAW_TEMP;
    mode = THERM_CVT_SCALE;

    if(!strcmp(mode_str, "clip")) mode = THERM_CVT_CLIP;
    else if(!strcmp(mode_str, "scale")) mode = THERM_CVT_SCALE;
    else if(!strcmp(mode_str, "map")) mode = THERM_CVT_MAP;

    if(objc > 4) 
      if(Tcl_GetIntFromObj(interp, objv[4], &min_int) == TCL_OK) min = min_int;
    if(objc > 5) 
      if(Tcl_GetIntFromObj(interp, objv[5], &max_int) == TCL_OK) max = max_int;
    
    nchan = block.pixelSize;
    block.pitch = nchan * width;
    block.width = width;
    block.height = 1;
    block.offset[0] = 0;
    block.offset[1] = 1;
    block.offset[2] = 2;
    block.offset[3] = 3;
    block.pixelPtr = (unsigned char *) ckalloc(nchan * width);

    for(y = 0; y < height; y++) {
     for(x = 0; x < width; x++) {
      uint16_t v;
      v = GET_PIXEL(buf, x, y);
      if(mode == THERM_CVT_MAP) v = SENSOR_SPECIFIC(palette_map)[v];
      else {
      	if (v <= min) v = 0;
      	else if(v >= max) v = MAX_RAW_TEMP;
      	else if(mode == THERM_CVT_SCALE) v = (uint32_t)(v - min) * MAX_RAW_TEMP / (max-min);
      }
      memcpy(block.pixelPtr+x*nchan, &COLORMAP[v], nchan);
     }
     Tk_PhotoPutBlock_Panic(img, &block, 0, y, width, 1, TK_PHOTO_COMPOSITE_SET);
    }
    ckfree(block.pixelPtr);
    return TCL_OK;
}

static Tk_PhotoImageFormat SENSOR_SPECIFIC(img_format) = {
	(char *) STR(PREFIX) "raw",
	SENSOR_SPECIFIC(ChnMatch), /* fileMatchProc */
	SENSOR_SPECIFIC(ObjMatch), /* stringMatchProc */
	0, //ChnRead, /* fileReadProc */
	SENSOR_SPECIFIC(ObjRead), /* stringReadProc */
	0, //ChnWrite, /* fileWriteProc */
	0 //StringWrite /* stringWriteProc */
};
