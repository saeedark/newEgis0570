#define FP_COMPONENT "egis0570"

#include "egis0570.h"
#include "drivers_api.h"

/* Packet types */
#define PKT_TYPE_INIT	 0
#define PKT_TYPE_REPEAT	 1

struct _FpDeviceEgis0570
{
  FpImageDevice 			parent;

  gboolean					running;
  gboolean					stop;
  gboolean 					retry;

  int 						pkt_num;
  int 						pkt_type;

  FpImage      			   *img;
};
G_DECLARE_FINAL_TYPE (FpDeviceEgis0570, fpi_device_egis0570, FPI, DEVICE_EGIS0570,
                      FpImageDevice);
G_DEFINE_TYPE (FpDeviceEgis0570, fpi_device_egis0570, FP_TYPE_IMAGE_DEVICE);

enum sm_states
{
	SM_START,
	SM_REQ,
	SM_RESP,
	SM_RESP_CB,
	SM_DATA,
	SM_DATA_PROC,
	SM_STATES_NUM
};

static void 
state_complete(Fpi_ssm *ssm, gboolean img_free)
{
	FpDeviceEgis0570 *self = FPI_DEVICE_EGIS0570 (dev);
	
	int r = ssm -> error;

	fpi_ssm_free(ssm);
	
	if (img_free)
		g_object_unref (self -> capture_img);
	self -> img = NULL;
	self -> running = FALSE;
	self -> retry = FALSE;
	if (r)
		fpi_imgdev_session_error(dev, r);

	if (self -> stop)
		fpi_imgdev_deactivate_complete(dev);
}

static void 
ssm_run_state(Fpi_ssm *ssm, FpDevice *dev)
{
	FpDeviceEgis0570 *self = FPI_DEVICE_EGIS0570 (dev);
	
	switch (fpi_ssm_get_cur_state (ssm))
	{
		case SM_START:
			self -> running = TRUE;
			self -> pkt_num = 0;
			fpi_ssm_next_state(ssm);
			break;

		case SM_REQ:
			if (self -> pkt_type == PKT_TYPE_INIT)
				send_cmd_req(ssm, init_pkts[self -> pkt_num]); //todo
			else
				send_cmd_req(ssm, repeat_pkts[self -> pkt_num]); //todo
			break;

		case SM_RESP:
			if (is_last_pkt(self) == FALSE) //todo
			{
				recv_cmd_resp(ssm); //todo
				++(self -> pkt_num);
			}
			else
				fpi_ssm_jump_to_state(ssm, SM_DATA); //todo
			break;

		case SM_RESP_CB:
			fpi_ssm_jump_to_state(ssm, SM_REQ); //todo
			break;

		case SM_DATA:
			recv_data_resp(ssm); //todo
			break;

		case SM_DATA_PROC:
			break;

		default:
      		g_assert_not_reached ();
	}
}

static void
activation_run_states (FpiSsm *ssm, FpDevice *dev)
{
  FpDeviceEgis0570 *self = FPI_DEVICE_EGIS0570 (dev);

  ssm_run_state(ssm, dev);

  switch (fpi_ssm_get_cur_state (ssm))
    {
    case SM_START:
      self -> pkt_type = PKT_TYPE_INIT;
      break;

    case SM_DATA_PROC:
      fpi_ssm_next_state(ssm);
      break;

    default:
      g_assert_not_reached ();
    }
}

static void 
activation_complete(FpiSsm *ssm, FpDevice *dev, GError *error)
{
	FpDeviceEgis0570 *self = FPI_DEVICE_EGIS0570 (dev);
	
	state_complete(ssm, TRUE);

	if (self -> stop == FALSE)
		fcheck_start(dev); //todo
	
	fpi_image_device_activate_complete (FP_IMAGE_DEVICE (dev), error);
}


static void
dev_init (FpImageDevice *dev)
{
  GError *error = NULL;

  FpDeviceEgis0570 *self = FPI_DEVICE_EGIS0570 (dev);

  g_usb_device_claim_interface (fpi_device_get_usb_device (FP_DEVICE (dev)), 0, 0, &error);

  self -> running = FALSE;
  self -> stop	= FALSE;
  self -> retry   = FALSE;
  self -> img     = NULL;

  fpi_image_device_open_complete (dev, error);
}

static void
dev_deinit (FpImageDevice *dev)
{
  GError *error = NULL;

  g_usb_device_release_interface (fpi_device_get_usb_device (FP_DEVICE (dev)), 0, 0, &error);

  fpi_image_device_close_complete (dev, error);
}

static void
dev_activate (FpImageDevice *dev)
{
  FpDeviceEgis0570 *self = FPI_DEVICE_EGIS0570 (dev);
  FpiSsm *ssm = fpi_ssm_new (FP_DEVICE (dev), activation_run_states, SM_STATES_NUM);

  self -> stop = FALSE;
  fpi_ssm_start (ssm, activation_complete);
  
  fpi_image_device_activate_complete (dev, NULL);
}

static void
dev_deactivate (FpImageDevice *dev)
{
  FpDeviceEgis0570 *self = FPI_DEVICE_EGIS0570 (dev);

  if (self -> running)
    self -> stop = TRUE;
  else
    fpi_image_device_deactivate_complete (dev, NULL);
}

static const FpIdEntry id_table[] = {
	{ .vid = EGIS0570_VID, .pid = EGIS0570_PID, }, 
	{ .vid = 0,  		   .pid = 0,  	.driver_data = 0 }, 		/* terminating entry */
};

static void
fpi_device_egis0570_init (FpDeviceVcom5s *self)
{
}

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