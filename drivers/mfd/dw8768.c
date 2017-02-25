/*
 * P1 DSV  MFD Driver
 *
 * Copyright 2014 LG Electronics Inc,
 *
 * Author:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/delay.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/mfd/core.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/slab.h>

#include <linux/mfd/dw8768.h>

static struct dw8768 *dw8768_base;
static struct mfd_cell dw8768_devs[] = {
	{ .name = "dw8768_dev" },
};

/* refer to default reg. setting and add other modes if needed */
typedef enum {
	MODE_DEFAULT,
	MODE_ENMR,
	MODE_HIZ_KNOCKON,
	INVALED,
} dw8768_mode;

int dw8768_mode_change(int mode)
{
	int ret = 0;

	struct i2c_client *cl;

	cl = container_of(dw8768_base->dev, struct i2c_client, dev);
	if (cl == NULL)
		return -EINVAL;

	if (mode == MODE_DEFAULT) {
		ret += i2c_smbus_write_byte_data(cl, DW8768_DISCHARGE_STATUS_CONTROL_REG, 0x83);
		ret += i2c_smbus_write_byte_data(cl, DW8768_ENABLE_REG, 0x07);
		ret += i2c_smbus_write_byte_data(cl, DW8768_KNOCK_ON_CONTROL_REG, 0x00);
	} else if (mode == MODE_ENMR) {
		ret += i2c_smbus_write_byte_data(cl, DW8768_DISCHARGE_STATUS_CONTROL_REG, 0x83);
		ret += i2c_smbus_write_byte_data(cl, DW8768_ENABLE_REG, 0x0F);
		ret += i2c_smbus_write_byte_data(cl, DW8768_KNOCK_ON_CONTROL_REG, 0x00);
	} else if (mode == MODE_HIZ_KNOCKON) {
		ret += i2c_smbus_write_byte_data(cl, DW8768_DISCHARGE_STATUS_CONTROL_REG, 0x80);
		ret += i2c_smbus_write_byte_data(cl, DW8768_ENABLE_REG, 0x07);
		ret += i2c_smbus_write_byte_data(cl, DW8768_KNOCK_ON_CONTROL_REG, 0x08);
	}
	pr_debug("%s: lpwg mode is set [%d] \n", __func__, ret);
	return ret;
}
EXPORT_SYMBOL_GPL(dw8768_mode_change);

static struct regmap_config dw8768_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = DW8768_MAX_REGISTERS,
};

static int dw8768_probe(struct i2c_client *cl, const struct i2c_device_id *id)
{
	struct dw8768 *dw8768;
	struct device *dev = &cl->dev;
	struct dw8768_platform_data *pdata = dev_get_platdata(dev);
	int rc = 0;

	dw8768 = devm_kzalloc(dev, sizeof(*dw8768), GFP_KERNEL);
	if (!dw8768)
		return -ENOMEM;

	dw8768->pdata = pdata;

	dw8768->regmap = devm_regmap_init_i2c(cl, &dw8768_regmap_config);
	if (IS_ERR(dw8768->regmap)) {
		pr_err("Failed to allocate register map\n");
		devm_kfree(dev, dw8768);
		return PTR_ERR(dw8768->regmap);
	}

	dw8768->dev = &cl->dev;
	i2c_set_clientdata(cl, dw8768);
	dw8768_base = dw8768;

	rc = mfd_add_devices(dev, -1, dw8768_devs, ARRAY_SIZE(dw8768_devs),
			       NULL, 0, NULL);
	if (rc) {
		pr_err("Failed to add dw8768 subdevice ret=%d\n", rc);
		return -ENODEV;
	}
#if 0
	rc += i2c_smbus_write_byte_data(cl, DW8768_DISCHARGE_STATUS_CONTROL_REG, 0x80);
	rc += i2c_smbus_write_byte_data(cl, DW8768_ENABLE_REG, 0x07);
	rc += i2c_smbus_write_byte_data(cl, DW8768_KNOCK_ON_CONTROL_REG, 0x08);
	pr_info("%s: dw8768 register set done [%d] \n", __func__, rc);
	mdelay(1);
#endif
	pr_info("%s: done \n", __func__);

	return rc;
}

static int dw8768_remove(struct i2c_client *cl)
{
	struct dw8768 *dw8768 = i2c_get_clientdata(cl);

	mfd_remove_devices(dw8768->dev);

	return 0;
}

static const struct i2c_device_id dw8768_ids[] = {
	{ "dw8768", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, dw8768_ids);

#ifdef CONFIG_OF
static const struct of_device_id dw8768_of_match[] = {
	{ .compatible = "dw8768", },
	{ }
};
MODULE_DEVICE_TABLE(of, dw8768_of_match);
#endif

static struct i2c_driver dw8768_driver = {
	.driver = {
		.name = "dw8768",
		.owner = THIS_MODULE,
#ifdef CONFIG_OF
		.of_match_table = of_match_ptr(dw8768_of_match),
#endif
	},
	.id_table = dw8768_ids,
	.probe = dw8768_probe,
	.remove = dw8768_remove,
};
module_i2c_driver(dw8768_driver);

MODULE_DESCRIPTION("dw8768 MFD Core");
