#define FP_COMPONENT "egis0570"

#include "egis0570.h"
#include "drivers_api.h"

struct _FpDeviceEgis0570
{
  FpImageDevice 			parent;
};
G_DECLARE_FINAL_TYPE (FpDeviceEgis0570, fpi_device_egis0570, FPI, DEVICE_EGIS0570,
                      FpImageDevice);
G_DEFINE_TYPE (FpDeviceEgis0570, fpi_device_egis0570, FP_TYPE_IMAGE_DEVICE);

static void
fpi_device_egis0570_class_init (FpDeviceEgis0570Class *klass)
{
  FpDeviceClass *dev_class = FP_DEVICE_CLASS (klass);
  FpImageDeviceClass *img_class = FP_IMAGE_DEVICE_CLASS (klass);

  dev_class->id = "egis0570";
  dev_class->full_name = "Egis Technology Inc. (aka. LighTuning) 0570";
  dev_class->type = FP_DEVICE_TYPE_USB;
  dev_class->id_table = id_table;
  dev_class->scan_type = FP_SCAN_TYPE_PRESS;

  img_class->img_open = dev_init;
  img_class->img_close = dev_deinit;
  img_class->activate = dev_activate;
  img_class->deactivate = dev_deactivate;

  img_class->img_width = EGIS0570_IMGWIDTH;
  img_class->img_height = EGIS0570_IMGHEIGHT;
  
  img_class->bz3_threshold = EGIS0570_BZ3_THRESHOLD; /* security issue */
}