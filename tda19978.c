#include <linux/delay.h>
#include <linux/hdmi.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/types.h>
#include <linux/v4l2-dv-timings.h>
#include <linux/videodev2.h>

#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-dv-timings.h>
#include <media/v4l2-event.h>
#include <media/v4l2-fwnode.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>

#include <media/v4l2-subdev.h>
#include <media/v4l2-mediabus.h>

#include "tda19978_regs.h"

/* debug level */
static int debug;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "debug level (0-2)");

/* Video input formats */
static const char * const hdmi_colorspace_names[] = {
	"RGB", "YUV422", "YUV444", "YUV420", "", "", "", "",
};
static const char * const hdmi_colorimetry_names[] = {
	"", "ITU601", "ITU709", "Extended",
};
static const char * const v4l2_quantization_names[] = {
	"Default",
	"Full Range (0-255)",
	"Limited Range (16-235)",
};

/* Video output port formats */
static const char * const vidfmt_names[] = {
	"RGB444/YUV444",	/* RGB/YUV444 16bit data bus, 8bpp */
	"YUV422 semi-planar",	/* YUV422 16bit data base, 8bpp */
	"YUV422 CCIR656",	/* BT656 (YUV 8bpp 2 clock per pixel) */
	"Invalid",
};

/*
 * Colorspace conversion matrices
 */
struct color_matrix_coefs {
	const char *name;
	/* Input offsets */
	s16 offint1;
	s16 offint2;
	s16 offint3;
	/* Coeficients */
	s16 p11coef;
	s16 p12coef;
	s16 p13coef;
	s16 p21coef;
	s16 p22coef;
	s16 p23coef;
	s16 p31coef;
	s16 p32coef;
	s16 p33coef;
	/* Output offsets */
	s16 offout1;
	s16 offout2;
	s16 offout3;
};

enum {
	ITU709_RGBFULL,
	ITU601_RGBFULL,
	RGBLIMITED_RGBFULL,
	RGBLIMITED_ITU601,
	RGBLIMITED_ITU709,
	RGBFULL_ITU601,
	RGBFULL_ITU709,
};

/* NB: 4096 is 1.0 using fixed point numbers */
static const struct color_matrix_coefs conv_matrix[] = {
	{
		"YUV709 -> RGB full",
		 -256, -2048,  -2048,
		 4769, -2183,   -873,
		 4769,  7343,      0,
		 4769,     0,   8652,
		    0,     0,      0,
	},
	{
		"YUV601 -> RGB full",
		 -256, -2048,  -2048,
		 4769, -3330,  -1602,
		 4769,  6538,      0,
		 4769,     0,   8264,
		  256,   256,    256,
	},
	{
		"RGB limited -> RGB full",
		 -256,  -256,   -256,
		    0,  4769,      0,
		    0,     0,   4769,
		 4769,     0,      0,
		    0,     0,      0,
	},
	{
		"RGB limited -> ITU601",
		 -256,  -256,   -256,
		 2404,  1225,    467,
		-1754,  2095,   -341,
		-1388,  -707,   2095,
		  256,  2048,   2048,
	},
	{
		"RGB limited -> ITU709",
		 -256,  -256,   -256,
		 2918,   867,    295,
		-1894,  2087,   -190,
		-1607,  -477,   2087,
		  256,  2048,   2048,
	},
	{
		"RGB full -> ITU601",
		    0,     0,      0,
		 2065,  1052,    401,
		-1506,  1799,   -293,
		-1192,  -607,   1799,
		  256,  2048,   2048,
	},
	{
		"RGB full -> ITU709",
		    0,     0,      0,
		 2506,   745,    253,
		-1627,  1792,   -163,
		-1380,  -410,   1792,
		  256,  2048,   2048,
	},
};

enum tda19978_type {
	TDA19977,   // 3 Inputs
	TDA19978,   // 4 Inputs
};

enum tda19978_hdmi_pads {
	TDA19978_PAD_SOURCE,
	TDA19978_NUM_PADS,
};

struct tda19978_chip_info {
	enum tda19978_type type;
	const char *name;
};

struct tda19978_state {
	const struct tda19978_chip_info *info;
	//struct tda19978_platform_data pdata;
	struct i2c_client *client;
	struct i2c_client *client_cec;
	struct v4l2_subdev sd;
	//struct regulator_bulk_data supplies[TDA19978_NUM_SUPPLIES];
	struct media_pad pads[TDA19978_NUM_PADS];
	struct mutex lock;
	struct mutex page_lock;
	char page;

	/* detected info from chip */
	int chip_revision;

	/* status info */
	u8 hdmi_status;
	u8 mptrw_in_progress;
	u8 activity_status;
	u8 input_detect[4];
	u8 current_port;

	/* video */
	struct hdmi_avi_infoframe avi_infoframe;
	struct v4l2_hdmi_colorimetry colorimetry;
	u32 rgb_quantization_range;
	struct v4l2_dv_timings timings;
	int fps;
	const struct color_matrix_coefs *conv;
	//u32 mbus_codes[1];	/* available modes */
	u32 mbus_code;		/* current mode */
	u8 vid_fmt;

	/* controls */
	struct i2c_client *client_hpd;	// PCA9536
	struct v4l2_ctrl_handler hdl;
	struct v4l2_ctrl *rgb_quantization_range_ctrl;

	/* audio */
	u8  audio_ch_alloc;
	int audio_samplerate;
	int audio_channels;
	int audio_samplesize;
	int audio_type;
	struct mutex audio_lock;
	struct snd_pcm_substream *audio_stream;

	/* EDID */
	struct {
		u8 edid[256];
		u32 present;
		unsigned int blocks;
	} edid;
	struct delayed_work delayed_work_enable_hpd;
};

static const struct tda19978_chip_info tda19978_chip_info[] = {
	[TDA19977] = {
		.type = TDA19977,
		.name = "tda19977",
	},
	[TDA19978] = {
		.type = TDA19978,
		.name = "tda19978",
	},
};

static const struct v4l2_dv_timings_cap tda19978_dv_timings_cap = {
	.type = V4L2_DV_BT_656_1120,
	/* keep this initialization for compatibility with GCC < 4.4.6 */
	.reserved = { 0 },

	V4L2_INIT_BT_TIMINGS(
		640, 1920,			/* min/max width */
		350, 1200,			/* min/max height */
		13000000, 165000000,		/* min/max pixelclock */
		/* standards */
		V4L2_DV_BT_STD_CEA861 | V4L2_DV_BT_STD_DMT |
			V4L2_DV_BT_STD_GTF | V4L2_DV_BT_STD_CVT,
		/* capabilities */
		V4L2_DV_BT_CAP_INTERLACED | V4L2_DV_BT_CAP_PROGRESSIVE |
			V4L2_DV_BT_CAP_REDUCED_BLANKING |
			V4L2_DV_BT_CAP_CUSTOM
	)
};

static inline struct tda19978_state *to_state(struct v4l2_subdev *sd)
{
	return container_of(sd, struct tda19978_state, sd);
}

static inline struct v4l2_subdev *to_sd(struct v4l2_ctrl *ctrl)
{
	return &container_of(ctrl->handler, struct tda19978_state, hdl)->sd;
}


/* -----------------------------------------------------------------------------
 * I2C transfer
 */

static int tda19978_setpage(struct v4l2_subdev *sd, u8 page)
{
	struct tda19978_state *state = to_state(sd);
	int ret;

	if (state->page != page) {
		ret = i2c_smbus_write_byte_data(state->client,
			REG_CURPAGE_00H, page);
		if (ret < 0) {
			v4l_err(state->client,
				"write reg error:reg=%2x,val=%2x\n",
				REG_CURPAGE_00H, page);
			return ret;
		}
		state->page = page;
	}
	return 0;
}

static inline int io_read(struct v4l2_subdev *sd, u16 reg)
{
	struct tda19978_state *state = to_state(sd);
	int val;

	mutex_lock(&state->page_lock);
	if (tda19978_setpage(sd, reg >> 8)) {
		val = -1;
		goto out;
	}

	val = i2c_smbus_read_byte_data(state->client, reg&0xff);
	if (val < 0) {
		v4l_err(state->client, "read reg error: reg=%2x\n", reg & 0xff);
		val = -1;
		goto out;
	}

out:
	mutex_unlock(&state->page_lock);
	return val;
}

static inline long io_read16(struct v4l2_subdev *sd, u16 reg)
{
	int val;
	long lval = 0;

	val = io_read(sd, reg);
	if (val < 0)
		return val;
	lval |= (val << 8);
	val = io_read(sd, reg + 1);
	if (val < 0)
		return val;
	lval |= val;

	return lval;
}

static inline long io_read24(struct v4l2_subdev *sd, u16 reg)
{
	int val;
	long lval = 0;

	val = io_read(sd, reg);
	if (val < 0)
		return val;
	lval |= (val << 16);
	val = io_read(sd, reg + 1);
	if (val < 0)
		return val;
	lval |= (val << 8);
	val = io_read(sd, reg + 2);
	if (val < 0)
		return val;
	lval |= val;

	return lval;
}

static unsigned int io_readn(struct v4l2_subdev *sd, u16 reg, u8 len, u8 *data)
{
	int i;
	int sz = 0;
	int val;

	for (i = 0; i < len; i++) {
		val = io_read(sd, reg + i);
		if (val < 0)
			break;
		data[i] = val;
		sz++;
	}

	return sz;
}

static int io_write(struct v4l2_subdev *sd, u16 reg, u8 val)
{
	struct tda19978_state *state = to_state(sd);
	s32 ret = 0;

	mutex_lock(&state->page_lock);
	if (tda19978_setpage(sd, reg >> 8)) {
		ret = -1;
		goto out;
	}

	ret = i2c_smbus_write_byte_data(state->client, reg & 0xff, val);
	if (ret < 0) {
		v4l_err(state->client, "write reg error:reg=%2x,val=%2x\n",
			reg&0xff, val);
		ret = -1;
		goto out;
	}

out:
	mutex_unlock(&state->page_lock);
	return ret;
}

static int io_write16(struct v4l2_subdev *sd, u16 reg, u16 val)
{
	int ret;

	ret = io_write(sd, reg, (val >> 8) & 0xff);
	if (ret < 0)
		return ret;
	ret = io_write(sd, reg + 1, val & 0xff);
	if (ret < 0)
		return ret;
	return 0;
}

static int io_write24(struct v4l2_subdev *sd, u16 reg, u32 val)
{
	int ret;

	ret = io_write(sd, reg, (val >> 16) & 0xff);
	if (ret < 0)
		return ret;
	ret = io_write(sd, reg + 1, (val >> 8) & 0xff);
	if (ret < 0)
		return ret;
	ret = io_write(sd, reg + 2, val & 0xff);
	if (ret < 0)
		return ret;
	return 0;
}

static int io_writen(struct v4l2_subdev *sd, u16 reg, u8 len, u8 *data)
{
	int i, ret;

	for (i = 0; i < len; i++) {
		ret = io_write(sd, reg + i, data[i]);
		if (ret < 0)
			return ret;
	}
	return 0;
}

/* -----------------------------------------------------------------------------
* Hotplug
*/
static void tda19978_delayed_work_enable_hpd(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct tda19978_state *state = container_of(dwork,
						    struct tda19978_state,
						    delayed_work_enable_hpd);
	struct v4l2_subdev *sd = &state->sd;

	v4l2_dbg(2, debug, sd, "%s\n", __func__);

	/* Set HPD high */
	i2c_smbus_write_byte_data(state->client_hpd, 0x01, 0x0f);
	state->edid.present = 1;
}

static void tda19978_disable_edid(struct v4l2_subdev *sd)
{
	struct tda19978_state *state = to_state(sd);

	v4l2_dbg(1, debug, sd, "%s\n", __func__);
	cancel_delayed_work_sync(&state->delayed_work_enable_hpd);
	/* Set HPD low */
	i2c_smbus_write_byte_data(state->client_hpd, 0x01, 0x00);
}

static void tda19978_enable_edid(struct v4l2_subdev *sd)
{
	struct tda19978_state *state = to_state(sd);
	v4l2_dbg(1, debug, sd, "%s\n", __func__);

	/* Assert HPD high after 100ms */
	schedule_delayed_work(&state->delayed_work_enable_hpd, HZ / 10);
}

/* -----------------------------------------------------------------------------
 * Signal Control
 */
/*
 * configure vid_fmt based on mbus_code
 */
static int
tda19978_setup_format(struct tda19978_state *state, u32 code)
{
	v4l_dbg(1, debug, state->client, "%s code=0x%x\n", __func__, code);
	switch (code) {
	case MEDIA_BUS_FMT_UYVY8_1X16:
		state->vid_fmt = OF_FMT_422_SMPT;
		break;
	default:
		v4l_err(state->client, "incompatible format (0x%x)\n", code);
		return -EINVAL;
	}
	v4l_dbg(1, debug, state->client, "%s code=0x%x fmt=%s\n", __func__,
		code, vidfmt_names[state->vid_fmt]);
	state->mbus_code = code;

	return 0;
}

/*
* The color conversion matrix will convert between the colorimetry of the
* HDMI input to the desired output format RGB|YUV. RGB output is to be
* full-range and YUV is to be limited range.
*
* RGB full-range uses values from 0 to 255 which is recommended on a monitor
* and RGB Limited uses values from 16 to 236 (16=black, 235=white) which is
* typically recommended on a TV.
*/
static void
tda19978_configure_csc(struct v4l2_subdev *sd)
{
	struct tda19978_state *state = to_state(sd);
	struct hdmi_avi_infoframe *avi = &state->avi_infoframe;
	struct v4l2_hdmi_colorimetry *c = &state->colorimetry;
	/* Blanking code values depend on output colorspace (RGB or YUV) */
	struct blanking_codes {
		s16 code_gy;
		s16 code_bu;
		s16 code_rv;
	};
	//static const struct blanking_codes rgb_blanking = { 64, 64, 64 };
	//static const struct blanking_codes yuv_blanking = { 64, 512, 512 };
	const struct blanking_codes *blanking_codes = NULL;
	u8 reg;
	v4l_dbg(1, debug, state->client, "input:%s quant:%s output:%s\n",
		hdmi_colorspace_names[avi->colorspace],
		v4l2_quantization_names[c->quantization],
		vidfmt_names[state->vid_fmt]);
	
	state->conv = NULL;
	switch (state->vid_fmt) {
	/* To get correct color, we use ITU601 matrix for HD input */
	case OF_FMT_422_SMPT: /* semi-planar */
		//blanking_codes = &yuv_blanking;
		if ((c->colorspace == V4L2_COLORSPACE_SRGB) &&
		    (c->quantization == V4L2_QUANTIZATION_FULL_RANGE)) {
			if (state->timings.bt.height <= 576)
				state->conv = &conv_matrix[RGBFULL_ITU601];
			else
				state->conv = &conv_matrix[RGBFULL_ITU601];
		} else if ((c->colorspace == V4L2_COLORSPACE_SRGB) &&
			   (c->quantization == V4L2_QUANTIZATION_LIM_RANGE)) {
			if (state->timings.bt.height <= 576)
				state->conv = &conv_matrix[RGBLIMITED_ITU601];
			else
				state->conv = &conv_matrix[RGBLIMITED_ITU601];
		}
		break;
	}

	if (state->conv) {
		v4l_dbg(1, debug, state->client, "%s\n",
			state->conv->name);
		/* enable matrix conversion */
		reg = io_read(sd, REG_VDP_CTRL);
		reg &= ~VDP_CTRL_MATRIX_BP;
		io_write(sd, REG_VDP_CTRL, reg);
		/* offset inputs */
		io_write16(sd, REG_VDP_MATRIX + 0, state->conv->offint1);
		io_write16(sd, REG_VDP_MATRIX + 2, state->conv->offint2);
		io_write16(sd, REG_VDP_MATRIX + 4, state->conv->offint3);
		/* coefficients */
		io_write16(sd, REG_VDP_MATRIX + 6, state->conv->p11coef);
		io_write16(sd, REG_VDP_MATRIX + 8, state->conv->p12coef);
		io_write16(sd, REG_VDP_MATRIX + 10, state->conv->p13coef);
		io_write16(sd, REG_VDP_MATRIX + 12, state->conv->p21coef);
		io_write16(sd, REG_VDP_MATRIX + 14, state->conv->p22coef);
		io_write16(sd, REG_VDP_MATRIX + 16, state->conv->p23coef);
		io_write16(sd, REG_VDP_MATRIX + 18, state->conv->p31coef);
		io_write16(sd, REG_VDP_MATRIX + 20, state->conv->p32coef);
		io_write16(sd, REG_VDP_MATRIX + 22, state->conv->p33coef);
		/* offset outputs */
		io_write16(sd, REG_VDP_MATRIX + 24, state->conv->offout1);
		io_write16(sd, REG_VDP_MATRIX + 26, state->conv->offout2);
		io_write16(sd, REG_VDP_MATRIX + 28, state->conv->offout3);
	} else {
		/* disable matrix conversion */
		reg = io_read(sd, REG_VDP_CTRL);
		reg |= VDP_CTRL_MATRIX_BP;
		io_write(sd, REG_VDP_CTRL, reg);
	}

	/* SetBlankingCodes */
	if (blanking_codes) {
		/*
		io_write16(sd, REG_BLK_GY, blanking_codes->code_gy);
		io_write16(sd, REG_BLK_BU, blanking_codes->code_bu);
		io_write16(sd, REG_BLK_RV, blanking_codes->code_rv);
		*/
	}
}

/* Configure frame detection window and VHREF timing generator */
static void
tda19978_configure_vhref(struct v4l2_subdev *sd)
{
	struct tda19978_state *state = to_state(sd);
	const struct v4l2_bt_timings *bt = &state->timings.bt;
	int width, lines;
	u16 href_start, href_end;
	u16 vref_f1_start, vref_f2_start;
	u8 vref_f1_width, vref_f2_width;
	u8 field_polarity;
	u16 fieldref_f1_start, fieldref_f2_start;
	u8 reg;

	href_start = bt->hbackporch + bt->hsync + 1;
	href_end = href_start + bt->width;
	vref_f1_start = bt->height + bt->vbackporch + bt->vsync +
			bt->il_vbackporch + bt->il_vsync +
			bt->il_vfrontporch;
	vref_f1_width = bt->vbackporch + bt->vsync + bt->vfrontporch;
	vref_f2_start = 0;
	vref_f2_width = 0;
	fieldref_f1_start = 0;
	fieldref_f2_start = 0;
	if (bt->interlaced) {
		vref_f2_start = (bt->height / 2) +
				(bt->il_vbackporch + bt->il_vsync - 1);
		vref_f2_width = bt->il_vbackporch + bt->il_vsync +
				bt->il_vfrontporch;
		fieldref_f2_start = vref_f2_start + bt->il_vfrontporch +
				    fieldref_f1_start;
	}
	field_polarity = 1;

	width = V4L2_DV_BT_FRAME_WIDTH(bt);
	lines = V4L2_DV_BT_FRAME_HEIGHT(bt);

	/*
	 * Configure Frame Detection Window:
	 *  horiz area where the VHREF module consider a VSYNC a new frame
	 */
	io_write(sd, REG_FDW_S, 0x2ef & 0xff);
	io_write(sd, REG_FDW_S+1, ((0x2ef & 0xf00) >> 4) | ((0x141 & 0xf00) >> 8));
	io_write(sd, REG_FDW_E, 0x141 & 0xff);

	/* Set Pixel And Line Counters */
	if (state->chip_revision == 0)
		io_write(sd, REG_PXCNT_PR, 4);
	else
		io_write(sd, REG_PXCNT_PR, 1);
	io_write16(sd, REG_PXCNT_NPIX, width & MASK_VHREF);
	io_write(sd, REG_LCNT_PR, 1);
	io_write16(sd, REG_LCNT_NLIN, lines & MASK_VHREF);

	/*
	 * Configure the VHRef timing generator responsible for rebuilding all
	 * horiz and vert synch and ref signals from its input allowing auto
	 * detection algorithms and forcing predefined modes (480i & 576i)
	 */
	reg = VHREF_STD_DET_OFF << VHREF_STD_DET_SHIFT;
	io_write(sd, REG_VHREF_CTRL, reg);

	/*
	 * Configure the VHRef timing values. In case the VHREF generator has
	 * been configured in manual mode, this will allow to manually set all
	 * horiz and vert ref values (non-active pixel areas) of the generator
	 * and allows setting the frame reference params.
	 */
	/* horizontal reference start/end */
	reg = ((href_start & MASK_VHREF) & 0xf00) >> 4;
	reg |= ((href_end & MASK_VHREF) & 0xf00) >> 8;
	io_write(sd, REG_HREF_S, (href_start & MASK_VHREF) & 0xff);
	io_write(sd, REG_HREF_S+1, reg);
	io_write(sd, REG_HREF_E, (href_end & MASK_VHREF) & 0xff);
	/* vertical reference f1 start/end */
	io_write16(sd, REG_VREF_F1_S, vref_f1_start & MASK_VHREF);
	io_write(sd, REG_VREF_F1_WIDTH, vref_f1_width);
	/* vertical reference f2 start/end */
	io_write16(sd, REG_VREF_F2_S, vref_f2_start & MASK_VHREF);
	io_write(sd, REG_VREF_F2_WIDTH, vref_f2_width);

	/* F1/F2 FREF, field polarity */
	reg = field_polarity;
	io_write(sd, REG_FREF_F1_S, reg);
	io_write16(sd, REG_FREF_F2_S, fieldref_f2_start & MASK_VHREF);
}

static int tda19978_check_port(struct v4l2_subdev *sd) {
	struct tda19978_state *state = to_state(sd);
	char reg;

	/* HDMI port status */
	state->input_detect[0] = io_read(sd, 0x13CB);
	state->input_detect[1] = io_read(sd, 0x13D0);
	state->input_detect[2] = io_read(sd, 0x13D5);
	state->input_detect[3] = io_read(sd, 0x13DA);
	
	/*
	Register 0x13F0 seems Link status
		[7:5]: 0x011 when signal is good
		[4:3]: Port #
		[2:0]: 0x101 when signal is good
	*/
	state->current_port = io_read(sd, 0x13F0);
	reg = state->current_port & 0xE7;
	if (reg != 0x65) {
		return 0;
	}

	//reg = io_read(sd, 0x1120);
	//reg = io_read(sd, 0x1129);
	return 1;
}

static int
tda19978_detect_std(struct tda19978_state *state,
		    struct v4l2_dv_timings *timings)
{
	struct v4l2_subdev *sd = &state->sd;
	u32 vper;
	u16 hper;
	u16 hsper;
	int i;

	/*
	 * Read the FMT registers
	 *   REG_V_PER: Period of a frame (or two fields) in MCLK(27MHz) cycles
	 *   REG_H_PER: Period of a line in MCLK(27MHz) cycles
	 *   REG_HS_WIDTH: Period of horiz sync pulse in MCLK(27MHz) cycles
	 */
	vper = io_read24(sd, REG_V_PER) & MASK_VPER;
	hper = io_read16(sd, REG_H_PER) & MASK_HPER;
	hsper = io_read16(sd, REG_HS_WIDTH) & MASK_HSWIDTH;
	v4l2_dbg(1, debug, sd, "Signal Timings: %u/%u/%u\n", vper, hper, hsper);

	if (!tda19978_check_port(sd))
		return -ENOLINK;
	
	for (i = 0; v4l2_dv_timings_presets[i].bt.width; i++) {
		const struct v4l2_bt_timings *bt;
		u32 lines, width, _hper, _hsper;
		u32 vmin, vmax, hmin, hmax, hsmin, hsmax;
		bool vmatch, hmatch, hsmatch;

		bt = &v4l2_dv_timings_presets[i].bt;
		width = V4L2_DV_BT_FRAME_WIDTH(bt);
		lines = V4L2_DV_BT_FRAME_HEIGHT(bt);
		_hper = (u32)bt->pixelclock / width;
		if (bt->interlaced)
			lines /= 2;
		/* vper +/- 0.7% */
		vmin = ((27000000 / 1000) * 993) / _hper * lines;
		vmax = ((27000000 / 1000) * 1008) / _hper * lines;
		/* hper +/- 1.0% */
		hmin = ((27000000 / 100) * 99) / _hper;
		hmax = ((27000000 / 100) * 101) / _hper;
		/* hsper +/- 2 (take care to avoid 32bit overflow) */
		_hsper = 27000 * bt->hsync / ((u32)bt->pixelclock/1000);
		hsmin = _hsper - 2;
		hsmax = _hsper + 2;

		/* vmatch matches the framerate */
		vmatch = ((vper <= vmax) && (vper >= vmin)) ? 1 : 0;
		/* hmatch matches the width */
		hmatch = ((hper <= hmax) && (hper >= hmin)) ? 1 : 0;
		/* hsmatch matches the hswidth */
		hsmatch = ((hsper <= hsmax) && (hsper >= hsmin)) ? 1 : 0;
		if (hmatch && vmatch && hsmatch) {
			v4l2_print_dv_timings(sd->name, "Detected format: ",
					      &v4l2_dv_timings_presets[i],
					      debug);
			if (timings)
				*timings = v4l2_dv_timings_presets[i];
			return 0;
		}
	}

	v4l_err(state->client, "no resolution match for timings: %d/%d/%d\n",
		vper, hper, hsper);
	return -ERANGE;
}

static void
set_rgb_quantization_range(struct tda19978_state *state)
{
	struct v4l2_hdmi_colorimetry *c = &state->colorimetry;

	state->colorimetry = v4l2_hdmi_rx_colorimetry(&state->avi_infoframe,
						      NULL,
						      state->timings.bt.height);
	/* If ycbcr_enc is V4L2_YCBCR_ENC_DEFAULT, we receive RGB */
	if (c->ycbcr_enc == V4L2_YCBCR_ENC_DEFAULT) {
		switch (state->rgb_quantization_range) {
		case V4L2_DV_RGB_RANGE_LIMITED:
			c->quantization = V4L2_QUANTIZATION_FULL_RANGE;
			break;
		case V4L2_DV_RGB_RANGE_FULL:
			c->quantization = V4L2_QUANTIZATION_LIM_RANGE;
			break;
		}
	}
	v4l_dbg(1, debug, state->client,
		"colorspace=%d/%d colorimetry=%d range=%s content=%d\n",
		state->avi_infoframe.colorspace, c->colorspace,
		state->avi_infoframe.colorimetry,
		v4l2_quantization_names[c->quantization],
		state->avi_infoframe.content_type);
}

/* parse an infoframe and do some sanity checks on it */
static unsigned int
tda19978_parse_infoframe(struct tda19978_state *state, u16 addr)
{
	struct v4l2_subdev *sd = &state->sd;
	union hdmi_infoframe frame;
	u8 buffer[40] = { 0 };
	u8 reg;
	int len, err;

	/* read data */
	len = io_readn(sd, addr, sizeof(buffer), buffer);
	err = hdmi_infoframe_unpack(&frame, buffer, len);
	if (err) {
		v4l_err(state->client,
			"failed parsing %d byte infoframe: 0x%04x/0x%02x\n",
			len, addr, buffer[0]);
		return err;
	}
	hdmi_infoframe_log(KERN_INFO, &state->client->dev, &frame);
	switch (frame.any.type) {
	/* Audio InfoFrame: see HDMI spec 8.2.2 */
	case HDMI_INFOFRAME_TYPE_AUDIO:
		/* sample rate */
		switch (frame.audio.sample_frequency) {
		case HDMI_AUDIO_SAMPLE_FREQUENCY_32000:
			state->audio_samplerate = 32000;
			break;
		case HDMI_AUDIO_SAMPLE_FREQUENCY_44100:
			state->audio_samplerate = 44100;
			break;
		case HDMI_AUDIO_SAMPLE_FREQUENCY_48000:
			state->audio_samplerate = 48000;
			break;
		case HDMI_AUDIO_SAMPLE_FREQUENCY_88200:
			state->audio_samplerate = 88200;
			break;
		case HDMI_AUDIO_SAMPLE_FREQUENCY_96000:
			state->audio_samplerate = 96000;
			break;
		case HDMI_AUDIO_SAMPLE_FREQUENCY_176400:
			state->audio_samplerate = 176400;
			break;
		case HDMI_AUDIO_SAMPLE_FREQUENCY_192000:
			state->audio_samplerate = 192000;
			break;
		default:
		case HDMI_AUDIO_SAMPLE_FREQUENCY_STREAM:
			break;
		}

		/* sample size */
		switch (frame.audio.sample_size) {
		case HDMI_AUDIO_SAMPLE_SIZE_16:
			state->audio_samplesize = 16;
			break;
		case HDMI_AUDIO_SAMPLE_SIZE_20:
			state->audio_samplesize = 20;
			break;
		case HDMI_AUDIO_SAMPLE_SIZE_24:
			state->audio_samplesize = 24;
			break;
		case HDMI_AUDIO_SAMPLE_SIZE_STREAM:
		default:
			break;
		}

		/* Channel Count */
		state->audio_channels = frame.audio.channels;
		if (frame.audio.channel_allocation &&
		    frame.audio.channel_allocation != state->audio_ch_alloc) {
			/* use the channel assignment from the infoframe */
			state->audio_ch_alloc = frame.audio.channel_allocation;
			//tda19978_configure_audout(sd, state->audio_ch_alloc);
			/* reset the audio FIFO */
			//tda19978_hdmi_info_reset(sd, RESET_AUDIO, false);
		}
		break;

	/* Auxiliary Video information (AVI) InfoFrame: see HDMI spec 8.2.1 */
	case HDMI_INFOFRAME_TYPE_AVI:
		state->avi_infoframe = frame.avi;
		set_rgb_quantization_range(state);

		/* configure upsampler: 0=bypass 1=repeatchroma 2=interpolate
		reg = io_read(sd, REG_PIX_REPEAT);
		reg &= ~PIX_REPEAT_MASK_UP_SEL;
		if (frame.avi.colorspace == HDMI_COLORSPACE_YUV422)
			reg |= (PIX_REPEAT_CHROMA << PIX_REPEAT_SHIFT);
		io_write(sd, REG_PIX_REPEAT, reg);*/

		/* ConfigurePixelRepeater: repeat n-times each pixel 
		reg = io_read(sd, REG_PIX_REPEAT);
		reg &= ~PIX_REPEAT_MASK_REP;
		reg |= frame.avi.pixel_repeat;
		io_write(sd, REG_PIX_REPEAT, reg);*/
		/* configure the receiver with the new colorspace */
		tda19978_configure_csc(sd);
		break;
	default:
		break;
	}
	return 0;
}

/* Not implemented yet */
static irqreturn_t tda19978_isr_thread(int irq, void *d)
{
	struct tda19978_state *state = d;
	struct v4l2_subdev *sd = &state->sd;
	u8 flags;
#if 0
	mutex_lock(&state->lock);
	do {
		/* read interrupt flags */
		flags = io_read(sd, REG_INT_FLG_CLR_TOP);
		if (flags == 0)
			break;

		/* SUS interrupt source (Input activity events) */
		if (flags & INTERRUPT_SUS)
			tda1997x_irq_sus(state, &flags);
		/* DDC interrupt source (Display Data Channel) */
		else if (flags & INTERRUPT_DDC)
			tda1997x_irq_ddc(state, &flags);
		/* RATE interrupt source (Digital Input activity) */
		else if (flags & INTERRUPT_RATE)
			tda1997x_irq_rate(state, &flags);
		/* Infoframe change interrupt */
		else if (flags & INTERRUPT_INFO)
			tda1997x_irq_info(state, &flags);
		/* Audio interrupt source:
		 *   freq change, DST,OBA,HBR,ASP flags, mute, FIFO err
		 */
		else if (flags & INTERRUPT_AUDIO)
			tda1997x_irq_audio(state, &flags);
		/* HDCP interrupt source (content protection) */
		if (flags & INTERRUPT_HDCP)
			tda1997x_irq_hdcp(state, &flags);
	} while (flags != 0);
	mutex_unlock(&state->lock);
#endif
	return IRQ_HANDLED;
}

/* -----------------------------------------------------------------------------
 * v4l2_subdev_video_ops
 */

static int
tda19978_g_input_status(struct v4l2_subdev *sd, u32 *status)
{
	struct tda19978_state *state = to_state(sd);
	int port_active;
	u32 vper;
	u16 hper;
	u16 hsper;

	mutex_lock(&state->lock);
	vper = io_read24(sd, REG_V_PER) & MASK_VPER;
	hper = io_read16(sd, REG_H_PER) & MASK_HPER;
	hsper = io_read16(sd, REG_HS_WIDTH) & MASK_HSWIDTH;
	/*
	 * The tda19978 supports A/B/C/D inputs but only a single output.
	 * I believe selection of A/B/C/D is automatic.
	 */
	port_active = tda19978_check_port(sd);
	
	v4l2_dbg(1, debug, sd, "inputs:%d/%d/%d/%d timings:%d/%d/%d\n",
		state->input_detect[0], state->input_detect[1],
		state->input_detect[2], state->input_detect[3],
		vper, hper, hsper);
	if (!port_active)
		*status = V4L2_IN_ST_NO_SIGNAL;
	else if (!vper || !hper || !hsper)
		*status = V4L2_IN_ST_NO_SYNC;
	else
		*status = 0;
	mutex_unlock(&state->lock);

	return 0;
};

static int tda19978_s_dv_timings(struct v4l2_subdev *sd,
				struct v4l2_dv_timings *timings)
{
	struct tda19978_state *state = to_state(sd);

	v4l_dbg(1, debug, state->client, "%s\n", __func__);

	if (v4l2_match_dv_timings(&state->timings, timings, 0, false))
		return 0; /* no changes */

	if (!v4l2_valid_dv_timings(timings, &tda19978_dv_timings_cap,
				   NULL, NULL))
		return -ERANGE;

	mutex_lock(&state->lock);
	state->timings = *timings;
	/* setup frame detection window and VHREF timing generator */
	tda19978_configure_vhref(sd);
	/* configure colorspace conversion */
	tda19978_configure_csc(sd);
	mutex_unlock(&state->lock);

	return 0;
}

static int tda19978_g_dv_timings(struct v4l2_subdev *sd,
				 struct v4l2_dv_timings *timings)
{
	struct tda19978_state *state = to_state(sd);

	v4l_dbg(1, debug, state->client, "%s\n", __func__);
	mutex_lock(&state->lock);
	*timings = state->timings;
	mutex_unlock(&state->lock);

	return 0;
}

static int tda19978_query_dv_timings(struct v4l2_subdev *sd,
				     struct v4l2_dv_timings *timings)
{
	struct tda19978_state *state = to_state(sd);
	int ret;

	v4l_dbg(1, debug, state->client, "%s\n", __func__);
	memset(timings, 0, sizeof(struct v4l2_dv_timings));
	mutex_lock(&state->lock);
	ret = tda19978_detect_std(state, timings);
	mutex_unlock(&state->lock);

	return ret;
}

static const struct v4l2_subdev_video_ops tda19978_video_ops = {
	.g_input_status = tda19978_g_input_status,
	.s_dv_timings = tda19978_s_dv_timings,
	.g_dv_timings = tda19978_g_dv_timings,
	.query_dv_timings = tda19978_query_dv_timings,
};

/* -----------------------------------------------------------------------------
 * v4l2_subdev_pad_ops
 */

static int tda19978_init_cfg(struct v4l2_subdev *sd,
			     struct v4l2_subdev_state *sd_state)
{
	struct tda19978_state *state = to_state(sd);
	struct v4l2_mbus_framefmt *mf;

	mf = v4l2_subdev_get_try_format(sd, sd_state, 0);
	mf->code = state->mbus_code;

	return 0;
}

static int tda19978_enum_mbus_code(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *sd_state,
				  struct v4l2_subdev_mbus_code_enum *code)
{
	struct tda19978_state *state = to_state(sd);

	v4l_dbg(1, debug, state->client, "%s %d\n", __func__, code->index);
	if (code->index >= 1)
		return -EINVAL;

	code->code = state->mbus_code;

	return 0;
}

static void tda19978_fill_format(struct tda19978_state *state,
				 struct v4l2_mbus_framefmt *format)
{
	const struct v4l2_bt_timings *bt;

	memset(format, 0, sizeof(*format));
	bt = &state->timings.bt;
	format->width = bt->width;
	format->height = bt->height;
	format->colorspace = state->colorimetry.colorspace;
	format->field = (bt->interlaced) ?
		V4L2_FIELD_SEQ_TB : V4L2_FIELD_NONE;
}

static int tda19978_get_format(struct v4l2_subdev *sd,
			       struct v4l2_subdev_state *sd_state,
			       struct v4l2_subdev_format *format)
{
	struct tda19978_state *state = to_state(sd);

	v4l_dbg(1, debug, state->client, "%s pad=%d which=%d\n",
		__func__, format->pad, format->which);

	tda19978_fill_format(state, &format->format);

	if (format->which == V4L2_SUBDEV_FORMAT_TRY) {
		struct v4l2_mbus_framefmt *fmt;

		fmt = v4l2_subdev_get_try_format(sd, sd_state, format->pad);
		format->format.code = fmt->code;
	} else
		format->format.code = state->mbus_code;

	return 0;
}

static int tda19978_set_format(struct v4l2_subdev *sd,
			       struct v4l2_subdev_state *sd_state,
			       struct v4l2_subdev_format *format)
{
	struct tda19978_state *state = to_state(sd);

	v4l_dbg(1, debug, state->client, "%s pad=%d which=%d fmt=0x%x\n",
		__func__, format->pad, format->which, format->format.code);

	tda19978_fill_format(state, &format->format);
	format->format.code = state->mbus_code;

	if (format->which == V4L2_SUBDEV_FORMAT_TRY) {
		struct v4l2_mbus_framefmt *fmt;

		fmt = v4l2_subdev_get_try_format(sd, sd_state, format->pad);
		*fmt = format->format;
	} else {
		int ret = tda19978_setup_format(state, format->format.code);

		if (ret)
			return ret;
		/* mbus_code has changed - re-configure csc/vidout */
		tda19978_configure_csc(sd);
		//tda1997x_configure_vidout(state);
	}

	return 0;
}

static int tda19978_get_edid(struct v4l2_subdev *sd, struct v4l2_edid *edid)
{
	struct tda19978_state *state = to_state(sd);

	v4l_dbg(1, debug, state->client, "%s pad=%d\n", __func__, edid->pad);
	memset(edid->reserved, 0, sizeof(edid->reserved));

	if (edid->start_block == 0 && edid->blocks == 0) {
		edid->blocks = state->edid.blocks;
		return 0;
	}

	if (!state->edid.present)
		return -ENODATA;

	if (edid->start_block >= state->edid.blocks)
		return -EINVAL;

	if (edid->start_block + edid->blocks > state->edid.blocks)
		edid->blocks = state->edid.blocks - edid->start_block;

	memcpy(edid->edid, state->edid.edid + edid->start_block * 128,
	       edid->blocks * 128);

	return 0;
}

static int tda19978_set_edid(struct v4l2_subdev *sd, struct v4l2_edid *edid)
{
	struct tda19978_state *state = to_state(sd);
	int i;

	v4l_dbg(1, debug, state->client, "%s pad=%d\n", __func__, edid->pad);
	memset(edid->reserved, 0, sizeof(edid->reserved));

	if (edid->start_block != 0)
		return -EINVAL;

	if (edid->blocks == 0) {
		state->edid.blocks = 0;
		state->edid.present = 0;
		tda19978_disable_edid(sd);
		return 0;
	}

	if (edid->blocks > 2) {
		edid->blocks = 2;
		return -E2BIG;
	}

	tda19978_disable_edid(sd);

	/* write base EDID */
	for (i = 0; i < 128; i++)
		io_write(sd, REG_EDID_IN_BYTE0 + i, edid->edid[i]);

	/* write CEA Extension */
	for (i = 0; i < 128; i++)
		io_write(sd, REG_EDID_IN_BYTE128 + i, edid->edid[i+128]);

	/* store state */
	memcpy(state->edid.edid, edid->edid, 256);
	state->edid.blocks = edid->blocks;

	tda19978_enable_edid(sd);

	return 0;
}

static int tda19978_get_dv_timings_cap(struct v4l2_subdev *sd,
				       struct v4l2_dv_timings_cap *cap)
{
	*cap = tda19978_dv_timings_cap;
	return 0;
}

static int tda19978_enum_dv_timings(struct v4l2_subdev *sd,
				    struct v4l2_enum_dv_timings *timings)
{
	return v4l2_enum_dv_timings_cap(timings, &tda19978_dv_timings_cap,
					NULL, NULL);
}

static const struct v4l2_subdev_pad_ops tda19978_pad_ops = {
	.init_cfg = tda19978_init_cfg,
	.enum_mbus_code = tda19978_enum_mbus_code,
	.get_fmt = tda19978_get_format,
	.set_fmt = tda19978_set_format,
	.get_edid = tda19978_get_edid,
	.set_edid = tda19978_set_edid,
	.dv_timings_cap = tda19978_get_dv_timings_cap,
	.enum_dv_timings = tda19978_enum_dv_timings,
};


/* -----------------------------------------------------------------------------
 * v4l2_subdev_core_ops
 */

static int tda19978_log_infoframe(struct v4l2_subdev *sd, int addr)
{
	struct tda19978_state *state = to_state(sd);
	union hdmi_infoframe frame;
	u8 buffer[40] = { 0 };
	int len, err;

	/* read data */
	len = io_readn(sd, addr, sizeof(buffer), buffer);
	v4l2_dbg(1, debug, sd, "infoframe: addr=%d len=%d\n", addr, len);
	err = hdmi_infoframe_unpack(&frame, buffer, len);
	if (err) {
		v4l_err(state->client,
			"failed parsing %d byte infoframe: 0x%04x/0x%02x\n",
			len, addr, buffer[0]);
		return err;
	}
	hdmi_infoframe_log(KERN_INFO, &state->client->dev, &frame);

	return 0;
}


static int tda19978_log_status(struct v4l2_subdev *sd)
{
	struct tda19978_state *state = to_state(sd);
	struct v4l2_dv_timings timings;
	struct hdmi_avi_infoframe *avi = &state->avi_infoframe;
	int active;

	tda19978_parse_infoframe(state, AUD_IF);
	tda19978_parse_infoframe(state, AVI_IF);

	v4l2_info(sd, "-----Chip status-----\n");
	v4l2_info(sd, "Chip: %s\n", state->info->name);
	v4l2_info(sd, "EDID Enabled: %s\n", state->edid.present ? "yes" : "no");

	active = tda19978_check_port(sd);
	v4l2_info(sd, "-----Signal status-----\n");
	v4l2_info(sd, "Port status: 0x%02x/0x%02x/0x%02x/0x%02x",
			state->input_detect[0], state->input_detect[1],
			state->input_detect[2], state->input_detect[3]);
	if (active) {
		v4l2_info(sd, "Port[%d] is active\n", (state->current_port & 0x18) >> 3);
	} else {
		v4l2_info(sd, "No Active Port\n");
	}
	/*
	v4l2_info(sd, "Cable detected (+5V power): %s\n", 
		tda19978_detect_tx_5v(sd) ? "yes" : "no");
	v4l2_info(sd, "HPD detected: %s\n",
		  tda19978_detect_tx_hpd(sd) ? "yes" : "no");
	*/
	v4l2_info(sd, "-----Video Timings-----\n");
	switch (tda19978_detect_std(state, &timings)) {
	case -ENOLINK:
		v4l2_info(sd, "No video detected\n");
		break;
	case -ERANGE:
		v4l2_info(sd, "Invalid signal detected\n");
		break;
	}
	v4l2_print_dv_timings(sd->name, "Configured format: ",
			      &state->timings, true);
	
	v4l2_info(sd, "-----Color space-----\n");
	v4l2_info(sd, "Input color space: %s %s %s",
		  hdmi_colorspace_names[avi->colorspace],
		  (avi->colorspace == HDMI_COLORSPACE_RGB) ? "" :
			hdmi_colorimetry_names[avi->colorimetry],
		  v4l2_quantization_names[state->colorimetry.quantization]);
	v4l2_info(sd, "Output color space: %s",
		  vidfmt_names[state->vid_fmt]);
	v4l2_info(sd, "Color space conversion: %s", state->conv ?
		  state->conv->name : "None");
	
	v4l2_info(sd, "-----Audio-----\n");
	if (state->audio_channels) {
		v4l2_info(sd, "audio: %dch %dHz\n", state->audio_channels,
			  state->audio_samplerate);
	} else {
		v4l2_info(sd, "audio: none\n");
	}
	
	v4l2_info(sd, "-----Infoframes-----\n");
	tda19978_log_infoframe(sd, AUD_IF);
	tda19978_log_infoframe(sd, SPD_IF);
	tda19978_log_infoframe(sd, AVI_IF);

	return 0;
}


static const struct v4l2_subdev_core_ops tda19978_core_ops = {
	.log_status = tda19978_log_status,
	.subscribe_event = NULL,
	.unsubscribe_event = NULL,//v4l2_event_subdev_unsubscribe,
};

/* -----------------------------------------------------------------------------
 * v4l2_subdev_ops
 */

static const struct v4l2_subdev_ops tda19978_subdev_ops = {
	.core = &tda19978_core_ops,
	.video = &tda19978_video_ops,
	.pad = &tda19978_pad_ops,
};


/* -----------------------------------------------------------------------------
 * v4l2_controls
 */

static int tda19978_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct v4l2_subdev *sd = to_sd(ctrl);
	struct tda19978_state *state = to_state(sd);

	switch (ctrl->id) {
	/* allow overriding the default RGB quantization range */
	case V4L2_CID_DV_RX_RGB_RANGE:
		state->rgb_quantization_range = ctrl->val;
		set_rgb_quantization_range(state);
		tda19978_configure_csc(sd);
		return 0;
	}
 
	return -EINVAL;
};
 
static int tda19978_g_volatile_ctrl(struct v4l2_ctrl *ctrl)
{
	struct v4l2_subdev *sd = to_sd(ctrl);
	struct tda19978_state *state = to_state(sd);

	if (ctrl->id == V4L2_CID_DV_RX_IT_CONTENT_TYPE) {
		ctrl->val = state->avi_infoframe.content_type;
		return 0;
	}
	return -EINVAL;
};

static const struct v4l2_ctrl_ops tda19978_ctrl_ops = {
	.s_ctrl = tda19978_s_ctrl,
	.g_volatile_ctrl = tda19978_g_volatile_ctrl,
};


static const struct i2c_device_id tda19978_i2c_id[] = {
	{"tda19977", (kernel_ulong_t)&tda19978_chip_info[TDA19977]},
	{"tda19978", (kernel_ulong_t)&tda19978_chip_info[TDA19978]},
	{ },
};
MODULE_DEVICE_TABLE(i2c, tda19978_i2c_id);

static const struct of_device_id tda19978_of_id[] __maybe_unused = {
	{ .compatible = "nxp,tda19977", .data = &tda19978_chip_info[TDA19977] },
	{ .compatible = "nxp,tda19978", .data = &tda19978_chip_info[TDA19978] },
	{ },
};
MODULE_DEVICE_TABLE(of, tda19978_of_id);

static int tda19978_identify_module(struct tda19978_state *state)
{
	struct v4l2_subdev *sd = &state->sd;
	enum tda19978_type type;
	u8 reg;

	/* Read chip ID */
	reg = io_read(sd, REG_VERSION);
	printk("%s: VERSION = 0x%02x", __func__, reg);
	if (reg != 0x41) {
		dev_err(&state->client->dev, "unsupported chip ID\n");
		return -EIO;
	}

	/* read chip revision */
	state->chip_revision = 0x01;

	return 0;
}

static const struct media_entity_operations tda19978_media_ops = {
	.link_validate = v4l2_subdev_link_validate,
};

/* -----------------------------------------------------------------------------
 * HDMI Audio Codec
 */

/* refine sample-rate based on HDMI source */
static int tda19978_pcm_startup(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	struct tda19978_state *state = snd_soc_dai_get_drvdata(dai);
	struct snd_soc_component *component = dai->component;
	struct snd_pcm_runtime *rtd = substream->runtime;
	int rate, err;

	rate = state->audio_samplerate;
	err = snd_pcm_hw_constraint_minmax(rtd, SNDRV_PCM_HW_PARAM_RATE,
					   rate, rate);
	if (err < 0) {
		dev_err(component->dev, "failed to constrain samplerate to %dHz\n",
			rate);
		return err;
	}
	dev_info(component->dev, "set samplerate constraint to %dHz\n", rate);

	return 0;
}

static const struct snd_soc_dai_ops tda19978_dai_ops = {
	.startup = tda19978_pcm_startup,
};

static struct snd_soc_dai_driver tda19978_audio_dai = {
	.name = "tda19978",
	.capture = {
		.stream_name = "Capture",
		.channels_min = 2,
		.channels_max = 8,
		.rates = SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_48000,
	},
	.ops = &tda19978_dai_ops,
};

static int tda19978_codec_probe(struct snd_soc_component *component)
{
	return 0;
}

static void tda19978_codec_remove(struct snd_soc_component *component)
{
}

static struct snd_soc_component_driver tda19978_codec_driver = {
	.probe			= tda19978_codec_probe,
	.remove			= tda19978_codec_remove,
	.idle_bias_on		= 1,
	.use_pmdown_time	= 1,
	.endianness		= 1,
	.non_legacy_dai_naming	= 1,
};

static void tda19978_init_0x11F7(struct v4l2_subdev *sd, u16 reg) {
	io_write(sd, 0x000D, 0x0F);
	io_read24(sd, 0x000E);	// 0x88FF9E
	io_read24(sd, 0x0012);	// 0x222202
	io_write24(sd, 0x000E, 0x88FF9E);
	io_write24(sd, 0x0012, 0x444402);

	io_write16(sd, 0x11F7, reg);
	io_write(sd, 0x13E0, 0x00);
	io_write16(sd, 0x11F7, 0x0000);
	io_write(sd, 0x13E0, 0x00);
}

/* 
	Send i2c command sequence to initialize.
	These commands are came from Windows driver.
*/
static int tda19978_core_init(struct v4l2_subdev *sd) {
	struct tda19978_state *state = to_state(sd);
	u32 reg;

	/* Reset */
	io_write(sd, 0x000D, 0x0F);
	io_write24(sd, 0x000E, 0x88FF9E);
	io_write24(sd, 0x0012, 0x444402);
	io_write(sd, 0x000D, 0x0B);
	io_write24(sd, 0x000E, 0xFFFFFF);
	io_write24(sd, 0x0012, 0xFFFFFF);

	/* Set registers */
	io_write(sd, 0x13C5, 0x00);
	io_write(sd, 0x13C6, 0x57);
	io_write(sd, 0x13C7, 0xE4);		// CLK_MIN_RATE?
	io_write(sd, 0x13C0, 0x37);
	io_write(sd, 0x13C3, 0xFF);
	io_write(sd, 0x1387, 0x00);
	io_write(sd, 0x1352, 0x07);

	io_write(sd, 0x1210, 0x00);
	io_write(sd, 0x1233, 0x01);
	io_write(sd, 0x1234, 0x03);
	io_write(sd, 0x1236, 0x03);
	io_write(sd, 0x1238, 0x01);
	io_write(sd, 0x1245, 0x0F);
	io_write(sd, 0x1239, 0x00);
	io_write(sd, 0x121E, 0x82);

	io_write(sd, 0x1109, 0x30);
	io_write(sd, 0x0018, 0x09);
	io_write16(sd, 0x11F7, 0x0080);
	io_write(sd, 0x13E0, 0x00);
	io_write(sd, 0x111A, 0x00);
	io_write(sd, 0x111E, 0x0A);

	/* ??? */
	reg = io_read24(sd, 0x111B);	// 0xA10056
	io_write24(sd, 0x1140, 0x0274);
	io_write24(sd, 0x1142, 0x1B9A);
	io_write(sd, 0x0018, 0x01);

	io_write(sd, 0x1149, 0x81);
	io_write(sd, 0x1100, 0x01);
	reg = io_read(sd, 0x11F9);	// 0x88
	io_write(sd, 0x11F9, 0x08);	// HDMI DDC enable?

	io_write(sd, 0x0018, 0x09);
	io_write16(sd, 0x11F7, 0x0080); 
	io_write(sd, 0x13E0, 0x00);
	io_write24(sd, 0x1140, 0x0274);
	io_write24(sd, 0x1142, 0x1B9A);
	io_write(sd, 0x1150, 0x00);
	io_write(sd, 0x111E, 0x06);
	io_write24(sd, 0x1140, 0x0174);
	io_write24(sd, 0x1142, 0x1B9A);
	io_write16(sd, 0x11F7, 0x0000);
	io_write(sd, 0x13E0, 0x00);

	io_write24(sd, 0x1158, 0x07183F);
	io_write(sd, 0x0018, 0x01);

	io_write(sd, 0x0001, 0x14);
	io_write(sd, 0x00E8, 0x12);
	reg = io_read(sd, 0x0017);	// 0x2F
	io_write(sd, 0x0017, 0x2F);
	reg = io_read(sd, 0x00E9);	// 0x00
	io_write(sd, 0x00E9, 0x00);
	io_write(sd, 0x00E7, 0x00);
	io_write24(sd, 0x1158, 0x07183F);
	io_write24(sd, 0x1101, 0x808000);
	reg = io_read(sd, 0x1104);	// 0x18
	io_write(sd, 0x1104, 0x18);

	/* EDID (0x2000, 0x2100) */
	// set edid body later ( pad_ops->set_edid )
	io_write(sd, 0x2180, 0x08);
	io_write16(sd, 0x2181, 0x1000);
	io_write16(sd, 0x2183, 0x2000);
	io_write16(sd, 0x2185, 0x3000);
	io_write16(sd, 0x2187, 0x4000);
	io_write(sd, 0x2189, 0x66);
	io_write(sd, 0x218A, 0x56);
	io_write(sd, 0x218B, 0x46);
	io_write(sd, 0x218C, 0x36);

	io_write(sd, 0x0001, 0x14);
	io_write(sd, 0x00E8, 0x12);
	reg = io_read(sd, 0x0017);	// 0x2F
	io_write(sd, 0x0017, 0x2F);
	reg = io_read(sd, 0x00E9);	// 0x00
	io_write(sd, 0x00E9, 0x00);
	io_write(sd, 0x00E7, 0x00);
	io_write24(sd, 0x1158, 0x07183F);
	io_write24(sd, 0x1101, 0x808000);
	reg = io_read(sd, 0x1104);	// 0x18
	io_write(sd, 0x1104, 0x18);

	/* CSC */
	state->conv = &conv_matrix[RGBLIMITED_ITU601];
	io_write(sd, REG_VDP_CTRL, 0x00);
	io_write16(sd, REG_VDP_MATRIX + 0, state->conv->offint1);
	io_write16(sd, REG_VDP_MATRIX + 2, state->conv->offint2);
	io_write16(sd, REG_VDP_MATRIX + 4, state->conv->offint3);
	io_write16(sd, REG_VDP_MATRIX + 6, state->conv->p11coef);
	io_write16(sd, REG_VDP_MATRIX + 8, state->conv->p12coef);
	io_write16(sd, REG_VDP_MATRIX + 10, state->conv->p13coef);
	io_write16(sd, REG_VDP_MATRIX + 12, state->conv->p21coef);
	io_write16(sd, REG_VDP_MATRIX + 14, state->conv->p22coef);
	io_write16(sd, REG_VDP_MATRIX + 16, state->conv->p23coef);
	io_write16(sd, REG_VDP_MATRIX + 18, state->conv->p31coef);
	io_write16(sd, REG_VDP_MATRIX + 20, state->conv->p32coef);
	io_write16(sd, REG_VDP_MATRIX + 22, state->conv->p33coef);
	io_write16(sd, REG_VDP_MATRIX + 24, state->conv->offout1);
	io_write16(sd, REG_VDP_MATRIX + 26, state->conv->offout2);
	io_write16(sd, REG_VDP_MATRIX + 28, state->conv->offout3);
	
	reg = io_read(sd, 0x00E6);	// 0xA0
	io_write(sd, 0x00E6, 0xF0);
	io_write(sd, 0x00E5, 0x79);	//format/color?
	io_write(sd, 0x1341, 0x06);
	reg = io_read(sd, 0x0017);	// 0x2F
	io_write(sd, 0x0017, 0x2F);
	reg = io_read(sd, 0x00E9);	// 0x00
	io_write(sd, 0x00E9, 0x00);
	reg = io_read(sd, 0x0017);	// 0x2F
	io_write(sd, 0x0017, 0x2F);
	io_write(sd, 0x0016, 0x04);
	io_write(sd, 0x00DB, 0x88);
	io_write(sd, 0x00EB, 0x18);
	io_write(sd, 0x00EA, 0x78);	// format/color?
	io_write24(sd, 0x1101, 0x808000);

	reg = io_read(sd, 0x1104);	// 0x18
	io_write(sd, 0x1104, 0x18);
	reg = io_read(sd, 0x1104);	// 0x18
	io_write(sd, 0x1104, 0x10);
	io_write(sd, 0x1363, 0x19);

	tda19978_check_port(sd);
	tda19978_init_0x11F7(sd, 0x0004);
	tda19978_init_0x11F7(sd, 0x0002);

	tda19978_check_port(sd);
	tda19978_init_0x11F7(sd, 0x0008);

	tda19978_check_port(sd);
	reg = io_read(sd, 0x1149);	// 0x81
	io_write(sd, 0x1149, 0x81);

	tda19978_check_port(sd);

	return 0;
}

static int tda19978_probe(struct i2c_client *client,
						  const struct i2c_device_id *id)
{
	struct tda19978_state *state;
	struct i2c_client *hpd_client;
	struct v4l2_subdev *sd;
	struct v4l2_ctrl_handler *hdl;
	struct v4l2_ctrl *ctrl;
	static const struct v4l2_dv_timings cea1080P30 =
		V4L2_DV_BT_CEA_1920X1080P30;
	int ret;

	/* Check if the adapter supports the needed features */
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -EIO;

	state = kzalloc(sizeof(struct tda19978_state), GFP_KERNEL);
	if (!state)
		return -ENOMEM;

	state->client = client;
	state->info = (const struct tda19978_chip_info *)id->driver_data;

	mutex_init(&state->page_lock);
	mutex_init(&state->lock);
	state->page = 0xff;

	INIT_DELAYED_WORK(&state->delayed_work_enable_hpd, tda19978_delayed_work_enable_hpd);

	/* set video format based on chip and bus width */
	ret = tda19978_identify_module(state);
	if (ret)
		goto err_free_mutex;

	/* initialize subdev */
	sd = &state->sd;
	v4l2_i2c_subdev_init(sd, client, &tda19978_subdev_ops);
	snprintf(sd->name, sizeof(sd->name), "%s %d-%04x",
			 id->name, i2c_adapter_id(client->adapter), client->addr);
	printk("%s: sd->name = %s", __func__, sd->name);

	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE | V4L2_SUBDEV_FL_HAS_EVENTS;
	sd->entity.function = MEDIA_ENT_F_DV_DECODER;
	sd->entity.ops = &tda19978_media_ops;

	/* set allowed mbus modes based on chip, bus-type, and bus-width */
	state->mbus_code = MEDIA_BUS_FMT_UYVY8_1X16;

	/* default format */
	tda19978_setup_format(state, state->mbus_code);
	state->timings = cea1080P30;

	/*
	 * default to SRGB full range quantization
	 * (in case we don't get an infoframe such as DVI signal
	 */
	state->colorimetry.colorspace = V4L2_COLORSPACE_SRGB;
	state->colorimetry.quantization = V4L2_QUANTIZATION_FULL_RANGE;

	/* disable/reset HDCP to get correct I2C access to Rx HDMI */
	// io_write(sd, REG_MAN_SUS_HDMI_SEL, MAN_RST_HDCP | MAN_DIS_HDCP);

	/*
	 * TDA19978 does not have HPD assertion pin. 
	 * Instead, HPD signal is asserted by PCA9536 I2C IO expander
	 * implemented on PCB with TDA19978.
	 */
	hpd_client = i2c_new_ancillary_device(client, "hpd", 0x41);
	if (IS_ERR(hpd_client))
	{
		ret = PTR_ERR(hpd_client);
		v4l2_err(sd, "failed to create i2c client for hpd");
		goto err_free_mutex;
	}
	state->client_hpd = hpd_client;
	/* HPD assert high */
	ret = i2c_smbus_write_byte_data(hpd_client, 0x03, 0x00);
	ret = i2c_smbus_write_byte_data(hpd_client, 0x01, 0x0f);

	ret = tda19978_core_init(sd);
	if (ret)
		goto err_free_mutex;

	/* control handlers */
	hdl = &state->hdl;
	v4l2_ctrl_handler_init(hdl, 3);
	ctrl = v4l2_ctrl_new_std_menu(hdl, &tda19978_ctrl_ops,
							V4L2_CID_DV_RX_IT_CONTENT_TYPE,
							V4L2_DV_IT_CONTENT_TYPE_NO_ITC, 0,
							V4L2_DV_IT_CONTENT_TYPE_NO_ITC);
	if (ctrl)
		ctrl->flags |= V4L2_CTRL_FLAG_VOLATILE;

	/* custom controls */
	state->rgb_quantization_range_ctrl = v4l2_ctrl_new_std_menu(hdl, &tda19978_ctrl_ops,
											V4L2_CID_DV_RX_RGB_RANGE,
											V4L2_DV_RGB_RANGE_FULL, 0,
											V4L2_DV_RGB_RANGE_AUTO);

	state->sd.ctrl_handler = hdl;
	if (hdl->error)
	{
		ret = hdl->error;
		goto err_free_handler;
	}

	v4l2_ctrl_handler_setup(hdl);

	/* initialize source pads */
	state->pads[TDA19978_PAD_SOURCE].flags = MEDIA_PAD_FL_SOURCE;
	ret = media_entity_pads_init(&sd->entity, TDA19978_NUM_PADS,
								 state->pads);
	if (ret)
	{
		v4l_err(client, "failed entity_init: %d", ret);
		goto err_free_handler;
	}

	ret = v4l2_async_register_subdev(sd);
	if (ret)
		goto err_free_media;

	/* register audio DAI */
	/*
	u64 formats;
	formats = SNDRV_PCM_FMTBIT_S16_LE;
	tda19978_audio_dai.capture.formats = formats;
	ret = devm_snd_soc_register_component(&state->client->dev,
						&tda19978_codec_driver,
						&tda19978_audio_dai, 1);
	if (ret) {
		dev_err(&client->dev, "register audio codec failed\n");
		goto err_free_media;
	}
	dev_set_drvdata(&state->client->dev, state);
	v4l_info(state->client, "registered audio codec\n");
	*/

	/* request irq */
	/*
	ret = devm_request_threaded_irq(&client->dev, client->irq,
					NULL, tda19978_isr_thread,
					IRQF_TRIGGER_LOW | IRQF_ONESHOT,
					KBUILD_MODNAME, state);
	if (ret) {
		v4l_err(client, "irq%d reg failed: %d\n", client->irq, ret);
		goto err_free_media;
	}
	*/
	printk("%s: Done.", __func__);

	return 0;

err_free_media:
	media_entity_cleanup(&sd->entity);
err_free_handler:
	v4l2_ctrl_handler_free(&state->hdl);
err_free_mutex:
	cancel_delayed_work(&state->delayed_work_enable_hpd);
	mutex_destroy(&state->page_lock);
	mutex_destroy(&state->lock);
err_free_state:
	kfree(state);
	dev_err(&client->dev, "%s failed: %d\n", __func__, ret);

	return ret;
}

static int tda19978_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct tda19978_state *state = to_state(sd);
	// struct tda19978_platform_data *pdata = &state->pdata;

	/*
	if (pdata->audout_format) {
		mutex_destroy(&state->audio_lock);
	}
	*/

	// disable_irq(state->client->irq);
	// tda19978_power_mode(state, 0);

	v4l2_async_unregister_subdev(sd);
	media_entity_cleanup(&sd->entity);
	v4l2_ctrl_handler_free(&state->hdl);
	// regulator_bulk_disable(TDA19978_NUM_SUPPLIES, state->supplies);
	cancel_delayed_work_sync(&state->delayed_work_enable_hpd);
	i2c_unregister_device(state->client_hpd);
	mutex_destroy(&state->page_lock);
	mutex_destroy(&state->lock);

	kfree(state);

	printk("%s: Done.", __func__);

	return 0;
}

static struct i2c_driver tda19978_i2c_driver = {
	.driver = {
		.name = "tda19978",
		.of_match_table = of_match_ptr(tda19978_of_id),
	},
	.probe = tda19978_probe,
	.remove = tda19978_remove,
	.id_table = tda19978_i2c_id,
};

module_i2c_driver(tda19978_i2c_driver);

MODULE_AUTHOR("notsugi");
MODULE_DESCRIPTION("TDA19978 HDMI Receiver driver");
MODULE_LICENSE("GPL v2");
