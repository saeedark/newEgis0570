#define FP_COMPONENT "egis0570"

#include "egis0570.h"
#include "drivers_api.h"

/* Packet types */
#define PKT_TYPE_INIT	 0
#define PKT_TYPE_REPEAT	 1

/* Struct */
struct _FpDeviceEgis0570
{
  FpImageDevice					parent;

	gboolean					running;
	gboolean					stop;
	gboolean					retry;

	int							pkt_num;
	int							pkt_type;

	FpImage						*img;
};
G_DECLARE_FINAL_TYPE (FpDeviceEgis0570, fpi_device_egis0570, FPI, DEVICE_EGIS0570, FpImageDevice);
G_DEFINE_TYPE (FpDeviceEgis0570, fpi_device_egis0570, FP_TYPE_IMAGE_DEVICE);

/*
 * Service
 */

static gboolean 
is_last_pkt(FpDevice *dev)
{
	FpDeviceEgis0570 *self = FPI_DEVICE_EGIS0570 (dev);

	int type = self -> pkt_type;
	int num = self -> pkt_num;
	
	gboolean r;
	
	r = ((type == PKT_TYPE_INIT) && (num == (EGIS0570_INIT_TOTAL - 1)));
	r |= ((type == PKT_TYPE_REPEAT) && (num == (EGIS0570_REPEAT_TOTAL - 1)));
	
	return r;
}

static int 
finger_status(FpImage * img)
{
	size_t total[5] = {0, 0, 0, 0, 0};
	size_t max_total_value = 0;
	int max_total_id = 0;
	unsigned char min, max;
	min = max = img -> data[0];

	for (size_t k = 0; k < EGIS0570_IMGCOUNT; ++k)
	{
		for (size_t i = (k * EGIS0570_IMGSIZE); i < ((k + 1) * EGIS0570_IMGSIZE); ++i)
		{
			total[k] += img -> data[i];

			if (img -> data[i] < min)
				min = img -> data[i];

			if (img -> data[i] > max)
				max = img -> data[i];
		}

		if (total[k] > max_total_value)
			max_total_id = k;
	}

	unsigned char avg = total[max_total_id] / EGIS0570_IMGSIZE;

	int result  = -1;
	if ((avg > EGIS0570_MIN_FINGER_PRESENT_AVG) && (min < EGIS0570_MAX_MIN)) /* ReThink on values */
		result = max_total_id;

	fp_dbg("Finger status (min, max, biggest avg) : %d : %d - %d", min, max, avg);

	return result;
}

/*
 * SSM States
 */

static void recv_data_resp(FpiSsm *ssm, FpDevice *dev);
static void recv_cmd_resp(FpiSsm *ssm, FpDevice *dev);
static void send_cmd_req(FpiSsm *ssm, FpDevice *dev, unsigned char *pkt);

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
state_complete(FpiSsm *ssm, FpDevice *dev, gboolean img_free)
{
	FpDeviceEgis0570 *self = FPI_DEVICE_EGIS0570 (dev);
	
	GError * error = fpi_ssm_get_error (ssm);
	
	if (img_free)
		g_object_unref (self -> img);
	self -> img = NULL;
	self -> running = FALSE;
	self -> retry = FALSE;

	if (error)
		fpi_image_device_session_error(dev, error);

	if (self -> stop)
		fpi_image_device_deactivate_complete(dev, NULL);

	fpi_ssm_free(ssm);
}

static void 
ssm_run_state(FpiSsm *ssm, FpDevice *dev)
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
				send_cmd_req(ssm, dev, init_pkts[self -> pkt_num]); //todo
			else
				send_cmd_req(ssm, dev, repeat_pkts[self -> pkt_num]); //todo
			break;

		case SM_RESP:
			if (is_last_pkt(self) == FALSE) //todo
			{
				recv_cmd_resp(ssm, dev); //todo
				++(self -> pkt_num);
			}
			else
				fpi_ssm_jump_to_state(ssm, SM_DATA); //todo
			break;

		case SM_RESP_CB:
			fpi_ssm_jump_to_state(ssm, SM_REQ); //todo
			break;

		case SM_DATA:
			recv_data_resp(ssm, self); //todo
			break;

		case SM_DATA_PROC:
			break;

		default:
			g_assert_not_reached ();
	}
}

/*
 * Device communication
 */

static void
data_resp_cb(FpiUsbTransfer *transfer, FpDevice *dev, gpointer user_data, GError *error)
{
	FpImageDevice *self = FP_IMAGE_DEVICE (dev);
	FpDeviceEgis0570 *img_self = FPI_DEVICE_EGIS0570 (dev);

	if (error)
	{
		fp_dbg("request is not completed, %s", error -> message);
		fpi_ssm_mark_failed (transfer -> ssm, error);
		return;
	}

	img_self -> img = fp_image_new (EGIS0570_IMGWIDTH, EGIS0570_IMGHEIGHT * EGIS0570_IMGCOUNT);
  	FpImage *capture_img = img_self -> img;
  	memcpy (capture_img -> data, transfer -> buffer, EGIS0570_IMGSIZE);


	fpi_ssm_next_state (transfer -> ssm);
}

static void
recv_data_resp(FpiSsm *ssm, FpDevice *dev)
{
	FpiUsbTransfer *transfer = fpi_usb_transfer_new (dev);

	fpi_usb_transfer_fill_bulk (transfer, EGIS0570_EPIN, EGIS0570_INPSIZE);
	transfer -> ssm = ssm;

	fpi_usb_transfer_submit (transfer, EGIS0570_TIMEOUT, NULL, data_resp_cb, NULL);
}

static void
cmd_resp_cb(FpiUsbTransfer *transfer, FpDevice *dev, gpointer user_data, GError *error)
{
	if (!error)
	{	
		fpi_ssm_jump_to_state (transfer -> ssm, SM_RESP_CB);
	}
	else
	{
		fp_dbg ("bad answer, %s", error->message);
		fpi_ssm_mark_failed (transfer -> ssm, error);	
	}
}

static void 
recv_cmd_resp(FpiSsm *ssm, FpDevice *dev)
{
	FpiUsbTransfer *transfer = fpi_usb_transfer_new (dev);

	fpi_usb_transfer_fill_bulk (transfer, EGIS0570_EPIN, EGIS0570_PKTSIZE);
	transfer->ssm = ssm;

	fpi_usb_transfer_submit (transfer, EGIS0570_TIMEOUT, NULL, cmd_resp_cb, NULL);

}

static void 
cmd_req_cb(FpiUsbTransfer *transfer, FpDevice *dev, gpointer user_data, GError *error)
{
	if (!error)
	{
		fpi_ssm_next_state (transfer -> ssm);
	}
	else
	{
		fp_dbg ("request is not completed, %s", error->message);
		fpi_ssm_mark_failed (transfer->ssm, error);
		return;
	}


}

static void 
send_cmd_req(FpiSsm *ssm, FpDevice *dev, unsigned char *pkt)
{
	FpiUsbTransfer *transfer = fpi_usb_transfer_new (dev);

	fpi_usb_transfer_fill_bulk_full (transfer, EGIS0570_EPOUT, pkt, EGIS0570_PKTSIZE, NULL);
	transfer -> ssm = ssm;
	transfer -> short_is_error = TRUE;

	fpi_usb_transfer_submit (transfer, EGIS0570_TIMEOUT, NULL, cmd_req_cb, NULL);
}

/*
 * Capture
 */

static void fcheck_start(FpDevice *dev);

static void 
capture_run_state(FpiSsm *ssm, FpDevice *dev)
{
	FpImageDevice *imgdev = FP_IMAGE_DEVICE (dev);
	FpDeviceEgis0570 *self = FPI_DEVICE_EGIS0570 (dev);

	ssm_run_state(ssm, self);

	switch (fpi_ssm_get_cur_state (ssm))
	{
		case SM_START:
			self -> pkt_type = PKT_TYPE_REPEAT;
			break;

		case SM_DATA_PROC:
		{
			FpImage *capture_img;
			capture_img = self -> img;
			fpi_image_device_report_finger_status (imgdev, FALSE);
			fpi_image_device_image_captured (imgdev, capture_img);
			fpi_ssm_next_state(ssm);
			break;
		}

		default:
			g_assert_not_reached ();
	}
	
}

static void 
capture_complete(FpiSsm *ssm, FpDevice *dev, GError *error)
{
	FpDeviceEgis0570 *self = FPI_DEVICE_EGIS0570 (dev);

	state_complete(ssm, dev, FALSE); 

	if (self -> stop == FALSE)
		fcheck_start(dev); 
}

static void 
capture_start(FpDevice *dev)
{
	FpiSsm *ssm = fpi_ssm_new (FP_DEVICE (dev), capture_run_state, SM_STATES_NUM);

	fpi_ssm_start(ssm, capture_complete);
}

/*
 * Finger check
 */

static void 
fcheck_run_state(FpiSsm *ssm, FpDevice *dev)
{
	FpDeviceEgis0570 *self = FPI_DEVICE_EGIS0570 (dev);

	ssm_run_state(ssm, self);

	switch (fpi_ssm_get_cur_state (ssm))
	{
		case SM_START:
			self -> pkt_type = PKT_TYPE_REPEAT;
			break;

		case SM_DATA_PROC:
		{
			int finger_state = finger_status(self -> img);
			if (finger_state + 1) 
			{
				fpi_image_device_report_finger_status(dev, TRUE);
				g_object_unref (self -> img);
				self -> img = NULL;
				fpi_ssm_next_state(ssm);
			}
			else
				fpi_ssm_jump_to_state(ssm, SM_START);
			break;
		}
		
		default:
			g_assert_not_reached ();
	}
	
}

static void 
fcheck_complete(FpiSsm *ssm, FpDevice *dev, GError *error)
{
	FpDeviceEgis0570 *self = FPI_DEVICE_EGIS0570 (dev);

	state_complete(ssm, dev, TRUE); 

	if (self -> stop == FALSE)
		capture_start(dev); //todo
}

static void 
fcheck_start(FpDevice *dev)
{
	FpiSsm *ssm = fpi_ssm_new (FP_DEVICE (dev), fcheck_run_state, SM_STATES_NUM);

	fpi_ssm_start(ssm, fcheck_complete);
}

/*
 * Activation
 */

static void
activation_run_states (FpiSsm *ssm, FpDevice *dev)
{
	FpDeviceEgis0570 *self = FPI_DEVICE_EGIS0570 (dev);

	ssm_run_state(ssm, self);

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
	
	state_complete(ssm, dev, TRUE);

	if (self -> stop == FALSE)
		fcheck_start(dev); 
	
	fpi_image_device_activate_complete (FP_IMAGE_DEVICE (dev), error);
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

/*
 * Opening
 */

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

/*
 * Closing
 */

static void
dev_deinit (FpImageDevice *dev)
{
	GError *error = NULL;

	g_usb_device_release_interface (fpi_device_get_usb_device (FP_DEVICE (dev)), 0, 0, &error);

	fpi_image_device_close_complete (dev, error);
}

/*
 * Deactivation
 */

static void
dev_deactivate (FpImageDevice *dev)
{
	FpDeviceEgis0570 *self = FPI_DEVICE_EGIS0570 (dev);

	if (self -> running)
		self -> stop = TRUE;
	else
		fpi_image_device_deactivate_complete (dev, NULL);
}

/*
 * Driver data
 */

static const FpIdEntry id_table[] = {
	{ .vid = EGIS0570_VID, .pid = EGIS0570_PID, }, 
	{ .vid = 0,  		   .pid = 0,  	.driver_data = 0 }, 		/* terminating entry */
};

static void
fpi_device_egis0570_init (FpDeviceEgis0570 *self)
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