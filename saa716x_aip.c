#include <linux/kernel.h>

#include "saa716x_mod.h"

#include "saa716x_aip_reg.h"
#include "saa716x_dma_reg.h"
#include "saa716x_msi_reg.h"

#include "saa716x_priv.h"

static const u32 ai_ch[] = {
	AI0,
	AI1
};

static const u32 msi_int_tagack[] = {
	MSI_INT_TAGACK_AI_0,
	MSI_INT_TAGACK_AI_1
};

static const u32 msi_int_avint[] = {
	MSI_INT_AVINT_AI_0,
	MSI_INT_AVINT_AI_1
};

int saa716x_aip_get_write_index(struct saa716x_dev *saa716x, int port)
{
	u32 buf_mode, val;

	buf_mode = BAM_DMA_BUF_MODE(saa716x->aip[port].dma_channel);

	val = SAA716x_EPRD(BAM, buf_mode);
	return (val >> 3) & 0x7;
}
EXPORT_SYMBOL_GPL(saa716x_aip_get_write_index);

static int saa716x_aip_init_ptables(struct saa716x_dmabuf *dmabuf, int channel)
{
	struct saa716x_dev *saa716x = dmabuf->saa716x;
	u32 config, i;

	for (i = 0; i < AIP_BUFFERS; i++)
		BUG_ON((dmabuf[i].mem_ptab_phys == 0));

	config = MMU_DMA_CONFIG(channel); /* DMACONFIGx */

	SAA716x_EPWR(MMU, config, (AIP_BUFFERS - 1));

	SAA716x_EPWR(MMU, MMU_PTA0_LSB(channel), PTA_LSB(dmabuf[0].mem_ptab_phys)); /* Low */
	SAA716x_EPWR(MMU, MMU_PTA0_MSB(channel), PTA_MSB(dmabuf[0].mem_ptab_phys)); /* High */
	SAA716x_EPWR(MMU, MMU_PTA1_LSB(channel), PTA_LSB(dmabuf[1].mem_ptab_phys)); /* Low */
	SAA716x_EPWR(MMU, MMU_PTA1_MSB(channel), PTA_MSB(dmabuf[1].mem_ptab_phys)); /* High */
	SAA716x_EPWR(MMU, MMU_PTA2_LSB(channel), PTA_LSB(dmabuf[2].mem_ptab_phys)); /* Low */
	SAA716x_EPWR(MMU, MMU_PTA2_MSB(channel), PTA_MSB(dmabuf[2].mem_ptab_phys)); /* High */
	SAA716x_EPWR(MMU, MMU_PTA3_LSB(channel), PTA_LSB(dmabuf[3].mem_ptab_phys)); /* Low */
	SAA716x_EPWR(MMU, MMU_PTA3_MSB(channel), PTA_MSB(dmabuf[3].mem_ptab_phys)); /* High */
	SAA716x_EPWR(MMU, MMU_PTA4_LSB(channel), PTA_LSB(dmabuf[4].mem_ptab_phys)); /* Low */
	SAA716x_EPWR(MMU, MMU_PTA4_MSB(channel), PTA_MSB(dmabuf[4].mem_ptab_phys)); /* High */
	SAA716x_EPWR(MMU, MMU_PTA5_LSB(channel), PTA_LSB(dmabuf[5].mem_ptab_phys)); /* Low */
	SAA716x_EPWR(MMU, MMU_PTA5_MSB(channel), PTA_MSB(dmabuf[5].mem_ptab_phys)); /* High */
	SAA716x_EPWR(MMU, MMU_PTA6_LSB(channel), PTA_LSB(dmabuf[6].mem_ptab_phys)); /* Low */
	SAA716x_EPWR(MMU, MMU_PTA6_MSB(channel), PTA_MSB(dmabuf[6].mem_ptab_phys)); /* High */
	SAA716x_EPWR(MMU, MMU_PTA7_LSB(channel), PTA_LSB(dmabuf[7].mem_ptab_phys)); /* Low */
	SAA716x_EPWR(MMU, MMU_PTA7_MSB(channel), PTA_MSB(dmabuf[7].mem_ptab_phys)); /* High */
	
	return 0;
}

static int saa716x_aip_setparams(struct saa716x_dev *saa716x, int port,
			struct aip_stream_params *stream_params)
{
	u32 ai_port, buf_mode, val, i;
	u8 dma_channel;
	u32 base_address, base_offset;
	u32 ain_ctl = 0;

	ai_port = ai_ch[port];
	dma_channel = saa716x->aip[port].dma_channel;
	buf_mode = BAM_DMA_BUF_MODE(dma_channel);

	/* Reset DMA channel */
	SAA716x_EPWR(BAM, buf_mode, 0x00000040);
	saa716x_aip_init_ptables(saa716x->aip[port].dma_buf, dma_channel);
	
	base_address = saa716x->aip[port].dma_channel << 21;
	base_offset = 0;
	
	/* set device to normal operation */
	SAA716x_EPWR(ai_port, AI_PWR_DOWN, 0);
	
	SAA716x_EPWR(ai_port, AI_FRAMING, 0x0200);
	//SAA716x_EPWR(ai_port, AI_SIZE, 0x0640); // TDA19978
	//SAA716x_EPWR(ai_port, AI_SIZE, 0x05c0); // ADV7611
	/* s16le 2ch = 4bytes per sample
	 * 0x5c0 * 4 = 0x1700 bytes written in each buffer
	 * 0x640 * 4 = 0x1900 bytes
	 */
	SAA716x_EPWR(ai_port, AI_SIZE, stream_params->ai_size);

	SAA716x_EPWR(ai_port, AI_BASE1, base_address);
	SAA716x_EPWR(ai_port, AI_BASE2, base_address);

	/* monitor BAM reset */
	i = 0;
	val = SAA716x_EPRD(BAM, buf_mode);
	while (val && (i < 100)) {
		msleep(30);
		val = SAA716x_EPRD(BAM, buf_mode);
		i++;
	}
	if (val) {
		dprintk(SAA716x_ERROR, 1, "Error: BAM AIP Reset failed!");
		return -EIO;
	}

	/* set buffer count */
	SAA716x_EPWR(BAM, buf_mode, AIP_BUFFERS - 1);
	/* initialize all available address offsets to 0 */
	SAA716x_EPWR(BAM, BAM_ADDR_OFFSET_0(dma_channel), 0x0);
	SAA716x_EPWR(BAM, BAM_ADDR_OFFSET_1(dma_channel), 0x0);
	SAA716x_EPWR(BAM, BAM_ADDR_OFFSET_2(dma_channel), 0x0);
	SAA716x_EPWR(BAM, BAM_ADDR_OFFSET_3(dma_channel), 0x0);
	SAA716x_EPWR(BAM, BAM_ADDR_OFFSET_4(dma_channel), 0x0);
	SAA716x_EPWR(BAM, BAM_ADDR_OFFSET_5(dma_channel), 0x0);
	SAA716x_EPWR(BAM, BAM_ADDR_OFFSET_6(dma_channel), 0x0);
	SAA716x_EPWR(BAM, BAM_ADDR_OFFSET_7(dma_channel), 0x0);

	return 0;
}

int saa716x_aip_start(struct saa716x_dev *saa716x, int port,
			struct aip_stream_params *stream_params)
{
	u32 ai_port;
	u32 config;
	u32 ain_ctl;
	u32 val;
	u32 i;

	ai_port = ai_ch[port];
	config = MMU_DMA_CONFIG(saa716x->aip[port].dma_channel);
	
	if (saa716x_aip_setparams(saa716x, port, stream_params) != 0) {
		return -EIO;
	}

	val = SAA716x_EPRD(MMU, config);
	SAA716x_EPWR(MMU, config, val & ~0x40);
	SAA716x_EPWR(MMU, config, val | 0x40);

	ain_ctl = SAA716x_EPRD(ai_port, AI_CTL);
	ain_ctl |= AI_OVR_INTEN;
	ain_ctl |= AI_HBE_INTEN;
	ain_ctl |= AI_BUF2_INTEN | AI_BUF1_INTEN;
	SAA716x_EPWR(ai_port, AI_CTL, ain_ctl);

	i = 0;
	while (i < 500) {
		val = SAA716x_EPRD(MMU, config);
		if (val & 0x80)
			break;
		msleep(10);
		i++;
	}

	if (!(val & 0x80)) {
		dprintk(SAA716x_ERROR, 1, "Error: PTE pre-fetch failed!");
		return -EIO;
	}

	/* enable audio capture path */
	ain_ctl = SAA716x_EPRD(ai_port, AI_CTL);
	ain_ctl |= AI_CAP_ENABLE | AI_CAP_MODE;
	
	saa716x_set_clk_external(saa716x, saa716x->aip[port].dma_channel);
	
	SAA716x_EPWR(ai_port, AI_CTL, ain_ctl);
	printk("%s: [AI%d] AI_CTL=0x%x", __func__, port, ain_ctl);

	SAA716x_EPWR(MSI, MSI_INT_ENA_SET_L, msi_int_tagack[port]);

	return 0;
}
EXPORT_SYMBOL_GPL(saa716x_aip_start);

int saa716x_aip_stop(struct saa716x_dev *saa716x, int port)
{
	u32 val;

	SAA716x_EPWR(MSI, MSI_INT_ENA_CLR_L, msi_int_tagack[port]);

	/* disable capture */
	val = SAA716x_EPRD(ai_ch[port], AI_CTL);
	val &= ~AI_CAP_ENABLE;
	SAA716x_EPWR(ai_ch[port], AI_CTL, val);
	saa716x_set_clk_internal(saa716x, saa716x->aip[port].dma_channel);
	saa716x->aip[port].read_index = 0;
	
	return 0;
}
EXPORT_SYMBOL_GPL(saa716x_aip_stop);

int saa716x_aip_init(struct saa716x_dev *saa716x, int port,
		     void (*worker)(unsigned long))
{
	int i;
	int ret;

	/* reset AI */
	SAA716x_EPWR(ai_ch[port], AI_CTL, AI_RESET);

	saa716x->aip[port].dma_channel = 10 + port;
	for (i = 0; i < AIP_BUFFERS; i++) {
		saa716x->aip[port].dma_buf[i].saa716x = saa716x;
		ret = saa716x_allocate_ptable(&saa716x->aip[port].dma_buf[i]);
		if (ret)
			return ret;
	}

	saa716x->aip[port].saa716x = saa716x;
	tasklet_init(&saa716x->aip[port].tasklet, worker,
		     (unsigned long)&saa716x->aip[port]);
	saa716x->aip[port].read_index = 0;

	printk("%s: AI%d initialized", __func__, port);

	return 0;
}
EXPORT_SYMBOL_GPL(saa716x_aip_init);

int saa716x_aip_init2(struct saa716x_dev *saa716x, int port,
		     void (*worker)(unsigned long))
{
	int i;
	int ret;

	/* reset AI */
	SAA716x_EPWR(ai_ch[port], AI_CTL, AI_RESET);

	saa716x->aip[port].dma_channel = 10 + port;
	for (i = 0; i < AIP_BUFFERS; i++) {
		ret = saa716x_dmabuf_alloc(
				saa716x,
				&saa716x->aip[port].dma_buf[i],
				512 * SAA716x_PAGE_SIZE);
		if (ret < 0) {
			return ret;
		}
	}

	saa716x->aip[port].saa716x = saa716x;
	tasklet_init(&saa716x->aip[port].tasklet, worker,
		     (unsigned long)&saa716x->aip[port]);
	saa716x->aip[port].read_index = 0;

	printk("%s: AI%d initialized", __func__, port);

	return 0;
}
EXPORT_SYMBOL_GPL(saa716x_aip_init2);

int saa716x_aip_exit(struct saa716x_dev *saa716x, int port)
{
	int i;

	tasklet_kill(&saa716x->aip[port].tasklet);
	for (i = 0; i < AIP_BUFFERS; i++) {
		saa716x_free_ptable(&saa716x->aip[port].dma_buf[i]);
	}
	return 0;
}
EXPORT_SYMBOL_GPL(saa716x_aip_exit);

int saa716x_aip_exit2(struct saa716x_dev *saa716x, int port)
{
	int i;

	tasklet_kill(&saa716x->aip[port].tasklet);
	for (i = 0; i < AIP_BUFFERS; i++) {
		saa716x_dmabuf_free(saa716x, &saa716x->aip[port].dma_buf[i]);
	}
	return 0;
}
EXPORT_SYMBOL_GPL(saa716x_aip_exit2);

int saa716x_aip_status(struct saa716x_dev *saa716x, u32 dev)
{
	return SAA716x_EPRD(dev, AI_CTL) == 0 ? 0 : -1;
}
EXPORT_SYMBOL_GPL(saa716x_aip_status);

void saa716x_aip_disable(struct saa716x_dev *saa716x)
{
	SAA716x_EPWR(AI0, AI_PWR_DOWN, AI_PWR_DWN);
	SAA716x_EPWR(AI1, AI_PWR_DOWN, AI_PWR_DWN);
}
EXPORT_SYMBOL_GPL(saa716x_aip_disable);
