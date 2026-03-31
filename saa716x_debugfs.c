#include <linux/fs.h>
#include <linux/debugfs.h>
#include <linux/v4l2-dv-timings.h>

#include <media/v4l2-subdev.h>
#include <media/v4l2-common.h>
#include <media/v4l2-dv-timings.h>

#include "saa716x_priv.h"
#include "saa716x_cap.h"
#include "saa716x_vip_reg.h"
#include "saa716x_aip_reg.h"
#include "saa716x_mod.h"
#include "saa716x_debugfs.h"


static const u32 vi_ch[] = {
	VI0,
	VI1
};

static const u32 ai_ch[] = {
	AI0,
	AI1
};

struct saa716x_debugfs_data {
	unsigned int    size;
	struct mutex	mutex;
};

/*
    VIP parameters for given timings.
    VIP_FMT_TYPE2 should be set for interlaced video.
*/
static int video_vip_get_stream_params_tda19978(struct vip_stream_params *params, struct v4l2_dv_timings *timings)
{
    u8 cea861_vic;
    if (timings->type == V4L2_DV_BT_656_1120 && (timings->bt.flags & V4L2_DV_FL_HAS_CEA861_VIC)){
        cea861_vic = timings->bt.cea861_vic;
    } else {
        return -1;
    }

    params->source_format = VIP_FMT_TYPE2;
	switch (cea861_vic)
	{
	case 4:	 /* 1280x720p60 */
        params->bits = 16;
        params->samples = 1280;
        params->lines = 720;
        params->pitch = 1280 * 2;
        params->offset_x = 366;
        params->offset_y = 0;
        params->stream_flags = VIP_HD;
    break;
	case 19: /* 1280x720p50 */
        params->bits = 16;
        params->samples = 1280;
        params->lines = 720;
        params->pitch = 1280 * 2;
        params->offset_x = 696;
        params->offset_y = 0;
        params->stream_flags = VIP_HD;
        break;
    case 5:	 /* 1920x1080i60 */
        params->bits = 16;
        params->samples = 1920;
        params->lines = 1080;
        params->pitch = 1920 * 2;
        params->offset_x = 276;
        params->offset_y = 0; // relate with PSU_WINDOW
        params->stream_flags = VIP_ODD_FIELD | VIP_EVEN_FIELD | VIP_INTERLACED | VIP_HD;
        break;
    case 20: /* 1920x1080i50 */
    params->bits = 16;
        params->samples = 1920;
        params->lines = 1080;
        params->pitch = 1920 * 2;
        params->offset_x = 716;
        params->offset_y = 0;
        params->stream_flags = VIP_ODD_FIELD | VIP_EVEN_FIELD | VIP_INTERLACED | VIP_HD;
        break;
	case 32: /* 1920x1080p24 */
	case 33: /* 1920x1080p25 */
	case 34: /* 1920x1080p30 */
        params->source_format = VIP_FMT_DEFAULT;
		params->bits = 16;
		params->samples = 1920;
		params->lines = 1080;
		params->pitch = 1920 * 2;
		params->offset_x = 0;
		params->offset_y = 44;
		params->stream_flags = VIP_HD;
		break;

	default:
		return -1;
	}
	return 0;
}

static int video_vip_get_stream_params_tda19978_old(struct vip_stream_params *params, struct v4l2_dv_timings *timings)
{
    u8 cea861_vic;
    if (timings->type == V4L2_DV_BT_656_1120 && (timings->bt.flags & V4L2_DV_FL_HAS_CEA861_VIC)){
        cea861_vic = timings->bt.cea861_vic;
    } else {
        return -1;
    }

    params->source_format = VIP_FMT_TYPE2;
	switch (cea861_vic)
	{
	case 4:	 /* 1280x720p60 */
        params->bits = 16;
        params->samples = 1280;
        params->lines = 720;
        params->pitch = 1280 * 2;
        params->offset_x = 366;
        params->offset_y = 0;
        params->stream_flags = VIP_HD;
    break;
	case 19: /* 1280x720p50 */
        params->bits = 16;
        params->samples = 1280;
        params->lines = 720;
        params->pitch = 1280 * 2;
        params->offset_x = 696;
        params->offset_y = 0;
        params->stream_flags = VIP_HD;
        break;
    case 5:	 /* 1920x1080i60 */
        params->bits = 16;
        params->samples = 1920;
        params->lines = 1080;
        params->pitch = 1920 * 2;
        params->offset_x = 276;
        params->offset_y = 0; // relate with PSU_WINDOW
        params->stream_flags = VIP_ODD_FIELD | VIP_EVEN_FIELD | VIP_INTERLACED | VIP_HD;
        break;
    case 20: /* 1920x1080i50 */
    params->bits = 16;
        params->samples = 1920;
        params->lines = 1080;
        params->pitch = 1920 * 2;
        params->offset_x = 716;
        params->offset_y = 0;
        params->stream_flags = VIP_ODD_FIELD | VIP_EVEN_FIELD | VIP_INTERLACED | VIP_HD;
        break;
	case 32: /* 1920x1080p24 */
        params->bits = 16;
        params->samples = 1920;
        params->lines = 1080;
        params->pitch = 1920 * 2;
        params->offset_x = 826;
        params->offset_y = 0;
        params->stream_flags = VIP_HD;
        break;
	case 33: /* 1920x1080p25 */
        params->bits = 16;
        params->samples = 1920;
        params->lines = 1080;
        params->pitch = 1920 * 2;
        params->offset_x = 716;
        params->offset_y = 0;
        params->stream_flags = VIP_HD;
        break;
	case 34: /* 1920x1080p30 */
		params->bits = 16;
		params->samples = 1920;
		params->lines = 1080;
		params->pitch = 1920 * 2;
		params->offset_x = 276;
		params->offset_y = 0;
		params->stream_flags = VIP_HD;
		break;

	default:
		return -1;
	}
	return 0;
}

static int video_vip_get_stream_params_adv7611(struct vip_stream_params *params, struct v4l2_dv_timings *timings)
{
    u8 cea861_vic;
    if (timings->type == V4L2_DV_BT_656_1120 && (timings->bt.flags & V4L2_DV_FL_HAS_CEA861_VIC)){
        cea861_vic = timings->bt.cea861_vic;
    } else {
        return -1;
    }
    
    params->source_format = VIP_FMT_DEFAULT;
	switch (cea861_vic)
	{
	case 4:	 /* 1280x720p60 */
	case 19: /* 1280x720p50 */
		params->bits = 16;
		params->samples = 1280;
		params->lines = 720;
		params->pitch = 1280 * 2;
		params->offset_x = 0;
		params->offset_y = 30;
		params->stream_flags = VIP_HD;
		break;
	case 5:	 /* 1920x1080i60 */
        params->source_format = VIP_FMT_TYPE2;
		params->bits = 16;
		params->samples = 1920;
		params->lines = 1080;
		params->pitch = 1920 * 2;
		params->offset_x = 276;  // 0
		params->offset_y = 0; // 20
		params->stream_flags = VIP_ODD_FIELD | VIP_EVEN_FIELD | VIP_INTERLACED | VIP_HD;
        break;
    case 20: /* 1920x1080i50 */
        params->source_format = VIP_FMT_TYPE2;
        params->bits = 16;
        params->samples = 1920;
        params->lines = 1080;
        params->pitch = 1920 * 2;
        params->offset_x = 716;
        params->offset_y = 0;
        params->stream_flags = VIP_ODD_FIELD | VIP_EVEN_FIELD | VIP_INTERLACED | VIP_HD;
        break;
	case 32: /* 1920x1080p24 */
	case 33: /* 1920x1080p25 */
	case 34: /* 1920x1080p30 */ 
		params->bits = 16;
		params->samples = 1920;
		params->lines = 1080;
		params->pitch = 1920 * 2;
		params->offset_x = 0;
		params->offset_y = 45;
		params->stream_flags = VIP_HD;
		break;

	default:
		return -1;
	}
	return 0;
}

static ssize_t video_vip_read(struct saa716x_dev *saa716x,
                              struct vip_stream_params *stream_params,
                              char __user *buf, size_t count)
{
	int vi_port, one_shot;
	size_t num_bytes = 0;
	size_t copy_bytes;
	u32 read_index;
	u8 *data;
	int err = 0;

	vi_port = saa716x->config->capture_config.vip_port;
	one_shot = 1;

	if (count > stream_params->lines * stream_params->pitch)
		count = stream_params->lines * stream_params->pitch;

	saa716x_vip_start(saa716x, vi_port, one_shot, stream_params);
	/* Sleep long enough to be sure to capture at least one frame.
	TODO: Change this in a way that it just waits the required time. */
	msleep(100);
	saa716x_vip_stop(saa716x, vi_port);

	read_index = saa716x->vip[vi_port].read_index;
	printk("%s: read_index = %d", __func__, read_index);

	if ((stream_params->stream_flags & VIP_INTERLACED) &&
		(stream_params->stream_flags & VIP_ODD_FIELD) &&
		(stream_params->stream_flags & VIP_EVEN_FIELD))
	{
		read_index = read_index & ~1;
		read_index = (read_index + 7) & 7;
		read_index = read_index / 2;
	}
	else
	{
		read_index = (read_index + 7) & 7;
	}

	copy_bytes = count;
	if (copy_bytes > (SAA716x_PAGE_SIZE / 8 * SAA716x_PAGE_SIZE))
		copy_bytes = SAA716x_PAGE_SIZE / 8 * SAA716x_PAGE_SIZE;
	data = (u8 *)saa716x->vip[vi_port].dma_buf[0][read_index].mem_virt;
	if (copy_to_user((void __user *)(buf + num_bytes), data, copy_bytes))
	{
		err = -EFAULT;
		goto out;
	}
	num_bytes += copy_bytes;
	if (saa716x->vip[vi_port].dual_channel &&
		count - num_bytes > 0)
	{
		copy_bytes = count - num_bytes;
		if (copy_bytes > (SAA716x_PAGE_SIZE / 8 * SAA716x_PAGE_SIZE))
			copy_bytes = SAA716x_PAGE_SIZE / 8 * SAA716x_PAGE_SIZE;
		data = (u8 *)saa716x->vip[vi_port].dma_buf[1][read_index].mem_virt;
		if (copy_to_user((void __user *)(buf + num_bytes), data,
							copy_bytes))
		{
			err = -EFAULT;
			goto out;
		}
		num_bytes += copy_bytes;
	}
	printk("%s: %ld bytes copied to userspace buffer", __func__, num_bytes);
	return num_bytes;

out:
	return err;
}

static int saa716x_debugfs_open(struct inode *inode, struct file *file)
{
	struct saa716x_dev *saa716x = (struct saa716x_dev*)inode->i_private;
	file->private_data = saa716x;
	printk("%s: debugfs opened", __func__);
	/*
	struct saa716x_debugfs_data *debug_data;
	debug_data = kzalloc(sizeof(*debug_data), GFP_KERNEL);
	if (!debug_data)
	return -ENOMEM;

	mutex_init(&debug_data->mutex);
	file->private_data = debug_data;
	*/ 
	return nonseekable_open(inode, file);
}

static ssize_t saa716x_debugfs_aip_read(struct file *file, char *buf, size_t len, loff_t *off)
{
	struct saa716x_dev *saa716x = (struct saa716x_dev*)file->private_data;
	enum saa716x_capture_subdev sd_type = saa716x->config->capture_config.subdev;
	struct aip_stream_params param;
	size_t copy_bytes, num_bytes = 0;
	u32 val, read_index;
	int ai_port = 0;
	u8 *data;
	int i, err = 0;

	if(sd_type == SAA716x_SUBDEV_TDA19978) {
		param.ai_size = 0x0640;
	} else {
		param.ai_size = 0x05c0;
	}
	saa716x_aip_start(saa716x, ai_port, &param);
	msleep(270);
	val = SAA716x_EPRD(ai_ch[ai_port], AI_STATUS);
	printk("%s: [AI%d] AI_STATUS = 0x%x", __func__, ai_port, val);
	saa716x_aip_stop(saa716x, ai_port);
	//SAA716x_EPWR(ai_ch[ai_port], AI_CTL, AI_RESET);
	//saa716x_aip_disable(saa716x);

	copy_bytes = len;
	if (copy_bytes > param.ai_size * 4)
		copy_bytes = param.ai_size * 4;

	read_index = saa716x->aip[ai_port].read_index;
	//read_index = 2;
	printk("%s: read_index = %d", __func__, read_index);

	//read_index = (read_index + 7) & 7;
	for(read_index = 0; read_index < AIP_BUFFERS; read_index++) {
		data = (u8 *)saa716x->aip[ai_port].dma_buf[read_index].mem_virt;
		if (copy_to_user((void __user *)(buf+num_bytes), data, copy_bytes))
		{
			err = -EFAULT;
			goto out;
		}
		num_bytes += copy_bytes;
	}

	printk("%s: %ld bytes copied to userspace buffer", __func__, num_bytes);
	return num_bytes;
out:
	return err;
}

static ssize_t saa716x_debugfs_vip_read(struct file *file, char *buf, size_t len, loff_t *off)
{   
    struct saa716x_dev *saa716x = (struct saa716x_dev*)file->private_data;
    struct vip_stream_params stream_params;
    ssize_t retval = -ENODATA;

    struct saa716x_stream *s = &saa716x->saa716x_stream[0];
    struct v4l2_subdev *sd = s->sd_receiver;
    enum saa716x_capture_subdev sd_type = saa716x->config->capture_config.subdev;
    struct v4l2_dv_timings timings = V4L2_DV_BT_CEA_1280X720P60;
    int err;

    if (s->sd_receiver) {
        err = v4l2_subdev_call(sd, video, query_dv_timings, &timings);

        if (sd_type == SAA716x_SUBDEV_TDA19978) {
            v4l2_subdev_call(sd, video, s_dv_timings, &timings);
            err = video_vip_get_stream_params_tda19978(&stream_params, &timings);
        } else {
            v4l2_subdev_call(sd, video, s_dv_timings, &timings);
            err = video_vip_get_stream_params_adv7611(&stream_params, &timings);
        }
        if (err)
            printk("%s: Unsupported dv timings.", __func__);
    } else {
        if (sd_type == SAA716x_SUBDEV_ADV7611) {
            err = video_vip_get_stream_params_adv7611(&stream_params, &timings);
        }
        if (sd_type == SAA716x_SUBDEV_ADV7611_AD9983) { 
            err = video_vip_get_stream_params_adv7611(&stream_params, &timings);
        }
    }

    retval = video_vip_read(saa716x, &stream_params, buf, len);

    u32 val, port = saa716x->config->capture_config.vip_port;
    val = SAA716x_EPRD(vi_ch[port], INT_STATUS);
    printk("%s: [VI%d] INT_STATUS=0x%x", __func__, port, val);
    val &= VI_STAT_LINE_COUNT;
    printk("%s: [VI%d] LINE_COUNT=%d", __func__, port, val >> 16);

    return retval;
}

static ssize_t saa716x_debugfs_read(struct file *file, char *buf, size_t len, loff_t *off)
{
    return saa716x_debugfs_aip_read(file, buf, len, off);
    //return saa716x_debugfs_vip_read(file, buf, len, off);
}

static ssize_t saa716x_debugfs_write(struct file *file, const char *buf, size_t len,  loff_t *off)
{  
	return 0;
}

static int saa716x_debugfs_release(struct inode *inode, struct file *file)
{   
	//kfree(file->private_data);
	struct saa716x_dev *saa716x = (struct saa716x_dev*)file->private_data;
	u32 port = saa716x->config->capture_config.vip_port;

	SAA716x_EPWR(vi_ch[port], VI_MODE, SOFT_RESET);

	printk("%s: debugfs released", __func__);
	return 0;
}

static const struct file_operations saa716x_debugfs_fops = {
	.open = saa716x_debugfs_open,
	.read = saa716x_debugfs_read,
	.write = saa716x_debugfs_write,
	.release = saa716x_debugfs_release,
	.llseek = no_llseek,
};

int saa716x_make_debugfs(struct saa716x_dev *saa716x)
{
	struct dentry *root, *file;

	root = debugfs_create_dir("saa716x", NULL);
	if (!root)
		return -ENOMEM;

	saa716x->debugfs_root = root;
	file = debugfs_create_file("debug", 0644, root, saa716x, &saa716x_debugfs_fops);
	if (!file) {
		debugfs_remove(root);
		return -ENOMEM;
	}

	return 0;
}
EXPORT_SYMBOL(saa716x_make_debugfs);

int saa716x_remove_debugfs(struct saa716x_dev *saa716x)
{
	debugfs_remove_recursive(saa716x->debugfs_root);

	return 0;
}
EXPORT_SYMBOL(saa716x_remove_debugfs);
