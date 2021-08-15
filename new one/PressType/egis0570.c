/*
 * Egis Technology Inc. (aka. LighTuning) 0570 driver for libfprint
 * Copyright (C) 2021 Maxim Kolesnikov <kolesnikov@svyazcom.ru>
 * Copyright (C) 2021 Saeed/Ali Rk <saeed.ali.rahimi@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#define FP_COMPONENT "egis0570"

#include "egis0570.h"
#include "drivers_api.h"

/* Packet types */
#define PKT_TYPE_INIT 0
#define PKT_TYPE_REPEAT 1

/* Struct */
struct _FpDeviceEgis0570
{
  FpImageDevice parent;

  gboolean      running;
  gboolean      stop;

  int           pkt_num;
  int           pkt_type;
};
G_DECLARE_FINAL_TYPE (FpDeviceEgis0570, fpi_device_egis0570, FPI, DEVICE_EGIS0570, FpImageDevice);
G_DEFINE_TYPE (FpDeviceEgis0570, fpi_device_egis0570, FP_TYPE_IMAGE_DEVICE);


/*
 * Service
 */

static gboolean
is_last_pkt (FpDevice *dev)
{
  FpDeviceEgis0570 *self = FPI_DEVICE_EGIS0570 (dev);

  int type = self->pkt_type;
  int num = self->pkt_num;

  gboolean r;

  r = ((type == PKT_TYPE_INIT) && (num == (EGIS0570_INIT_TOTAL - 1)));
  r |= ((type == PKT_TYPE_REPEAT) && (num == (EGIS0570_REPEAT_TOTAL - 1)));

  return r;
}

/*
 * Returns a bit for each frame on whether or not a finger has been detected.
 * e.g. 00110 means that there is a finger in frame two and three.
 */
static char
postprocess_frames (guint8 * img)
{
  guint64 mean[EGIS0570_IMGCOUNT] = {0, 0, 0, 0, 0};
  unsigned char covers[EGIS0570_IMGCOUNT];
  char columns[EGIS0570_IMGWIDTH];
  char rows[EGIS0570_IMGHEIGHT];
  char pixel_value;
  char cover;

  for (size_t k = 0; k < EGIS0570_IMGCOUNT; k += 1)
    {
      guint8 * frame = &img[(k * EGIS0570_IMGSIZE)];

      cover = 0;
      
      for (unsigned char i=0; i < EGIS0570_IMGHEIGHT; i++)
        rows[i] = 0;
      for (unsigned char i=0; i < EGIS0570_IMGWIDTH; i++)
        columns[i] = 0;

      for (unsigned char i = 0; i < EGIS0570_IMGWIDTH; i += 1)
        {
          for (unsigned char j=0; j < EGIS0570_IMGHEIGHT; j+=1 )
          {
            pixel_value = (frame[j*EGIS0570_IMGWIDTH + i] > EGIS0570_ON_PIXEL) ? 1 : 0;
            columns[i] += pixel_value;
            rows[j] += pixel_value;
            mean[k] += frame[j*EGIS0570_IMGWIDTH + i];
          }
        }
      
      for(unsigned char i=0; i<EGIS0570_IMGHEIGHT; i++)
        cover += ((rows[i] > (EGIS0570_IMGWIDTH/2)) ? 1 : 0);
      for(unsigned char i=0; i<EGIS0570_IMGWIDTH; i++)
        cover += ((columns[i] > (EGIS0570_IMGHEIGHT/2)) ? 1 : 0);
      
      covers[k] = cover;
      mean[k] /= EGIS0570_IMGWIDTH * EGIS0570_IMGHEIGHT;
    }

  char result = -1;
  guint64 score =  mean[0]*covers[0];
  
  for (size_t k = 0; k < EGIS0570_IMGCOUNT; k += 1)
    {
      fp_dbg ("Finger status (picture number, mean, cover) : %ld , %ld, %d", k, mean[k], covers[k]);
      if ((mean[k] > EGIS0570_MIN_MEAN) && (covers[k] > EGIS0570_COVER))
        {
        mean[k] *= covers[k];       
        if(score < mean[k])
        {
          result = k;
          score = mean[k];
        }
      }
    }

  return result;
}

/*
 * Device communication
 */

static void
data_resp_cb (FpiUsbTransfer *transfer, FpDevice *dev, gpointer user_data, GError *error)
{
  FpImageDevice *img_self = FP_IMAGE_DEVICE (dev);

  if (error)
    {
      fpi_ssm_mark_failed (transfer->ssm, error);
      return;
    }

  int where_finger_is = postprocess_frames (transfer->buffer);

  if (where_finger_is > -1)
    {
      fpi_image_device_report_finger_status (img_self, TRUE);
      FpImage *img = fp_image_new(EGIS0570_IMGWIDTH, EGIS0570_IMGHEIGHT);
      memcpy (img -> data, (transfer -> buffer) + (where_finger_is * EGIS0570_IMGSIZE) , EGIS0570_IMGSIZE);
      img->flags |= (FPI_IMAGE_COLORS_INVERTED | FPI_IMAGE_PARTIAL);
      fpi_image_device_image_captured (img_self, img);
      fpi_image_device_report_finger_status (img_self, FALSE);
    }
  else
    {
      fpi_image_device_report_finger_status (img_self, FALSE);
    }
  fpi_ssm_next_state (transfer->ssm);
}

static void
recv_data_resp (FpiSsm *ssm, FpDevice *dev)
{
  FpiUsbTransfer *transfer = fpi_usb_transfer_new (dev);

  fpi_usb_transfer_fill_bulk (transfer, EGIS0570_EPIN, EGIS0570_INPSIZE);

  transfer->ssm = ssm;
  transfer->short_is_error = TRUE;

  fpi_usb_transfer_submit (transfer, EGIS0570_TIMEOUT, NULL, data_resp_cb, NULL);
}

static void
cmd_resp_cb (FpiUsbTransfer *transfer, FpDevice *dev, gpointer user_data, GError *error)
{
  if (error)
    fpi_ssm_mark_failed (transfer->ssm, error);
}

static void
recv_cmd_resp (FpiSsm *ssm, FpDevice *dev)
{
  FpiUsbTransfer *transfer = fpi_usb_transfer_new (dev);

  fpi_usb_transfer_fill_bulk (transfer, EGIS0570_EPIN, EGIS0570_PKTSIZE);

  transfer->ssm = ssm;

  fpi_usb_transfer_submit (transfer, EGIS0570_TIMEOUT, NULL, cmd_resp_cb, NULL);
}

static void
send_cmd_req (FpiSsm *ssm, FpDevice *dev, unsigned char *pkt)
{
  FpiUsbTransfer *transfer = fpi_usb_transfer_new (dev);

  fpi_usb_transfer_fill_bulk_full (transfer, EGIS0570_EPOUT, pkt, EGIS0570_PKTSIZE, NULL);

  transfer->ssm = ssm;
  transfer->short_is_error = TRUE;

  fpi_usb_transfer_submit (transfer, EGIS0570_TIMEOUT, NULL, fpi_ssm_usb_transfer_cb, NULL);
}

/*
 * SSM States
 */

enum sm_states {
  SM_INIT,
  SM_START,
  SM_REQ,
  SM_RESP,
  SM_REC_DATA,
  SM_DONE,
  SM_STATES_NUM
};

static void
ssm_run_state (FpiSsm *ssm, FpDevice *dev)
{
  FpDeviceEgis0570 *self = FPI_DEVICE_EGIS0570 (dev);
  FpImageDevice *img_dev = FP_IMAGE_DEVICE (dev);

  switch (fpi_ssm_get_cur_state (ssm))
    {
    case SM_INIT:
      self->pkt_type = PKT_TYPE_INIT;
      fpi_ssm_next_state (ssm);
      break;

    case SM_START:
      if (self->stop)
        {
          fp_dbg ("deactivating, marking completed");
          fpi_ssm_mark_completed (ssm);
          fpi_image_device_deactivate_complete (img_dev, NULL);
        }
      else
        {
          self->pkt_num = 0;
          fpi_ssm_next_state (ssm);
        }
      break;

    case SM_REQ:
      if (self->pkt_type == PKT_TYPE_INIT)
        send_cmd_req (ssm, dev, init_pkts[self->pkt_num]);
      else
        send_cmd_req (ssm, dev, repeat_pkts[self->pkt_num]);
      break;

    case SM_RESP:
      if (is_last_pkt (dev) == FALSE)
        {
          recv_cmd_resp (ssm, dev);
          self->pkt_num += 1;
          fpi_ssm_jump_to_state (ssm, SM_REQ);
        }
      else
        {
          if (self->pkt_type == PKT_TYPE_INIT)
            self->pkt_type = PKT_TYPE_REPEAT;

          fpi_ssm_next_state (ssm);
        }
      break;

    case SM_REC_DATA:
      recv_data_resp (ssm, dev);
      break;

    case SM_DONE:
      fpi_ssm_jump_to_state (ssm, SM_START);
      break;

    default:
      g_assert_not_reached ();
    }
}

/*
 * Activation
 */

static void
loop_complete (FpiSsm *ssm, FpDevice *dev, GError *error)
{
  FpImageDevice *img_dev = FP_IMAGE_DEVICE (dev);
  FpDeviceEgis0570 *self = FPI_DEVICE_EGIS0570 (dev);

  self->running = FALSE;

  if (error)
    fpi_image_device_session_error (img_dev, error);
}

static void
dev_activate (FpImageDevice *dev)
{
  FpDeviceEgis0570 *self = FPI_DEVICE_EGIS0570 (dev);
  FpiSsm *ssm = fpi_ssm_new (FP_DEVICE (dev), ssm_run_state, SM_STATES_NUM);

  self->stop    = FALSE;

  fpi_ssm_start (ssm, loop_complete);

  self->running = TRUE;

  fpi_image_device_activate_complete (dev, NULL);
}

/*
 * Opening
 */

static void
dev_init (FpImageDevice *dev)
{
  GError *error = NULL;

  g_usb_device_claim_interface (fpi_device_get_usb_device (FP_DEVICE (dev)), 0, 0, &error);

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

  if (self->running)
    self->stop = TRUE;
  else
    fpi_image_device_deactivate_complete (dev, NULL);
}

/*
 * Driver data
 */

static const FpIdEntry id_table[] = {
  { .vid = 0x1c7a, .pid = 0x0570, },
  { .vid = 0x1c7a, .pid = 0x0571, },
  { .vid = 0,      .pid = 0, },
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
  dev_class->scan_type = FP_SCAN_TYPE_SWIPE;

  img_class->img_open = dev_init;
  img_class->img_close = dev_deinit;
  img_class->activate = dev_activate;
  img_class->deactivate = dev_deactivate;

  img_class->img_width = EGIS0570_IMGWIDTH;
  img_class->img_height = EGIS0570_IMGHEIGHT;

  img_class->bz3_threshold = EGIS0570_BZ3_THRESHOLD; /* security issue */
}
