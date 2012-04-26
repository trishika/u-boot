/*
 * Copyright (C) 2011
 * Stefan Herbrechtsmeier <stefan@code.herbrechtsmeier.net>
 *
 * Based on Kirkwood support:
 * (C) Copyright 2009
 * Marvell Semiconductor <www.marvell.com>
 * Written-by: Prafulla Wadaskar <prafulla@marvell.com>
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */

#ifndef __DNSKW_H
#define __DNSKW_H

/* GPIO configuration */
#define DNSKW_OE_LOW			0x00000000
#define DNSKW_OE_HIGH			0x00039604

#define DNSKW_GPIO_LED_POWER		26
#define DNSKW_GPIO_SATA0_EN		39
#define DNSKW_GPIO_SATA1_EN		40

/* DNS-325 specific configuration */
#ifdef CONFIG_BOARD_IS_DNS325
#define DNSKW_OE_VAL_LOW		0x38000000	/* disable leds */
#define DNSKW_OE_VAL_HIGH		0x00000800	/* disable leds */
#endif /* CONFIG_BOARD_IS_DNS325 */

/* PHY related */
#define MV88E1116_MAC_CTRL_REG		21
#define MV88E1116_PGADR_REG		22
#define MV88E1116_RGMII_TXTM_CTRL	(1 << 4)
#define MV88E1116_RGMII_RXTM_CTRL	(1 << 5)

#endif /* __DNSKW_H */
