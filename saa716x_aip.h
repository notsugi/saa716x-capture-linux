#ifndef __SAA716x_AIP_H
#define __SAA716x_AIP_H

#include "saa716x_dma.h"

#define AIP_BUFFERS 8

struct aip_stream_params {
	u32			bits;
	u32			sample_rate;
	u32			ai_size;
};

struct saa716x_dev;
struct saa716x_dmabuf;

struct saa716x_aip_stream_port {
	u8			dma_channel;
	struct saa716x_dmabuf	dma_buf[AIP_BUFFERS];
	struct saa716x_dev	*saa716x;
	struct tasklet_struct	tasklet;
	u8			read_index;
};


extern int saa716x_aip_status(struct saa716x_dev *saa716x, u32 dev);
extern void saa716x_aip_disable(struct saa716x_dev *saa716x);

extern int saa716x_aip_get_write_index(struct saa716x_dev *saa716x, int port);
extern int saa716x_aip_start(struct saa716x_dev *saa716x, int port,
			    struct aip_stream_params *stream_params);
extern int saa716x_aip_stop(struct saa716x_dev *saa716x, int port);
extern int saa716x_aip_init(struct saa716x_dev *saa716x, int port,
				void (*worker)(unsigned long));
extern int saa716x_aip_init2(struct saa716x_dev *saa716x, int port,
				void (*worker)(unsigned long));
extern int saa716x_aip_exit(struct saa716x_dev *saa716x, int port);
extern int saa716x_aip_exit2(struct saa716x_dev *saa716x, int port);
#endif /* __SAA716x_AIP_H */
