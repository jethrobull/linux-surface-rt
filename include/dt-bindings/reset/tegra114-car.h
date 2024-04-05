/* SPDX-License-Identifier: GPL-2.0 */
/*
 * This header provides Tegra114-specific constants for binding
 * nvidia,tegra114-car.
 */

#ifndef _DT_BINDINGS_RESET_TEGRA114_CAR_H
#define _DT_BINDINGS_RESET_TEGRA114_CAR_H

#define TEGRA114_RESET(x)		(6 * 32 + (x))
#define TEGRA114_RST_DFLL_DVCO		TEGRA114_RESET(0)

#endif	/* _DT_BINDINGS_RESET_TEGRA114_CAR_H */
