#include <linux/kernel.h>

#include "saa716x_mod.h"
#include "saa716x_aip_reg.h"
#include "saa716x_spi.h"
#include "saa716x_aip.h"
#include "saa716x_priv.h"

/*
int saa716x_aip_get_write_index(struct saa716x_dev *saa716x, int port);

static int saa716x_aip_init_ptables(struct saa716x_dmabuf *dmabuf, int channel,
			struct vip_stream_params *stream_params);
static int saa716x_aip_setparams(struct saa716x_dev *saa716x, int port,
			struct vip_stream_params *stream_params)

int saa716x_aip_start(struct saa716x_dev *saa716x, int port, int one_shot,
			struct vip_stream_params *stream_params);
int saa716x_aip_stop(struct saa716x_dev *saa716x, int port);

int saa716x_aip_init(struct saa716x_dev *saa716x, int port);
int saa716x_aip_exit(struct saa716x_dev *saa716x, int port);
*/

int saa716x_aip_status(struct saa716x_dev *saa716x, u32 dev)
{
	return SAA716x_EPRD(dev, AI_CTL) == 0 ? 0 : -1;
}
EXPORT_SYMBOL_GPL(saa716x_aip_status);

void saa716x_aip_disable(struct saa716x_dev *saa716x)
{
	SAA716x_EPWR(AI0, AI_CTL, 0x00);
	SAA716x_EPWR(AI1, AI_CTL, 0x00);
}
EXPORT_SYMBOL_GPL(saa716x_aip_disable);
