/******************************************************************************
*                                                                             *
*  easycap_sound.c                                                            *
*                                                                             *
*  Audio driver for EasyCAP USB2.0 Video Capture Device DC60                  *
*                                                                             *
*                                                                             *
******************************************************************************/
/*
 *
 *  Copyright (C) 2010 R.M. Thomas  <rmthomas@sciolus.org>
 *
 *
 *  This is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  The software is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this software; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
*/
/*****************************************************************************/

#include "easycap.h"

#ifndef CONFIG_EASYCAP_OSS
/*--------------------------------------------------------------------------*/
/*
 *  PARAMETERS USED WHEN REGISTERING THE AUDIO INTERFACE
 */
/*--------------------------------------------------------------------------*/
static const struct snd_pcm_hardware alsa_hardware = {
	.info = SNDRV_PCM_INFO_BLOCK_TRANSFER |
		SNDRV_PCM_INFO_MMAP           |
		SNDRV_PCM_INFO_INTERLEAVED    |
		SNDRV_PCM_INFO_MMAP_VALID,
	.formats = SNDRV_PCM_FMTBIT_S16_LE,
	.rates = SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_48000,
	.rate_min = 32000,
	.rate_max = 48000,
	.channels_min = 2,
	.channels_max = 2,
	.buffer_bytes_max = PAGE_SIZE * PAGES_PER_AUDIO_FRAGMENT *
						AUDIO_FRAGMENT_MANY,
	.period_bytes_min = PAGE_SIZE * PAGES_PER_AUDIO_FRAGMENT,
	.period_bytes_max = PAGE_SIZE * PAGES_PER_AUDIO_FRAGMENT * 2,
	.periods_min = AUDIO_FRAGMENT_MANY,
	.periods_max = AUDIO_FRAGMENT_MANY * 2,
};


/*****************************************************************************/
/*---------------------------------------------------------------------------*/
/*
 *  ON COMPLETION OF AN AUDIO URB ITS DATA IS COPIED TO THE DAM BUFFER
 *  PROVIDED peasycap->audio_idle IS ZERO.  REGARDLESS OF THIS BEING TRUE,
 *  IT IS RESUBMITTED PROVIDED peasycap->audio_isoc_streaming IS NOT ZERO.
 */
/*---------------------------------------------------------------------------*/
void
easycap_alsa_complete(struct urb *purb)
{
struct easycap *peasycap;
struct snd_pcm_substream *pss;
struct snd_pcm_runtime *prt;
int dma_bytes, fragment_bytes;
int isfragment;
__u8 *p1, *p2;
__s16 s16;
int i, j, more, much, rc;
#if defined(UPSAMPLE)
int k;
__s16 oldaudio, newaudio, delta;
#endif /*UPSAMPLE*/

JOT(16, "\n");

if (NULL == purb) {
	SAY("ERROR: purb is NULL\n");
	return;
}
peasycap = purb->context;
if (NULL == peasycap) {
	SAY("ERROR: peasycap is NULL\n");
	return;
}
if (memcmp(&peasycap->telltale[0], TELLTALE, strlen(TELLTALE))) {
	SAY("ERROR: bad peasycap\n");
	return;
}
much = 0;
if (peasycap->audio_idle) {
	JOM(16, "%i=audio_idle  %i=audio_isoc_streaming\n",
			peasycap->audio_idle, peasycap->audio_isoc_streaming);
	if (peasycap->audio_isoc_streaming)
		goto resubmit;
}
/*---------------------------------------------------------------------------*/
pss = peasycap->psubstream;
if (NULL == pss)
	goto resubmit;
prt = pss->runtime;
if (NULL == prt)
	goto resubmit;
dma_bytes = (int)prt->dma_bytes;
if (0 == dma_bytes)
	goto resubmit;
fragment_bytes = 4 * ((int)prt->period_size);
if (0 == fragment_bytes)
	goto resubmit;
/* -------------------------------------------------------------------------*/
if (purb->status) {
	if ((-ESHUTDOWN == purb->status) || (-ENOENT == purb->status)) {
		JOM(16, "urb status -ESHUTDOWN or -ENOENT\n");
		return;
	}
	SAM("ERROR: non-zero urb status: -%s: %d\n",
		strerror(purb->status), purb->status);
	goto resubmit;
}
/*---------------------------------------------------------------------------*/
/*
 *  PROCEED HERE WHEN NO ERROR
 */
/*---------------------------------------------------------------------------*/

#if defined(UPSAMPLE)
oldaudio = peasycap->oldaudio;
#endif /*UPSAMPLE*/

for (i = 0;  i < purb->number_of_packets; i++) {
	if (purb->iso_frame_desc[i].status < 0) {
		SAM("-%s: %d\n",
			strerror(purb->iso_frame_desc[i].status),
			purb->iso_frame_desc[i].status);
	}
	if (!purb->iso_frame_desc[i].status) {
		more = purb->iso_frame_desc[i].actual_length;
		if (!more)
			peasycap->audio_mt++;
		else {
			if (peasycap->audio_mt) {
				JOM(12, "%4i empty audio urb frames\n",
							peasycap->audio_mt);
				peasycap->audio_mt = 0;
			}

			p1 = (__u8 *)(purb->transfer_buffer +
					purb->iso_frame_desc[i].offset);

/*---------------------------------------------------------------------------*/
/*
 *  COPY more BYTES FROM ISOC BUFFER TO THE DMA BUFFER,
 *  CONVERTING 8-BIT MONO TO 16-BIT SIGNED LITTLE-ENDIAN SAMPLES IF NECESSARY
 */
/*---------------------------------------------------------------------------*/
			while (more) {
				if (0 > more) {
					SAM("MISTAKE: more is negative\n");
					return;
				}
				much = dma_bytes - peasycap->dma_fill;
				if (0 > much) {
					SAM("MISTAKE: much is negative\n");
					return;
				}
				if (0 == much) {
					peasycap->dma_fill = 0;
					peasycap->dma_next = fragment_bytes;
					JOM(8, "wrapped dma buffer\n");
				}
				if (false == peasycap->microphone) {
					if (much > more)
						much = more;
					memcpy(prt->dma_area +
						peasycap->dma_fill,
								p1, much);
					p1 += much;
					more -= much;
				} else {
#if defined(UPSAMPLE)
					if (much % 16)
						JOM(8, "MISTAKE? much"
						" is not divisible by 16\n");
					if (much > (16 *
							more))
						much = 16 *
							more;
					p2 = (__u8 *)(prt->dma_area +
						peasycap->dma_fill);

					for (j = 0;  j < (much/16);  j++) {
						newaudio =  ((int) *p1) - 128;
						newaudio = 128 *
								newaudio;

						delta = (newaudio - oldaudio)
									/ 4;
						s16 = oldaudio + delta;

						for (k = 0;  k < 4;  k++) {
							*p2 = (0x00FF & s16);
							*(p2 + 1) = (0xFF00 &
								s16) >> 8;
							p2 += 2;
							*p2 = (0x00FF & s16);
							*(p2 + 1) = (0xFF00 &
								s16) >> 8;
							p2 += 2;
							s16 += delta;
						}
						p1++;
						more--;
						oldaudio = s16;
					}
#else /*!UPSAMPLE*/
					if (much > (2 * more))
						much = 2 * more;
					p2 = (__u8 *)(prt->dma_area +
						peasycap->dma_fill);

					for (j = 0;  j < (much / 2);  j++) {
						s16 =  ((int) *p1) - 128;
						s16 = 128 *
								s16;
						*p2 = (0x00FF & s16);
						*(p2 + 1) = (0xFF00 & s16) >>
									8;
						p1++;  p2 += 2;
						more--;
					}
#endif /*UPSAMPLE*/
				}
				peasycap->dma_fill += much;
				if (peasycap->dma_fill >= peasycap->dma_next) {
					isfragment = peasycap->dma_fill /
						fragment_bytes;
					if (0 > isfragment) {
						SAM("MISTAKE: isfragment is "
							"negative\n");
						return;
					}
					peasycap->dma_read = (isfragment
						- 1) * fragment_bytes;
					peasycap->dma_next = (isfragment
						+ 1) * fragment_bytes;
					if (dma_bytes < peasycap->dma_next) {
						peasycap->dma_next =
								fragment_bytes;
					}
					if (0 <= peasycap->dma_read) {
						JOM(8, "snd_pcm_period_elap"
							"sed(), %i="
							"isfragment\n",
							isfragment);
						snd_pcm_period_elapsed(pss);
					}
				}
			}
		}
	} else {
		JOM(12, "discarding audio samples because "
			"%i=purb->iso_frame_desc[i].status\n",
				purb->iso_frame_desc[i].status);
	}

#if defined(UPSAMPLE)
peasycap->oldaudio = oldaudio;
#endif /*UPSAMPLE*/

}
/*---------------------------------------------------------------------------*/
/*
 *  RESUBMIT THIS URB
 */
/*---------------------------------------------------------------------------*/
resubmit:
if (peasycap->audio_isoc_streaming) {
	rc = usb_submit_urb(purb, GFP_ATOMIC);
	if (rc) {
		if ((-ENODEV != rc) && (-ENOENT != rc)) {
			SAM("ERROR: while %i=audio_idle, "
				"usb_submit_urb() failed "
				"with rc: -%s :%d\n", peasycap->audio_idle,
				strerror(rc), rc);
		}
		if (0 < peasycap->audio_isoc_streaming)
			(peasycap->audio_isoc_streaming)--;
	}
}
return;
}
/*****************************************************************************/
static int easycap_alsa_open(struct snd_pcm_substream *pss)
{
struct snd_pcm *psnd_pcm;
struct snd_card *psnd_card;
struct easycap *peasycap;

JOT(4, "\n");
if (NULL == pss) {
	SAY("ERROR:  pss is NULL\n");
	return -EFAULT;
}
psnd_pcm = pss->pcm;
if (NULL == psnd_pcm) {
	SAY("ERROR:  psnd_pcm is NULL\n");
	return -EFAULT;
}
psnd_card = psnd_pcm->card;
if (NULL == psnd_card) {
	SAY("ERROR:  psnd_card is NULL\n");
	return -EFAULT;
}

peasycap = psnd_card->private_data;
if (NULL == peasycap) {
	SAY("ERROR:  peasycap is NULL\n");
	return -EFAULT;
}
if (memcmp(&peasycap->telltale[0], TELLTALE, strlen(TELLTALE))) {
	SAY("ERROR: bad peasycap\n");
	return -EFAULT;
}
if (peasycap->psnd_card != psnd_card) {
	SAM("ERROR: bad peasycap->psnd_card\n");
	return -EFAULT;
}
if (NULL != peasycap->psubstream) {
	SAM("ERROR: bad peasycap->psubstream\n");
	return -EFAULT;
}
pss->private_data = peasycap;
peasycap->psubstream = pss;
pss->runtime->hw = peasycap->alsa_hardware;
pss->runtime->private_data = peasycap;
pss->private_data = peasycap;

if (0 != easycap_sound_setup(peasycap)) {
	JOM(4, "ending unsuccessfully\n");
	return -EFAULT;
}
JOM(4, "ending successfully\n");
return 0;
}
/*****************************************************************************/
static int easycap_alsa_close(struct snd_pcm_substream *pss)
{
struct easycap *peasycap;

JOT(4, "\n");
if (NULL == pss) {
	SAY("ERROR:  pss is NULL\n");
	return -EFAULT;
}
peasycap = snd_pcm_substream_chip(pss);
if (NULL == peasycap) {
	SAY("ERROR:  peasycap is NULL\n");
	return -EFAULT;
}
if (memcmp(&peasycap->telltale[0], TELLTALE, strlen(TELLTALE))) {
	SAY("ERROR: bad peasycap\n");
	return -EFAULT;
}
pss->private_data = NULL;
peasycap->psubstream = NULL;
JOT(4, "ending successfully\n");
return 0;
}
/*****************************************************************************/
static int easycap_alsa_vmalloc(struct snd_pcm_substream *pss, size_t sz)
{
struct snd_pcm_runtime *prt;
JOT(4, "\n");

if (NULL == pss) {
	SAY("ERROR:  pss is NULL\n");
	return -EFAULT;
}
prt = pss->runtime;
if (NULL == prt) {
	SAY("ERROR: substream.runtime is NULL\n");
	return -EFAULT;
}
if (prt->dma_area) {
	if (prt->dma_bytes > sz)
		return 0;
	vfree(prt->dma_area);
}
prt->dma_area = vmalloc(sz);
if (NULL == prt->dma_area)
	return -ENOMEM;
prt->dma_bytes = sz;
return 0;
}
/*****************************************************************************/
static int easycap_alsa_hw_params(struct snd_pcm_substream *pss,
						struct snd_pcm_hw_params *phw)
{
int rc;

JOT(4, "%i\n", (params_buffer_bytes(phw)));
if (NULL == pss) {
	SAY("ERROR:  pss is NULL\n");
	return -EFAULT;
}
rc = easycap_alsa_vmalloc(pss, params_buffer_bytes(phw));
if (0 != rc)
	return rc;
return 0;
}
/*****************************************************************************/
static int easycap_alsa_hw_free(struct snd_pcm_substream *pss)
{
struct snd_pcm_runtime *prt;
JOT(4, "\n");

if (NULL == pss) {
	SAY("ERROR:  pss is NULL\n");
	return -EFAULT;
}
prt = pss->runtime;
if (NULL == prt) {
	SAY("ERROR: substream.runtime is NULL\n");
	return -EFAULT;
}
if (NULL != prt->dma_area) {
	JOT(8, "0x%08lX=prt->dma_area\n", (unsigned long int)prt->dma_area);
	vfree(prt->dma_area);
	prt->dma_area = NULL;
} else
	JOT(8, "dma_area already freed\n");
return 0;
}
/*****************************************************************************/
static int easycap_alsa_prepare(struct snd_pcm_substream *pss)
{
struct easycap *peasycap;
struct snd_pcm_runtime *prt;

JOT(4, "\n");
if (NULL == pss) {
	SAY("ERROR:  pss is NULL\n");
	return -EFAULT;
}
prt = pss->runtime;
peasycap = snd_pcm_substream_chip(pss);
if (NULL == peasycap) {
	SAY("ERROR:  peasycap is NULL\n");
	return -EFAULT;
}
if (memcmp(&peasycap->telltale[0], TELLTALE, strlen(TELLTALE))) {
	SAY("ERROR: bad peasycap\n");
	return -EFAULT;
}

JOM(16, "ALSA decides %8i Hz=rate\n", (int)pss->runtime->rate);
JOM(16, "ALSA decides %8i   =period_size\n", (int)pss->runtime->period_size);
JOM(16, "ALSA decides %8i   =periods\n", (int)pss->runtime->periods);
JOM(16, "ALSA decides %8i   =buffer_size\n", (int)pss->runtime->buffer_size);
JOM(16, "ALSA decides %8i   =dma_bytes\n", (int)pss->runtime->dma_bytes);
JOM(16, "ALSA decides %8i   =boundary\n", (int)pss->runtime->boundary);
JOM(16, "ALSA decides %8i   =period_step\n", (int)pss->runtime->period_step);
JOM(16, "ALSA decides %8i   =sample_bits\n", (int)pss->runtime->sample_bits);
JOM(16, "ALSA decides %8i   =frame_bits\n", (int)pss->runtime->frame_bits);
JOM(16, "ALSA decides %8i   =min_align\n", (int)pss->runtime->min_align);
JOM(12, "ALSA decides %8i   =hw_ptr_base\n", (int)pss->runtime->hw_ptr_base);
JOM(12, "ALSA decides %8i   =hw_ptr_interrupt\n",
					(int)pss->runtime->hw_ptr_interrupt);
if (prt->dma_bytes != 4 * ((int)prt->period_size) * ((int)prt->periods)) {
	SAY("MISTAKE:  unexpected ALSA parameters\n");
	return -ENOENT;
}
return 0;
}
/*****************************************************************************/
static int easycap_alsa_ack(struct snd_pcm_substream *pss)
{
	return 0;
}
/*****************************************************************************/
static int easycap_alsa_trigger(struct snd_pcm_substream *pss, int cmd)
{
struct easycap *peasycap;
int retval;

JOT(4, "%i=cmd cf %i=START %i=STOP\n", cmd, SNDRV_PCM_TRIGGER_START,
						SNDRV_PCM_TRIGGER_STOP);
if (NULL == pss) {
	SAY("ERROR:  pss is NULL\n");
	return -EFAULT;
}
peasycap = snd_pcm_substream_chip(pss);
if (NULL == peasycap) {
	SAY("ERROR:  peasycap is NULL\n");
	return -EFAULT;
}
if (memcmp(&peasycap->telltale[0], TELLTALE, strlen(TELLTALE))) {
	SAY("ERROR: bad peasycap\n");
	return -EFAULT;
}

switch (cmd) {
case SNDRV_PCM_TRIGGER_START: {
	peasycap->audio_idle = 0;
	break;
}
case SNDRV_PCM_TRIGGER_STOP: {
	peasycap->audio_idle = 1;
	break;
}
default:
	retval = -EINVAL;
}
return 0;
}
/*****************************************************************************/
static snd_pcm_uframes_t easycap_alsa_pointer(struct snd_pcm_substream *pss)
{
struct easycap *peasycap;
snd_pcm_uframes_t offset;

JOT(16, "\n");
if (NULL == pss) {
	SAY("ERROR:  pss is NULL\n");
	return -EFAULT;
}
peasycap = snd_pcm_substream_chip(pss);
if (NULL == peasycap) {
	SAY("ERROR:  peasycap is NULL\n");
	return -EFAULT;
}
if (memcmp(&peasycap->telltale[0], TELLTALE, strlen(TELLTALE))) {
	SAY("ERROR: bad peasycap\n");
	return -EFAULT;
}
if ((0 != peasycap->audio_eof) || (0 != peasycap->audio_idle)) {
	JOM(8, "returning -EIO because  "
			"%i=audio_idle  %i=audio_eof\n",
			peasycap->audio_idle, peasycap->audio_eof);
	return -EIO;
}
/*---------------------------------------------------------------------------*/
if (0 > peasycap->dma_read) {
	JOM(8, "returning -EBUSY\n");
	return -EBUSY;
}
offset = ((snd_pcm_uframes_t)peasycap->dma_read)/4;
JOM(8, "ALSA decides %8i   =hw_ptr_base\n", (int)pss->runtime->hw_ptr_base);
JOM(8, "ALSA decides %8i   =hw_ptr_interrupt\n",
					(int)pss->runtime->hw_ptr_interrupt);
JOM(8, "%7i=offset %7i=dma_read %7i=dma_next\n",
			(int)offset, peasycap->dma_read, peasycap->dma_next);
return offset;
}
/*****************************************************************************/
static struct page *easycap_alsa_page(struct snd_pcm_substream *pss,
				unsigned long offset)
{
	return vmalloc_to_page(pss->runtime->dma_area + offset);
}
/*****************************************************************************/

static struct snd_pcm_ops easycap_alsa_pcm_ops = {
	.open      = easycap_alsa_open,
	.close     = easycap_alsa_close,
	.ioctl     = snd_pcm_lib_ioctl,
	.hw_params = easycap_alsa_hw_params,
	.hw_free   = easycap_alsa_hw_free,
	.prepare   = easycap_alsa_prepare,
	.ack       = easycap_alsa_ack,
	.trigger   = easycap_alsa_trigger,
	.pointer   = easycap_alsa_pointer,
	.page      = easycap_alsa_page,
};

/*****************************************************************************/
/*---------------------------------------------------------------------------*/
/*
 *  THE FUNCTION snd_card_create() HAS  THIS_MODULE  AS AN ARGUMENT.  THIS
 *  MEANS MODULE easycap.  BEWARE.
*/
/*---------------------------------------------------------------------------*/
int easycap_alsa_probe(struct easycap *peasycap)
{
int rc;
struct snd_card *psnd_card;
struct snd_pcm *psnd_pcm;

if (NULL == peasycap) {
	SAY("ERROR: peasycap is NULL\n");
	return -ENODEV;
}
if (memcmp(&peasycap->telltale[0], TELLTALE, strlen(TELLTALE))) {
	SAY("ERROR: bad peasycap\n");
	return -EFAULT;
}
if (0 > peasycap->minor) {
	SAY("ERROR: no minor\n");
	return -ENODEV;
}

peasycap->alsa_hardware = alsa_hardware;
if (true == peasycap->microphone) {
	peasycap->alsa_hardware.rates = SNDRV_PCM_RATE_32000;
	peasycap->alsa_hardware.rate_min = 32000;
	peasycap->alsa_hardware.rate_max = 32000;
} else {
	peasycap->alsa_hardware.rates = SNDRV_PCM_RATE_48000;
	peasycap->alsa_hardware.rate_min = 48000;
	peasycap->alsa_hardware.rate_max = 48000;
}

	if (0 != snd_card_create(SNDRV_DEFAULT_IDX1, "easycap_alsa",
					THIS_MODULE, 0,
					&psnd_card)) {
		SAY("ERROR: Cannot do ALSA snd_card_create()\n");
		return -EFAULT;
	}

	sprintf(&psnd_card->id[0], "EasyALSA%i", peasycap->minor);
	strcpy(&psnd_card->driver[0], EASYCAP_DRIVER_DESCRIPTION);
	strcpy(&psnd_card->shortname[0], "easycap_alsa");
	sprintf(&psnd_card->longname[0], "%s", &psnd_card->shortname[0]);

	psnd_card->dev = &peasycap->pusb_device->dev;
	psnd_card->private_data = peasycap;
	peasycap->psnd_card = psnd_card;

	rc = snd_pcm_new(psnd_card, "easycap_pcm", 0, 0, 1, &psnd_pcm);
	if (0 != rc) {
		SAM("ERROR: Cannot do ALSA snd_pcm_new()\n");
		snd_card_free(psnd_card);
		return -EFAULT;
	}

	snd_pcm_set_ops(psnd_pcm, SNDRV_PCM_STREAM_CAPTURE,
							&easycap_alsa_pcm_ops);
	psnd_pcm->info_flags = 0;
	strcpy(&psnd_pcm->name[0], &psnd_card->id[0]);
	psnd_pcm->private_data = peasycap;
	peasycap->psnd_pcm = psnd_pcm;
	peasycap->psubstream = NULL;

	rc = snd_card_register(psnd_card);
	if (0 != rc) {
		SAM("ERROR: Cannot do ALSA snd_card_register()\n");
		snd_card_free(psnd_card);
		return -EFAULT;
	} else {
	;
	SAM("registered %s\n", &psnd_card->id[0]);
	}
return 0;
}
#endif /*! CONFIG_EASYCAP_OSS */

/*****************************************************************************/
/*****************************************************************************/
/*****************************************************************************/
/*****************************************************************************/
/*****************************************************************************/
/*****************************************************************************/
/*---------------------------------------------------------------------------*/
/*
 *  COMMON AUDIO INITIALIZATION
 */
/*---------------------------------------------------------------------------*/
int
easycap_sound_setup(struct easycap *peasycap)
{
int rc;

JOM(4, "starting initialization\n");

if (NULL == peasycap) {
	SAY("ERROR:  peasycap is NULL.\n");
	return -EFAULT;
}
if (NULL == peasycap->pusb_device) {
	SAM("ERROR: peasycap->pusb_device is NULL\n");
	return -ENODEV;
}
JOM(16, "0x%08lX=peasycap->pusb_device\n", (long int)peasycap->pusb_device);

rc = audio_setup(peasycap);
JOM(8, "audio_setup() returned %i\n", rc);

if (NULL == peasycap->pusb_device) {
	SAM("ERROR: peasycap->pusb_device has become NULL\n");
	return -ENODEV;
}
/*---------------------------------------------------------------------------*/
if (NULL == peasycap->pusb_device) {
	SAM("ERROR: peasycap->pusb_device has become NULL\n");
	return -ENODEV;
}
rc = usb_set_interface(peasycap->pusb_device, peasycap->audio_interface,
					peasycap->audio_altsetting_on);
JOM(8, "usb_set_interface(.,%i,%i) returned %i\n", peasycap->audio_interface,
					peasycap->audio_altsetting_on, rc);

rc = wakeup_device(peasycap->pusb_device);
JOM(8, "wakeup_device() returned %i\n", rc);

peasycap->audio_eof = 0;
peasycap->audio_idle = 0;

peasycap->timeval1.tv_sec  = 0;
peasycap->timeval1.tv_usec = 0;

submit_audio_urbs(peasycap);

JOM(4, "finished initialization\n");
return 0;
}
/*****************************************************************************/
/*---------------------------------------------------------------------------*/
/*
 *  SUBMIT ALL AUDIO URBS.
 */
/*---------------------------------------------------------------------------*/
int
submit_audio_urbs(struct easycap *peasycap)
{
struct data_urb *pdata_urb;
struct urb *purb;
struct list_head *plist_head;
int j, isbad, nospc, m, rc;
int isbuf;

if (NULL == peasycap) {
	SAY("ERROR: peasycap is NULL\n");
	return -EFAULT;
}
if (NULL == peasycap->purb_audio_head) {
	SAM("ERROR: peasycap->urb_audio_head uninitialized\n");
	return -EFAULT;
}
if (NULL == peasycap->pusb_device) {
	SAM("ERROR: peasycap->pusb_device is NULL\n");
	return -EFAULT;
}
if (!peasycap->audio_isoc_streaming) {
	JOM(4, "initial submission of all audio urbs\n");
	rc = usb_set_interface(peasycap->pusb_device,
					peasycap->audio_interface,
					peasycap->audio_altsetting_on);
	JOM(8, "usb_set_interface(.,%i,%i) returned %i\n",
					peasycap->audio_interface,
					peasycap->audio_altsetting_on, rc);

	isbad = 0;  nospc = 0;  m = 0;
	list_for_each(plist_head, (peasycap->purb_audio_head)) {
		pdata_urb = list_entry(plist_head, struct data_urb, list_head);
		if (NULL != pdata_urb) {
			purb = pdata_urb->purb;
			if (NULL != purb) {
				isbuf = pdata_urb->isbuf;

				purb->interval = 1;
				purb->dev = peasycap->pusb_device;
				purb->pipe =
					usb_rcvisocpipe(peasycap->pusb_device,
					peasycap->audio_endpointnumber);
				purb->transfer_flags = URB_ISO_ASAP;
				purb->transfer_buffer =
					peasycap->audio_isoc_buffer[isbuf].pgo;
				purb->transfer_buffer_length =
					peasycap->audio_isoc_buffer_size;
#ifdef CONFIG_EASYCAP_OSS
				purb->complete = easyoss_complete;
#else /* CONFIG_EASYCAP_OSS */
				purb->complete = easycap_alsa_complete;
#endif /* CONFIG_EASYCAP_OSS */
				purb->context = peasycap;
				purb->start_frame = 0;
				purb->number_of_packets =
					peasycap->audio_isoc_framesperdesc;
				for (j = 0;  j < peasycap->
						audio_isoc_framesperdesc;
									j++) {
					purb->iso_frame_desc[j].offset = j *
						peasycap->
						audio_isoc_maxframesize;
					purb->iso_frame_desc[j].length =
						peasycap->
						audio_isoc_maxframesize;
				}

				rc = usb_submit_urb(purb, GFP_KERNEL);
				if (rc) {
					isbad++;
					SAM("ERROR: usb_submit_urb() failed"
						" for urb with rc: -%s: %d\n",
						strerror(rc), rc);
				} else {
					 m++;
				}
			} else {
				isbad++;
			}
		} else {
			isbad++;
		}
	}
	if (nospc) {
		SAM("-ENOSPC=usb_submit_urb() for %i urbs\n", nospc);
		SAM(".....  possibly inadequate USB bandwidth\n");
		peasycap->audio_eof = 1;
	}
	if (isbad) {
		JOM(4, "attempting cleanup instead of submitting\n");
		list_for_each(plist_head, (peasycap->purb_audio_head)) {
			pdata_urb = list_entry(plist_head, struct data_urb,
								list_head);
			if (NULL != pdata_urb) {
				purb = pdata_urb->purb;
				if (NULL != purb)
					usb_kill_urb(purb);
			}
		}
		peasycap->audio_isoc_streaming = 0;
	} else {
		peasycap->audio_isoc_streaming = m;
		JOM(4, "submitted %i audio urbs\n", m);
	}
} else
	JOM(4, "already streaming audio urbs\n");

return 0;
}
/*****************************************************************************/
/*---------------------------------------------------------------------------*/
/*
 *  KILL ALL AUDIO URBS.
 */
/*---------------------------------------------------------------------------*/
int
kill_audio_urbs(struct easycap *peasycap)
{
int m;
struct list_head *plist_head;
struct data_urb *pdata_urb;

if (NULL == peasycap) {
	SAY("ERROR: peasycap is NULL\n");
	return -EFAULT;
}
if (peasycap->audio_isoc_streaming) {
	if (NULL != peasycap->purb_audio_head) {
		peasycap->audio_isoc_streaming = 0;
		JOM(4, "killing audio urbs\n");
		m = 0;
		list_for_each(plist_head, (peasycap->purb_audio_head)) {
			pdata_urb = list_entry(plist_head, struct data_urb,
								list_head);
			if (NULL != pdata_urb) {
				if (NULL != pdata_urb->purb) {
					usb_kill_urb(pdata_urb->purb);
					m++;
				}
			}
		}
		JOM(4, "%i audio urbs killed\n", m);
	} else {
		SAM("ERROR: peasycap->purb_audio_head is NULL\n");
		return -EFAULT;
	}
} else {
	JOM(8, "%i=audio_isoc_streaming, no audio urbs killed\n",
					peasycap->audio_isoc_streaming);
}
return 0;
}
/*****************************************************************************/
