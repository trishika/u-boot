/*
 * Marvell MMC/SD/SDIO driver
 *
 * (C) Copyright 2012
 * Marvell Semiconductor <www.marvell.com>
 * Written-by: Maen Suleiman, Gerald Kerma
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */

#include <common.h>
#include <malloc.h>
#include <part.h>
#include <mmc.h>
#include <asm/io.h>
#include <asm/arch/cpu.h>
#include <asm/arch/kirkwood.h>
#include <mvebu_mmc.h>

#define DRIVER_NAME "MVEBU_MMC"

static void mvebu_mmc_write(u32 offs, u32 val)
{
	writel(val, CONFIG_SYS_MMC_BASE + (offs));
}

static u32 mvebu_mmc_read(u32 offs)
{
	return readl(CONFIG_SYS_MMC_BASE + (offs));
}

static int mvebu_mmc_setup_data(struct mmc_data *data)
{
	u32 ctrl_reg;

	debug("%s, data %s : blocks=%d blksz=%d\n", DRIVER_NAME,
	      (data->flags & MMC_DATA_READ) ? "read" : "write",
	      data->blocks, data->blocksize);

	/* default to maximum timeout */
	ctrl_reg = mvebu_mmc_read(SDIO_HOST_CTRL);
	ctrl_reg |= SDIO_HOST_CTRL_TMOUT(SDIO_HOST_CTRL_TMOUT_MAX);
	mvebu_mmc_write(SDIO_HOST_CTRL, ctrl_reg);

	if (data->flags & MMC_DATA_READ) {
		mvebu_mmc_write(SDIO_SYS_ADDR_LOW, (u32)data->dest & 0xffff);
		mvebu_mmc_write(SDIO_SYS_ADDR_HI, (u32)data->dest >> 16);
	} else {
		mvebu_mmc_write(SDIO_SYS_ADDR_LOW, (u32)data->src & 0xffff);
		mvebu_mmc_write(SDIO_SYS_ADDR_HI, (u32)data->src >> 16);
	}

	mvebu_mmc_write(SDIO_BLK_COUNT, data->blocks);
	mvebu_mmc_write(SDIO_BLK_SIZE, data->blocksize);

	return 0;
}

static int mvebu_mmc_send_cmd(struct mmc *mmc, struct mmc_cmd *cmd,
			      struct mmc_data *data)
{
	int timeout = 10;
	ushort waittype = 0;
	ushort resptype = 0;
	ushort xfertype = 0;
	ushort resp_indx = 0;

	debug("cmdidx [0x%x] resp_type[0x%x] cmdarg[0x%x]\n",
	      cmd->cmdidx, cmd->resp_type, cmd->cmdarg);

	udelay(10*1000);

	debug("%s: cmd %d (hw state 0x%04x)\n", DRIVER_NAME,
	      cmd->cmdidx, mvebu_mmc_read(SDIO_HW_STATE));

	/* Checking if card is busy */
	while ((mvebu_mmc_read(SDIO_HW_STATE) & CARD_BUSY)) {
		if (timeout == 0) {
			printf("%s: card busy!\n", DRIVER_NAME);
			return -1;
		}
		timeout--;
		udelay(1000);
	}

	/* Set up for a data transfer if we have one */
	if (data) {
		int err = mvebu_mmc_setup_data(data);

		if (err)
			return err;
	}

	resptype = SDIO_CMD_INDEX(cmd->cmdidx);

	/* Analyzing resptype/xfertype/waittype for the command */
	if (cmd->resp_type & MMC_RSP_BUSY)
		resptype |= SDIO_CMD_RSP_48BUSY;
	else if (cmd->resp_type & MMC_RSP_136)
		resptype |= SDIO_CMD_RSP_136;
	else if (cmd->resp_type & MMC_RSP_PRESENT)
		resptype |= SDIO_CMD_RSP_48;
	else
		resptype |= SDIO_CMD_RSP_NONE;

	if (cmd->resp_type & MMC_RSP_CRC)
		resptype |= SDIO_CMD_CHECK_CMDCRC;

	if (cmd->resp_type & MMC_RSP_OPCODE)
		resptype |= SDIO_CMD_INDX_CHECK;

	if (cmd->resp_type & MMC_RSP_PRESENT) {
		resptype |= SDIO_UNEXPECTED_RESP;
		waittype |= SDIO_NOR_UNEXP_RSP;
	}

	if (data) {
		resptype |= SDIO_CMD_DATA_PRESENT | SDIO_CMD_CHECK_DATACRC16;
		xfertype |= SDIO_XFER_MODE_HW_WR_DATA_EN;
		if (data->flags & MMC_DATA_READ) {
			xfertype |= SDIO_XFER_MODE_TO_HOST;
			waittype = SDIO_NOR_DMA_INI;
		} else {
			waittype |= SDIO_NOR_XFER_DONE;
		}
	} else {
		waittype |= SDIO_NOR_CMD_DONE;
	}

	/* Setting cmd arguments */
	mvebu_mmc_write(SDIO_ARG_LOW, cmd->cmdarg & 0xffff);
	mvebu_mmc_write(SDIO_ARG_HI, cmd->cmdarg >> 16);

	/* Setting Xfer mode */
	mvebu_mmc_write(SDIO_XFER_MODE, xfertype);

	mvebu_mmc_write(SDIO_NOR_INTR_STATUS, ~SDIO_NOR_CARD_INT);
	mvebu_mmc_write(SDIO_ERR_INTR_STATUS, SDIO_POLL_MASK);

	/* Sending command */
	mvebu_mmc_write(SDIO_CMD, resptype);

	mvebu_mmc_write(SDIO_NOR_INTR_EN, SDIO_POLL_MASK);
	mvebu_mmc_write(SDIO_ERR_INTR_EN, SDIO_POLL_MASK);

	/* Waiting for completion */
	timeout = 1000000;

	while (!((mvebu_mmc_read(SDIO_NOR_INTR_STATUS)) & waittype)) {
		if (mvebu_mmc_read(SDIO_NOR_INTR_STATUS) & SDIO_NOR_ERROR) {
			debug("%s: error! cmdidx : %d, err reg: %04x\n",
			      DRIVER_NAME, cmd->cmdidx,
			      mvebu_mmc_read(SDIO_ERR_INTR_STATUS));
			if (mvebu_mmc_read(SDIO_ERR_INTR_STATUS) &
				(SDIO_ERR_CMD_TIMEOUT | SDIO_ERR_DATA_TIMEOUT))
				return TIMEOUT;
			return COMM_ERR;
		}

		timeout--;
		udelay(1);
		if (timeout <= 0) {
			printf("%s: command timed out\n", DRIVER_NAME);
			return TIMEOUT;
		}
	}

	/* Handling response */
	if (cmd->resp_type & MMC_RSP_136) {
		uint response[8];

		for (resp_indx = 0; resp_indx < 8; resp_indx++)
			response[resp_indx]
				= mvebu_mmc_read(SDIO_RSP(resp_indx));

		cmd->response[0] =	((response[0] & 0x03ff) << 22) |
					((response[1] & 0xffff) << 6) |
					((response[2] & 0xfc00) >> 10);
		cmd->response[1] =	((response[2] & 0x03ff) << 22) |
					((response[3] & 0xffff) << 6) |
					((response[4] & 0xfc00) >> 10);
		cmd->response[2] =	((response[4] & 0x03ff) << 22) |
					((response[5] & 0xffff) << 6) |
					((response[6] & 0xfc00) >> 10);
		cmd->response[3] =	((response[6] & 0x03ff) << 22) |
					((response[7] & 0x3fff) << 8);
	} else if (cmd->resp_type & MMC_RSP_PRESENT) {
		uint response[3];

		for (resp_indx = 0; resp_indx < 3; resp_indx++)
			response[resp_indx]
				= mvebu_mmc_read(SDIO_RSP(resp_indx));

		cmd->response[0] =	((response[2] & 0x003f) << (8 - 8)) |
					((response[1] & 0xffff) << (14 - 8)) |
					((response[0] & 0x03ff) << (30 - 8));
		cmd->response[1] =	((response[0] & 0xfc00) >> 10);
		cmd->response[2] =	0;
		cmd->response[3] =	0;
	}

	debug("%s: resp[0x%x] ", DRIVER_NAME, cmd->resp_type);
	debug("[0x%x] ", cmd->response[0]);
	debug("[0x%x] ", cmd->response[1]);
	debug("[0x%x] ", cmd->response[2]);
	debug("[0x%x] ", cmd->response[3]);
	debug("\n");

	return 0;
}

static void mvebu_mmc_power_up(void)
{
	debug("%s: power up\n", DRIVER_NAME);

	/* disable interrupts */
	mvebu_mmc_write(SDIO_NOR_INTR_EN, 0);
	mvebu_mmc_write(SDIO_ERR_INTR_EN, 0);

	/* SW reset */
	mvebu_mmc_write(SDIO_SW_RESET, SDIO_SW_RESET_NOW);

	mvebu_mmc_write(SDIO_XFER_MODE, 0);

	/* enable status */
	mvebu_mmc_write(SDIO_NOR_STATUS_EN, SDIO_POLL_MASK);
	mvebu_mmc_write(SDIO_ERR_STATUS_EN, SDIO_POLL_MASK);

	/* enable interrupts status */
	mvebu_mmc_write(SDIO_NOR_INTR_STATUS, SDIO_POLL_MASK);
	mvebu_mmc_write(SDIO_ERR_INTR_STATUS, SDIO_POLL_MASK);
}

static void mvebu_mmc_set_clk(unsigned int clock)
{
	unsigned int m;

	if (clock == 0) {
		debug("%s: clock off\n", DRIVER_NAME);
		mvebu_mmc_write(SDIO_XFER_MODE, SDIO_XFER_MODE_STOP_CLK);
		mvebu_mmc_write(SDIO_CLK_DIV, MVEBU_MMC_BASE_DIV_MAX);
	} else {
		m = MVEBU_MMC_BASE_FAST_CLOCK/(2*clock) - 1;
		if (m > MVEBU_MMC_BASE_DIV_MAX)
			m = MVEBU_MMC_BASE_DIV_MAX;
		mvebu_mmc_write(SDIO_CLK_DIV, m & MVEBU_MMC_BASE_DIV_MAX);
	}

	udelay(10*1000);
}

static void mvebu_mmc_set_bus(unsigned int bus)
{
	u32 ctrl_reg = 0;

	ctrl_reg = mvebu_mmc_read(SDIO_HOST_CTRL);
	ctrl_reg &= ~SDIO_HOST_CTRL_DATA_WIDTH_4_BITS;

	switch (bus) {
	case 4:
		ctrl_reg |= SDIO_HOST_CTRL_DATA_WIDTH_4_BITS;
		break;
	case 1:
	default:
		ctrl_reg |= SDIO_HOST_CTRL_DATA_WIDTH_1_BIT;
	}

	/* default transfer mode */
	ctrl_reg |= SDIO_HOST_CTRL_BIG_ENDIAN;
	ctrl_reg &= ~SDIO_HOST_CTRL_LSB_FIRST;

	/* default to maximum timeout */
	ctrl_reg |= SDIO_HOST_CTRL_TMOUT(SDIO_HOST_CTRL_TMOUT_MAX);

	ctrl_reg |= SDIO_HOST_CTRL_PUSH_PULL_EN;

	ctrl_reg |= SDIO_HOST_CTRL_CARD_TYPE_MEM_ONLY;

	debug("%s: ctrl 0x%04x: %s %s %s\n", DRIVER_NAME, ctrl_reg,
	      (ctrl_reg & SDIO_HOST_CTRL_PUSH_PULL_EN) ?
	      "push-pull" : "open-drain",
	      (ctrl_reg & SDIO_HOST_CTRL_DATA_WIDTH_4_BITS) ?
	      "4bit-width" : "1bit-width",
	      (ctrl_reg & SDIO_HOST_CTRL_HI_SPEED_EN) ?
	      "high-speed" : "");

	mvebu_mmc_write(SDIO_HOST_CTRL, ctrl_reg);
	udelay(10*1000);
}

static void mvebu_mmc_set_ios(struct mmc *mmc)
{
	debug("%s: bus[%d] clock[%d]\n", DRIVER_NAME,
	      mmc->bus_width, mmc->clock);
	mvebu_mmc_set_bus(mmc->bus_width);
	mvebu_mmc_set_clk(mmc->clock);
}

static int mvebu_mmc_initialize(struct mmc *mmc)
{
	debug("%s: mvebu_mmc_initialize", DRIVER_NAME);

	/*
	 * Setting host parameters
	 * Initial Host Ctrl : Timeout : max , Normal Speed mode,
	 * 4-bit data mode, Big Endian, SD memory Card, Push_pull CMD Line
	 */
	mvebu_mmc_write(SDIO_HOST_CTRL,
			SDIO_HOST_CTRL_TMOUT(SDIO_HOST_CTRL_TMOUT_MAX) |
			SDIO_HOST_CTRL_DATA_WIDTH_4_BITS |
			SDIO_HOST_CTRL_BIG_ENDIAN |
			SDIO_HOST_CTRL_PUSH_PULL_EN |
			SDIO_HOST_CTRL_CARD_TYPE_MEM_ONLY);

	mvebu_mmc_write(SDIO_CLK_CTRL, 0);

	/* enable status */
	mvebu_mmc_write(SDIO_NOR_STATUS_EN, SDIO_POLL_MASK);
	mvebu_mmc_write(SDIO_ERR_STATUS_EN, SDIO_POLL_MASK);

	/* disable interrupts */
	mvebu_mmc_write(SDIO_NOR_INTR_EN, 0);
	mvebu_mmc_write(SDIO_ERR_INTR_EN, 0);

	/* SW reset */
	mvebu_mmc_write(SDIO_SW_RESET, SDIO_SW_RESET_NOW);

	udelay(10*1000);

	return 0;
}

static const struct mmc_ops mvebu_mmc_ops = {
	.send_cmd	= mvebu_mmc_send_cmd,
	.set_ios	= mvebu_mmc_set_ios,
	.init		= mvebu_mmc_initialize,
};

static struct mmc_config mvebu_mmc_cfg = {
	.name		= DRIVER_NAME,
	.ops		= &mvebu_mmc_ops,
	.f_min		= MVEBU_MMC_BASE_FAST_CLOCK / MVEBU_MMC_BASE_DIV_MAX,
	.f_max		= MVEBU_MMC_CLOCKRATE_MAX,
	.voltages	= MMC_VDD_32_33 | MMC_VDD_33_34,
	.host_caps	= MMC_MODE_4BIT | MMC_MODE_HS,
	.part_type	= PART_TYPE_DOS,
	.b_max		= CONFIG_SYS_MMC_MAX_BLK_COUNT,
};

int mvebu_mmc_init(bd_t *bis)
{
	struct mmc *mmc;

	mvebu_mmc_power_up();

	mmc = mmc_create(&mvebu_mmc_cfg, bis);
	if (mmc == NULL)
		return -1;

	return 0;
}
