// SPDX-License-Identifier: GPL-2.0-only
/*
 * Tegra114 DFLL FCPU clock source driver
 *
 * Copyright (C) 2012-2019 NVIDIA Corporation.  All rights reserved.
 *
 * Aleksandr Frid <afrid@nvidia.com>
 * Paul Walmsley <pwalmsley@nvidia.com>
 */

#include <linux/cpu.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <soc/tegra/fuse.h>

#include "clk.h"
#include "clk-dfll.h"
#include "cvb.h"

struct dfll_fcpu_data {
	const unsigned long *cpu_max_freq_table;
	unsigned int cpu_max_freq_table_size;
	const struct cvb_table *cpu_cvb_tables;
	unsigned int cpu_cvb_tables_size;
};

/* Maximum CPU frequency, indexed by CPU speedo id */
static const unsigned long tegra114_cpu_max_freq_table[] = {
	[0] = 1914500000UL,
	[1] = 1920500000UL,
	[2] = 1916500000UL,
	[3] = 1924500000UL,

//	[0] = 2014500000UL,
//	[1] = 2320500000UL,
//	[2] = 2316500000UL,
//	[3] = 2524500000UL,
};

static const struct cvb_table tegra114_cpu_cvb_tables[] = {
	{
		.speedo_id = -1,
		.process_id = -1,
		.min_millivolts = 800,
		.max_millivolts = 1400,
		.speedo_scale = 100,
		.voltage_scale = 1000,
		.entries = {
			{  204000000UL, { 1112619, -29295, 402 } },
			{  306000000UL, { 1150460, -30585, 402 } },
			{  408000000UL, { 1190122, -31865, 402 } },
			{  510000000UL, { 1231606, -33155, 402 } },
			{  612000000UL, { 1274912, -34435, 402 } },
			{  714000000UL, { 1320040, -35725, 402 } },
			{  816000000UL, { 1366990, -37005, 402 } },
			{  918000000UL, { 1415762, -38295, 402 } },
			{ 1020000000UL, { 1466355, -39575, 402 } },
			{ 1122000000UL, { 1518771, -40865, 402 } },
			{ 1224000000UL, { 1573009, -42145, 402 } },
			{ 1326000000UL, { 1629068, -43435, 402 } },
			{ 1428000000UL, { 1686950, -44715, 402 } },
			{ 1530000000UL, { 1746653, -46005, 402 } },
			{ 1632000000UL, { 1808179, -47285, 402 } },
			{ 1734000000UL, { 1871526, -48575, 402 } },
			{ 1836000000UL, { 1936696, -49855, 402 } },
			{ 1938000000UL, { 2003687, -51145, 402 } },
			{ 2014500000UL, { 2054787, -52095, 402 } },
			{ 2116500000UL, { 2124957, -53385, 402 } },
			{ 2218500000UL, { 2196950, -54665, 402 } },
			{ 2320500000UL, { 2270765, -55955, 402 } },
			{ 2422500000UL, { 2346401, -57235, 402 } },
			{ 2524500000UL, { 2437299, -58535, 402 } },
			{          0UL, {       0,      0,   0 } },
		},
		.cpu_dfll_data = {
			.tune0_low = 0x005020ff,
			.tune0_high = 0x005040ff,
			.tune1 = 0x00000060,
		}
	},
};

static const struct dfll_fcpu_data tegra114_dfll_fcpu_data = {
	.cpu_max_freq_table = tegra114_cpu_max_freq_table,
	.cpu_max_freq_table_size = ARRAY_SIZE(tegra114_cpu_max_freq_table),
	.cpu_cvb_tables = tegra114_cpu_cvb_tables,
	.cpu_cvb_tables_size = ARRAY_SIZE(tegra114_cpu_cvb_tables)
};

static const struct of_device_id tegra114_dfll_fcpu_of_match[] = {
	{
		.compatible = "nvidia,tegra114-dfll",
		.data = &tegra114_dfll_fcpu_data,
	},
	{ },
};

static void get_alignment_from_dt(struct device *dev,
				  struct rail_alignment *align)
{
	if (of_property_read_u32(dev->of_node,
				 "nvidia,pwm-voltage-step-microvolts",
				 &align->step_uv))
		align->step_uv = 0;

	if (of_property_read_u32(dev->of_node,
				 "nvidia,pwm-min-microvolts",
				 &align->offset_uv))
		align->offset_uv = 0;
}

static int get_alignment_from_regulator(struct device *dev,
					 struct rail_alignment *align)
{
	struct regulator *reg = regulator_get(dev, "vdd-cpu");

	if (IS_ERR(reg))
		return PTR_ERR(reg);

	align->offset_uv = regulator_list_voltage(reg, 0);
	align->step_uv = regulator_get_linear_step(reg);




	regulator_put(reg);

	return 0;
}

static int tegra114_dfll_fcpu_probe(struct platform_device *pdev)
{
	int process_id, speedo_id, speedo_value, err;
	struct tegra_dfll_soc_data *soc;
	const struct dfll_fcpu_data *fcpu_data;
	struct rail_alignment align;

	fcpu_data = of_device_get_match_data(&pdev->dev);
	if (!fcpu_data)
		return -ENODEV;

	process_id = tegra_sku_info.cpu_process_id;
	speedo_id = tegra_sku_info.cpu_speedo_id;
	speedo_value = tegra_sku_info.cpu_speedo_value;

	if (speedo_id >= fcpu_data->cpu_max_freq_table_size) {
		dev_err(&pdev->dev, "unknown max CPU freq for speedo_id=%d\n",
			speedo_id);
		return -ENODEV;
	}

	soc = devm_kzalloc(&pdev->dev, sizeof(*soc), GFP_KERNEL);
	if (!soc)
		return -ENOMEM;

	soc->dev = get_cpu_device(0);
	if (!soc->dev) {
		dev_err(&pdev->dev, "no CPU0 device\n");
		return -ENODEV;
	}

	if (of_property_read_bool(pdev->dev.of_node, "nvidia,pwm-to-pmic")) {
		get_alignment_from_dt(&pdev->dev, &align);
	} else {
		err = get_alignment_from_regulator(&pdev->dev, &align);
		if (err)
			return err;
	}

	soc->max_freq = fcpu_data->cpu_max_freq_table[speedo_id];

	soc->cvb = tegra_cvb_add_opp_table(soc->dev, fcpu_data->cpu_cvb_tables,
					   fcpu_data->cpu_cvb_tables_size,
					   &align, process_id, speedo_id,
					   speedo_value, soc->max_freq);
	soc->alignment = align;

	if (IS_ERR(soc->cvb)) {
		dev_err(&pdev->dev, "couldn't add OPP table: %ld\n",
			PTR_ERR(soc->cvb));
		return PTR_ERR(soc->cvb);
	}

	err = tegra_dfll_register(pdev, soc);
	if (err < 0) {
		tegra_cvb_remove_opp_table(soc->dev, soc->cvb, soc->max_freq);
		return err;
	}

	return 0;
}

static void tegra114_dfll_fcpu_remove(struct platform_device *pdev)
{
	struct tegra_dfll_soc_data *soc;

	/*
	 * Note that exiting early here is dangerous as after this function
	 * returns *soc is freed.
	 */
	soc = tegra_dfll_unregister(pdev);
	if (IS_ERR(soc))
		return;

	tegra_cvb_remove_opp_table(soc->dev, soc->cvb, soc->max_freq);
}

static const struct dev_pm_ops tegra114_dfll_pm_ops = {
	SET_RUNTIME_PM_OPS(tegra_dfll_runtime_suspend,
			   tegra_dfll_runtime_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(tegra_dfll_suspend, tegra_dfll_resume)
};

static struct platform_driver tegra114_dfll_fcpu_driver = {
	.probe = tegra114_dfll_fcpu_probe,
	.remove_new = tegra114_dfll_fcpu_remove,
	.driver = {
		.name = "tegra114-dfll",
		.of_match_table = tegra114_dfll_fcpu_of_match,
		.pm = &tegra114_dfll_pm_ops,
	},
};
builtin_platform_driver(tegra114_dfll_fcpu_driver);
