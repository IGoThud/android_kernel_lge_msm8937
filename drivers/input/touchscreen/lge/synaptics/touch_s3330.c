/* touch_synaptics.c
 *
 * Copyright (C) 2015 LGE.
 *
 * Author: hoyeon.jang@lge.com
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/irq.h>

/*
 *  Include to touch core Header File
 */
#include <touch_hwif.h>
#include <touch_core.h>

/*
 *  Include to Local Header File
 */
#include "touch_s3330.h"
#include "touch_s3330_prd.h"
#include "touch_s3330_f54_test.h"

/*
 * PLG591 - PH2
 */

const char *f_str[] = {
	"ERROR",
	"DISTANCE_INTER_TAP",
	"DISTANCE_TOUCHSLOP",
	"TIMEOUT_INTER_TAP",
	"MULTI_FINGER",
	"DELAY_TIME"
};

static struct synaptics_exp_fhandler rmidev_fhandler;

bool synaptics_is_product(struct synaptics_data *d,
				const char *product_id, size_t len)
{
	return strncmp(d->fw.product_id, product_id, len)
			? false : true;
}

bool synaptics_is_img_product(struct synaptics_data *d,
				const char *product_id, size_t len)
{
	return strncmp(d->fw.img_product_id, product_id, len)
			? false : true;
}

int synaptics_read(struct device *dev, u16 addr, void *data, int size)
{
	struct touch_core_data *ts = to_touch_core(dev);
	struct touch_bus_msg msg;
	int ret = 0;

	ts->tx_buf[0] = addr;

	msg.tx_buf = ts->tx_buf;
	msg.tx_size = 1;

	msg.rx_buf = ts->rx_buf;
	msg.rx_size = size;

	ret = touch_bus_read(dev, &msg);

	if (ret < 0) {
		TOUCH_E("touch bus read error : %d\n", ret);
		return ret;
	}

	memcpy(data, &ts->rx_buf[0], size);
	return 0;
}

int synaptics_write(struct device *dev, u16 addr, void *data, int size)
{
	struct touch_core_data *ts = to_touch_core(dev);
	struct touch_bus_msg msg;
	int ret = 0;

	ts->tx_buf[0] = addr;
	memcpy(&ts->tx_buf[1], data, size);

	msg.tx_buf = ts->tx_buf;
	msg.tx_size = size+1;
	msg.rx_buf = NULL;
	msg.rx_size = 0;

	ret = touch_bus_write(dev, &msg);

	if (ret < 0) {
		TOUCH_E("touch bus write error : %d\n", ret);
		return ret;
	}

	return 0;
}

static int synaptics_irq_enable(struct device *dev, bool enable)
{
	struct synaptics_data *d = to_synaptics_data(dev);

	u8 val;
	int ret;

	ret = synaptics_read(dev, INTERRUPT_ENABLE_REG, &val, sizeof(val));
	if (ret < 0) {
		TOUCH_E("failed to read interrupt enable - ret:%d\n", ret);
		return ret;
	}

	if (enable)
		val |= (INTERRUPT_MASK_ABS0 | INTERRUPT_MASK_CUSTOM);
	else
		val &= ~INTERRUPT_MASK_ABS0;


	ret = synaptics_write(dev, INTERRUPT_ENABLE_REG, &val, sizeof(val));
	if (ret < 0) {
		TOUCH_E("failed to write interrupt enable - ret:%d\n", ret);
		return ret;
	}

	TOUCH_I("write interrupt : enable:%d, val:%02X\n", enable, val);

	return 0;
}

int synaptics_set_page(struct device *dev, u8 page)
{
	int ret = synaptics_write(dev, PAGE_SELECT_REG, &page, 1);

	if (ret >= 0)
		to_synaptics_data(dev)->curr_page = page;

	return ret;
}

static int synaptics_get_f12(struct device *dev)
{
	struct synaptics_data *d = to_synaptics_data(dev);

	u8 query_5_data[5];
	u8 query_8_data[3];
	u8 ctrl_23_data[2];
	u8 ctrl_8_data[14];

	u32 query_5_present = 0;
	u16 query_8_present = 0;

	u8 offset;
	int i;
	int ret;

	ret = synaptics_read(dev, d->f12.dsc.query_base + 5,
			     query_5_data, sizeof(query_5_data));

	if (ret < 0) {
		TOUCH_E("faied to get query5 (ret: %d)\n", ret);
		return ret;
	}

	query_5_present = (query_5_present << 8) | query_5_data[4];
	query_5_present = (query_5_present << 8) | query_5_data[3];
	query_5_present = (query_5_present << 8) | query_5_data[2];
	query_5_present = (query_5_present << 8) | query_5_data[1];
	TOUCH_I("qeury_5_present=0x%08X [%02X %02X %02X %02X %02X]\n",
			query_5_present, query_5_data[0], query_5_data[1],
			query_5_data[2], query_5_data[3], query_5_data[4]);

	for (i = 0, offset = 0; i < 32; i++) {
		d->f12_reg.ctrl[i] = d->f12.dsc.control_base + offset;

		if (query_5_present & (1 << i)) {
			TOUCH_I("f12_reg.ctrl[%d]=0x%02X (0x%02x+%d)\n",
					i, d->f12_reg.ctrl[i],
					d->f12.dsc.control_base, offset);
			offset++;
		}
	}

	ret = synaptics_read(dev, d->f12.dsc.query_base + 8,
			query_8_data, sizeof(query_8_data));

	if (ret < 0) {
		TOUCH_E("faied to get query8 (ret: %d)\n", ret);
		return ret;
	}

	query_8_present = (query_8_present << 8) | query_8_data[2];
	query_8_present = (query_8_present << 8) | query_8_data[1];
	TOUCH_I("qeury_8_present=0x%08X [%02X %02X %02X]\n",
			query_8_present, query_8_data[0],
			query_8_data[1], query_8_data[2]);

	for (i = 0, offset = 0; i < 16; i++) {
		d->f12_reg.data[i] = d->f12.dsc.data_base + offset;

		if (query_8_present & (1 << i)) {
			TOUCH_I("d->f12_reg.data[%d]=0x%02X (0x%02x+%d)\n",
					i, d->f12_reg.data[i],
					d->f12.dsc.data_base, offset);
			offset++;
		}
	}

	ret = synaptics_read(dev, d->f12_reg.ctrl[23],
			     ctrl_23_data, sizeof(ctrl_23_data));

	if (ret < 0) {
		TOUCH_E("faied to get f12_ctrl32_data (ret: %d)\n", ret);
		return ret;
	}

	d->object_report = ctrl_23_data[0];
	d->num_of_fingers = min_t(u8, ctrl_23_data[1], (u8) MAX_NUM_OF_FINGERS);

	TOUCH_I("object_report[0x%02X], num_of_fingers[%d]\n",
			d->object_report, d->num_of_fingers);

	ret = synaptics_read(dev, d->f12_reg.ctrl[8],
			     ctrl_8_data, sizeof(ctrl_8_data));

	if (ret < 0) {
		TOUCH_E("faied to get f12_ctrl8_data (ret: %d)\n", ret);
		return ret;
	}

	TOUCH_I("ctrl_8-sensor_max_x[%d], sensor_max_y[%d]\n",
			((u16)ctrl_8_data[0] << 0) |
			((u16)ctrl_8_data[1] << 8),
			((u16)ctrl_8_data[2] << 0) |
			((u16)ctrl_8_data[3] << 8));

	return 0;
}

static int synaptics_page_description(struct device *dev)
{
	struct synaptics_data *d = to_synaptics_data(dev);
	struct function_descriptor dsc;
	u8 page;

	unsigned short pdt;
	int ret;

	TOUCH_TRACE();

	memset(&d->f01, 0, sizeof(struct synaptics_function));
	memset(&d->f11, 0, sizeof(struct synaptics_function));
	memset(&d->f12, 0, sizeof(struct synaptics_function));
	memset(&d->f1a, 0, sizeof(struct synaptics_function));
	memset(&d->f34, 0, sizeof(struct synaptics_function));
	memset(&d->f51, 0, sizeof(struct synaptics_function));
	memset(&d->f54, 0, sizeof(struct synaptics_function));
	memset(&d->f55, 0, sizeof(struct synaptics_function));

	for (page = 0; page < PAGES_TO_SERVICE; page++) {
		ret = synaptics_set_page(dev, page);

		if (ret < 0) {
			TOUCH_E("faied to set page %d (ret: %d)\n", page, ret);
			return ret;
		}

		for (pdt = PDT_START; pdt > PDT_END; pdt -= sizeof(dsc)) {
			ret = synaptics_read(dev, pdt, &dsc, sizeof(dsc));

			if (ret < 0) {
				TOUCH_E("read descrptore %d (ret: %d)\n",
					pdt, ret);
				return ret;
			}

			if (!dsc.fn_number)
				break;

			TOUCH_I("dsc - %02x, %02x, %02x, %02x, %02x, %02x\n",
				dsc.query_base, dsc.command_base,
				dsc.control_base, dsc.data_base,
				dsc.int_source_count, dsc.fn_number);

			switch (dsc.fn_number) {
			case 0x01:
				d->f01.dsc = dsc;
				d->f01.page = page;
				break;

			case 0x11:
				d->f11.dsc = dsc;
				d->f11.page = page;
				break;

			case 0x12:
				d->f12.dsc = dsc;
				d->f12.page = page;
				synaptics_get_f12(dev);
				break;

			case 0x1a:
				d->f1a.dsc = dsc;
				d->f1a.page = page;
				break;

			case 0x34:
				d->f34.dsc = dsc;
				d->f34.page = page;
				break;

			case 0x51:
				d->f51.dsc = dsc;
				d->f51.page = page;
				break;

			case 0x54:
				d->f54.dsc = dsc;
				d->f54.page = page;
				break;

			default:
				break;
			}
		}
	}

	TOUCH_D(BASE_INFO,
		"common[%dP:0x%02x] finger_f12[%dP:0x%02x] flash[%dP:0x%02x] analog[%dP:0x%02x] lpwg[%dP:0x%02x]\n",
		d->f01.page, d->f01.dsc.fn_number,
		d->f12.page, d->f12.dsc.fn_number,
		d->f34.page, d->f34.dsc.fn_number,
		d->f54.page, d->f54.dsc.fn_number,
		d->f51.page, d->f51.dsc.fn_number);

	if (!(d->f01.dsc.fn_number &&
	      d->f12.dsc.fn_number &&
	      d->f34.dsc.fn_number &&
	      d->f54.dsc.fn_number &&
	      d->f51.dsc.fn_number))
		return -EINVAL;

	ret = synaptics_set_page(dev, 0);

	if (ret) {
		TOUCH_E("faied to set page %d (ret: %d)\n", 0, ret);
		return ret;
	}

	return 0;
}

static int synaptics_get_product_id(struct device *dev)
{
	struct synaptics_data *d = to_synaptics_data(dev);

	int ret = 0;
	u8 temp_pid[11] = {0,};

	TOUCH_TRACE();

	ret = synaptics_read(dev, PRODUCT_ID_REG,
			d->fw.product_id, sizeof(d->fw.product_id) - 1);

	if (ret < 0) {
		TOUCH_I("[%s]read error...\n", __func__);
		return ret;
	}

	TOUCH_I("[%s] IC_product_id: %s\n",
			__func__, d->fw.product_id);

	if (synaptics_is_product(d, "S3330", 5)) {
		ret = synaptics_read(dev, FLASH_CONFIG_ID_REG,
				temp_pid, sizeof(temp_pid) - 1);

		if (ret < 0) {
			TOUCH_I("[%s]read error...\n", __func__);
			return ret;
		}

		memset(d->fw.product_id, 0,
				sizeof(d->fw.product_id));
		memcpy(d->fw.product_id, &temp_pid[4], 6);

		TOUCH_I("[%s] Product_ID_Reset ! , addr = 0x%x, P_ID = %s\n",
			__func__, FLASH_CONFIG_ID_REG, d->fw.product_id);
	} else {
		return -EINVAL;
	}

	return 0;
}

int synaptics_ic_info(struct device *dev)
{
	struct synaptics_data *d = to_synaptics_data(dev);

	int ret;

	if (d->need_scan_pdt == true) {
		ret = synaptics_page_description(dev);
		SCAN_PDT(dev);
		d->need_scan_pdt = false;
	}

	synaptics_set_page(dev, DEFAULT_PAGE);

	ret = synaptics_get_product_id(dev);
	ret = synaptics_read(dev, FLASH_CONFIG_ID_REG,
			d->fw.version, sizeof(d->fw.version) - 1);
	ret = synaptics_read(dev, CUSTOMER_FAMILY_REG,
			&(d->fw.family), sizeof(d->fw.family));
	ret = synaptics_read(dev, FW_REVISION_REG,
			&(d->fw.revision), sizeof(d->fw.revision));

	if (ret < 0) {
		TOUCH_I("[%s]read error...\n", __func__);
		return ret;
	}

	TOUCH_I("ic_version = V%d.%02d\n", d->fw.version[3] & 0x80 ? 1 : 0,
			d->fw.version[3] & 0x7F);
	TOUCH_I("CUSTOMER_FAMILY_REG = %d\n", d->fw.family);
	TOUCH_I("FW_REVISION_REG = %d\n", d->fw.revision);
	TOUCH_I("PRODUCT ID = %s\n", d->fw.product_id);

	return 0;
}

static int synaptics_sleep_control(struct device *dev, u8 mode)
{
	struct synaptics_data *d = to_synaptics_data(dev);

	u8 val;
	int ret;

	ret = synaptics_read(dev, DEVICE_CONTROL_REG, &val, sizeof(val));

	if (ret < 0) {
		TOUCH_E("failed to read finger report enable - ret:%d\n", ret);
		return ret;
	}

	val &= 0xf8;

	if (mode)
		val |= 1;
	else
		val |= 0;

	ret = synaptics_write(dev, DEVICE_CONTROL_REG, &val, sizeof(val));
	if (ret < 0) {
		TOUCH_E("failed to write finger report enable - ret:%d\n", ret);
		return ret;
	}

	TOUCH_I("%s - mode:%d\n", __func__, mode);

	return 0;
}

static int synaptics_lpwg_debug(struct device *dev, int num)
{
	struct synaptics_data *d = to_synaptics_data(dev);

	u8 count = 0;
	u8 index = 0;
	u8 buf = 0;
	u8 i = 0;
	u8 addr = 0;
	u8 offset = num ? LPWG_MAX_BUFFER + 2 : 0;

	synaptics_set_page(dev, LPWG_PAGE);

	synaptics_read(dev, LPWG_TCI1_FAIL_COUNT_REG + offset, &count, sizeof(count));
	synaptics_read(dev, LPWG_TCI1_FAIL_INDEX_REG + offset, &index, sizeof(index));

	for (i = 1; i <= count; i++) {
		addr = LPWG_TCI1_FAIL_BUFFER_REG + offset +
			((index + LPWG_MAX_BUFFER - i) % LPWG_MAX_BUFFER);
		synaptics_read(dev, addr, &buf, sizeof(buf));
		TOUCH_I("TCI(%d)-Fail[%d/%d] : %s\n", num, count - i + 1, count,
			(buf > 0 && buf < 6) ? f_str[buf] : f_str[0]);

		if (i == LPWG_MAX_BUFFER)
			break;
	}

	synaptics_set_page(dev, DEFAULT_PAGE);

	return 0;
}

static int synaptics_tci_report_enable(struct device *dev, bool enable)
{
	struct synaptics_data *d = to_synaptics_data(dev);

	u8 val[3];
	int ret;

	synaptics_irq_enable(dev, enable ? false : true);

	ret = synaptics_read(dev, FINGER_REPORT_REG, val, sizeof(val));
	if (ret < 0) {
		TOUCH_E("failed to read finger report enable - ret:%d\n", ret);
		return ret;
	}

	val[2] &= 0xfc;

	if (enable)
		val[2] |= 0x2;

	ret = synaptics_write(dev, FINGER_REPORT_REG, val, sizeof(val));
	if (ret < 0) {
		TOUCH_E("failed to write finger report enable - ret:%d\n", ret);
		return ret;
	}

	TOUCH_I("%s - enable:%d\n", __func__, enable);

	return 0;
}

static int synaptics_tci_knock(struct device *dev, u8 value)
{
	struct touch_core_data *ts = to_touch_core(dev);
	struct synaptics_data *d = to_synaptics_data(dev);
	struct tci_info *info;

	u8 lpwg_data[7];
	int ret;

	TOUCH_TRACE();

	ret = synaptics_set_page(dev, LPWG_PAGE);

	if (ret < 0) {
		TOUCH_E("failed to set page to LPWG_PAGE\n");
		return ret;
	}

	ret = synaptics_read(dev, LPWG_TAPCOUNT_REG,
			      lpwg_data, sizeof(lpwg_data));

	TOUCH_I("0 : %d,%d,%d,%d,%d,%d,%d\n",
		lpwg_data[0],
		lpwg_data[1],
		lpwg_data[2],
		lpwg_data[3],
		lpwg_data[4],
		lpwg_data[5],
		lpwg_data[6]);

	info = &ts->tci.info[0];

	info->tap_count &= 0xf;
	info->intr_delay = 0;
	info->tap_distance = 10;

	lpwg_data[0] = ((info->tap_count << 3) | 1);
	lpwg_data[1] = info->min_intertap;
	lpwg_data[2] = info->max_intertap;
	lpwg_data[3] = info->touch_slop;
	lpwg_data[4] = info->tap_distance;
	lpwg_data[6] = info->intr_delay;

	if (!value)
		lpwg_data[0] &= 0xfe;

	ret = synaptics_write(dev, LPWG_TAPCOUNT_REG,
			      lpwg_data, sizeof(lpwg_data));

	ret = synaptics_read(dev, LPWG_TAPCOUNT_REG2,
			      &lpwg_data[0], sizeof(u8));

	TOUCH_I("1 : %d\n", lpwg_data[0]);

	lpwg_data[0] &= 0xfe;

	ret = synaptics_write(dev, LPWG_TAPCOUNT_REG2,
			      lpwg_data, sizeof(u8));
	if (ret < 0) {
		TOUCH_E("failed to write LPWG_TAPCOUNT_REG2\n");
		return ret;
	}

	ret = synaptics_set_page(dev, DEFAULT_PAGE);

	if (ret < 0) {
		TOUCH_E("failed to set page to DEFAULT_PAGE\n");
		return ret;
	}

	return ret;
}

static int synaptics_tci_password(struct device *dev)
{
	struct touch_core_data *ts = to_touch_core(dev);
	struct synaptics_data *d = to_synaptics_data(dev);
	struct tci_info *info;

	u8 lpwg_data[7];
	int ret;

	TOUCH_TRACE();

	ret = synaptics_set_page(dev, LPWG_PAGE);

	if (ret < 0) {
		TOUCH_E("failed to set page to LPWG_PAGE\n");
		return ret;
	}

	ret = synaptics_read(dev, LPWG_TAPCOUNT_REG,
			      lpwg_data, sizeof(lpwg_data));

	TOUCH_I("0: %d,%d,%d,%d,%d,%d,%d\n",
		lpwg_data[0],
		lpwg_data[1],
		lpwg_data[2],
		lpwg_data[3],
		lpwg_data[4],
		lpwg_data[5],
		lpwg_data[6]);

	info = &ts->tci.info[0];

	info->tap_count &= 0xf;
	info->intr_delay = ts->tci.double_tap_check ? 68 : 0;
	info->tap_distance = 7;

	lpwg_data[0] = ((info->tap_count << 3) | 1);
	lpwg_data[1] = info->min_intertap;
	lpwg_data[2] = info->max_intertap;
	lpwg_data[3] = info->touch_slop;
	lpwg_data[4] = info->tap_distance;
	lpwg_data[6] = (info->intr_delay << 1 | 1);

	ret = synaptics_write(dev, LPWG_TAPCOUNT_REG,
			      lpwg_data, sizeof(lpwg_data));

	ret = synaptics_read(dev, LPWG_TAPCOUNT_REG2,
			      lpwg_data, sizeof(lpwg_data));

	TOUCH_I("1: %d,%d,%d,%d,%d,%d,%d\n",
		lpwg_data[0],
		lpwg_data[1],
		lpwg_data[2],
		lpwg_data[3],
		lpwg_data[4],
		lpwg_data[5],
		lpwg_data[6]);

	info = &ts->tci.info[1];
	info->tap_count &= 0xf;
	lpwg_data[0] = ((info->tap_count << 3) | 1);
	lpwg_data[1] = info->min_intertap;
	lpwg_data[2] = info->max_intertap;
	lpwg_data[3] = info->touch_slop;
	lpwg_data[4] = info->tap_distance;
	lpwg_data[6] = (info->intr_delay << 1 | 1);

	ret = synaptics_write(dev, LPWG_TAPCOUNT_REG2,
			      lpwg_data, sizeof(lpwg_data));

	if (ret < 0) {
		TOUCH_E("failed to write LPWG_TAPCOUNT_REG2\n");
		return ret;
	}

	ret = synaptics_set_page(dev, DEFAULT_PAGE);

	if (ret < 0) {
		TOUCH_E("failed to set page to DEFAULT_PAGE\n");
		return ret;
	}

	return ret;
}

static int synaptics_tci_active_area(struct device *dev)
{
	struct touch_core_data *ts = to_touch_core(dev);
	struct synaptics_data *d = to_synaptics_data(dev);
	u8 buffer[8];

	buffer[0] = (ts->lpwg.area[0].x >> 0) & 0xff;
	buffer[1] = (ts->lpwg.area[0].x >> 8) & 0xff;
	buffer[2] = (ts->lpwg.area[0].y >> 0) & 0xff;
	buffer[3] = (ts->lpwg.area[0].y >> 8) & 0xff;
	buffer[4] = (ts->lpwg.area[1].x >> 0) & 0xff;
	buffer[5] = (ts->lpwg.area[1].x >> 8) & 0xff;
	buffer[6] = (ts->lpwg.area[1].y >> 0) & 0xff;
	buffer[7] = (ts->lpwg.area[1].y >> 8) & 0xff;

	synaptics_write(dev, d->f12_reg.ctrl[18], buffer, sizeof(buffer));

	return 0;
}

static int synaptics_cover_set(struct device *dev)
{
	struct touch_core_data *ts = to_touch_core(dev);
	struct synaptics_data *d = to_synaptics_data(dev);

	u8 buf_1 = 0, buf_2 = 0;
	u8 official = 0;
	u8 version = 0;
	int ret = 0;

	if (ts->factory_boot)
		return ret;

	official = d->fw.version[3] & 0x80 ? 1 : 0;
	version = d->fw.version[3] & 0x7F;

	if (((official == 1) && (version < 5)) || ((official == 0) && (version < 18))) {
		TOUCH_I("Not support f/w to cover setting\n");
		return ret;
	}

	ret = synaptics_read(dev, OBJECT_REPORT_ENABLE_REG, &buf_1, sizeof(buf_1));
	if (ret < 0) {
		TOUCH_E("failed to read OBJECT_REPORT_ENABLE_REG\n");
		return ret;
	}

	ret = synaptics_read(dev, FEATURE_ENABLE_REG, &buf_2, sizeof(buf_2));
	if (ret < 0) {
		TOUCH_E("failed to read FEATURE_ENABLE_REG\n");
		return ret;
	}

	if (ts->lpwg.qcover == QUICKCOVER_CLOSE) {
		TOUCH_I("QUICKCOVER_CLOSE\n");
		buf_1 |= 0x20;
		buf_2 |= 0x03;
	} else {
		TOUCH_I("QUICKCOVER_OPEN\n");
		buf_1 &= 0xDF;
		buf_2 &= 0xFC;
	}

	ret = synaptics_write(dev, OBJECT_REPORT_ENABLE_REG, &buf_1, sizeof(buf_1));
	if (ret < 0) {
		TOUCH_E("failed to write OBJECT_REPORT_ENABLE_REG\n");
		return ret;
	}

	ret = synaptics_write(dev, FEATURE_ENABLE_REG, &buf_2, sizeof(buf_2));
	if (ret < 0) {
		TOUCH_E("failed to write FEATURE_ENABLE_REG\n");
		return ret;
	}

	return 0;
}

static int synaptics_lpwg_control(struct device *dev, int mode)
{
	struct touch_core_data *ts = to_touch_core(dev);

	TOUCH_I("synaptics_lpwg_control mode = %d\n", mode);

	switch (mode) {
	case LPWG_DOUBLE_TAP:
		ts->tci.mode = 0x01;
		synaptics_tci_knock(dev, ts->tci.mode);
		break;

	case LPWG_PASSWORD:
		ts->tci.mode = 0x02;
		synaptics_tci_password(dev);
		break;

	default:
		ts->tci.mode = 0;
		synaptics_tci_knock(dev, ts->tci.mode);
		break;
	}

	return 0;
}

static int synaptics_lpwg_mode(struct device *dev)
{
	struct touch_core_data *ts = to_touch_core(dev);

	if (atomic_read(&ts->state.fb) == FB_SUSPEND) {
		if (ts->role.mfts_lpwg) {
			synaptics_sleep_control(dev, 0);
			synaptics_lpwg_control(dev, LPWG_DOUBLE_TAP);
			return 0;
		}
		if (ts->lpwg.sensor == PROX_NEAR) {
			/* deep sleep - Proxy Near */
			TOUCH_I("%s(%d) - deep sleep by prox\n",
				__func__, __LINE__);
			synaptics_sleep_control(dev, 1);
			synaptics_lpwg_control(dev, LPWG_NONE);
		} else if (ts->lpwg.screen) {
			TOUCH_I("%s(%d) - FB_SUSPEND & screen on -> skip\n",
				__func__, __LINE__);
			synaptics_sleep_control(dev, 0);
			synaptics_tci_report_enable(dev, false);
			if (!ts->factory_boot) {
				if (ts->lpwg.mode == 1)
					synaptics_lpwg_debug(dev, 0);
				else if (ts->lpwg.mode == 2)
					synaptics_lpwg_debug(dev, 1);
			}
			return 0;
		} else if (ts->lpwg.mode == LPWG_NONE) {
			/* deep sleep - LPWG Disable */
			TOUCH_I("%s(%d) - deep sleep by knock on disable\n",
				__func__, __LINE__);
			synaptics_sleep_control(dev, 1);
			synaptics_lpwg_control(dev, LPWG_NONE);
		} else if (ts->lpwg.qcover == HOLE_NEAR) {
			/* folio cover - LPWG Disable */
			TOUCH_I("%s(%d) - qcover close\n",
				__func__, __LINE__);
			synaptics_sleep_control(dev, 1);
		} else {
			/* knock on/code */
			TOUCH_I("%s(%d) - knock %d, screen %d, proxy %d, qcover %d\n",
				__func__, __LINE__,
				ts->lpwg.mode, ts->lpwg.screen, ts->lpwg.sensor, ts->lpwg.qcover);
			synaptics_sleep_control(dev, 0);
			synaptics_lpwg_control(dev, ts->lpwg.mode);
		}
		return 0;
	}

	/* resume */
	touch_report_all_event(ts);

	if (ts->lpwg.screen) {
		/* normal */
		TOUCH_I("%s(%d) - normal\n",
				__func__, __LINE__);
		synaptics_lpwg_control(dev, LPWG_NONE);
		synaptics_cover_set(dev);
	} else if (ts->lpwg.mode == LPWG_NONE) {
		/* normal */
		TOUCH_I("%s(%d) - normal on screen off\n",
				__func__, __LINE__);
		synaptics_lpwg_control(dev, LPWG_NONE);
	} else if (ts->lpwg.sensor == PROX_NEAR) {
		/* wake up */
		TOUCH_I("%s(%d) - wake up on screen off and prox\n",
				__func__, __LINE__);
		TOUCH_I("%s - wake up is not ready\n", __func__);
		synaptics_lpwg_control(dev, LPWG_NONE);
	} else {
		/* partial */
		TOUCH_I("%s(%d) - partial mode(knock %d, screen %d, proxy %d, qcover %d)\n",
				__func__, __LINE__,
				ts->lpwg.mode, ts->lpwg.screen, ts->lpwg.sensor, ts->lpwg.qcover);
		TOUCH_I("%s - partial is not ready\n", __func__);
		synaptics_lpwg_control(dev, LPWG_NONE);
	}

	return 0;
}

static void synaptics_init_tci_info(struct device *dev)
{
	struct touch_core_data *ts = to_touch_core(dev);

	ts->tci.info[0].tap_count = 2;
	ts->tci.info[0].min_intertap = 4;
	ts->tci.info[0].max_intertap = 70;
	ts->tci.info[0].touch_slop = 100;
	ts->tci.info[0].tap_distance = 10;
	ts->tci.info[0].intr_delay = 0;

	ts->tci.info[1].min_intertap = 4;
	ts->tci.info[1].max_intertap = 70;
	ts->tci.info[1].touch_slop = 100;
	ts->tci.info[1].tap_distance = 255;
	ts->tci.info[1].intr_delay = 68;
}

static int synaptics_remove(struct device *dev)
{
	if (rmidev_fhandler.initialized
		&& rmidev_fhandler.inserted) {
		rmidev_fhandler.exp_fn->remove(dev);
		rmidev_fhandler.initialized = false;
	}

	return 0;
}

static int synaptics_get_status(struct device *dev, u8 *device, u8 *interrupt)
{
	struct synaptics_data *d = to_synaptics_data(dev);
	u8 dev_status;
	u8 irq_status;
	int ret;

	ret = synaptics_read(dev, DEVICE_STATUS_REG,
			&dev_status, sizeof(dev_status));

	if (ret < 0) {
		TOUCH_E("failed to read device status - ret:%d\n", ret);
		return ret;
	}

	ret = synaptics_read(dev, INTERRUPT_STATUS_REG,
			&irq_status, sizeof(irq_status));

	if (ret < 0) {
		TOUCH_E("failed to read interrupt status - ret:%d\n", ret);
		return ret;
	}

	TOUCH_D(TRACE, "status[device:%02x, interrupt:%02x]\n",
		dev_status, irq_status);

	if (device)
		*device = dev_status;

	if (interrupt)
		*interrupt = irq_status;

	return ret;
}

static int synaptics_irq_clear(struct device *dev)
{
	return synaptics_get_status(dev, NULL, NULL);
}

static int synaptics_noise_log(struct device *dev)
{
	struct touch_core_data *ts = to_touch_core(dev);
	struct synaptics_data *d = to_synaptics_data(dev);

	u8 buffer[2] = {0};
	u8 buf_lsb = 0, buf_msb = 0, cns = 0;
	u16 im = 0, cid_im = 0, freq_scan_im = 0;
	int i = 0;

	synaptics_set_page(dev, ANALOG_PAGE);
	synaptics_read(dev, INTERFERENCE_METRIC_LSB_REG, &buf_lsb, sizeof(buf_lsb));
	synaptics_read(dev, INTERFERENCE_METRIC_MSB_REG, &buf_msb, sizeof(buf_msb));

	im = (buf_msb << 8) | buf_lsb;
	d->noise.im_sum += im;

	synaptics_read(dev, CURRENT_NOISE_STATUS_REG, &cns, sizeof(cns));
	d->noise.cns_sum += cns;

	synaptics_read(dev, CID_IM_REG, buffer, sizeof(buffer));
	cid_im = (buffer[1] << 8) | buffer[0];
	d->noise.cid_im_sum += cid_im;

	synaptics_read(dev, FREQ_SCAN_IM_REG, buffer, sizeof(buffer));
	freq_scan_im = (buffer[1] << 8) | buffer[0];
	d->noise.freq_scan_im_sum += freq_scan_im;
	synaptics_set_page(dev, DEFAULT_PAGE);

	d->noise.cnt++;

	if (d->noise.noise_log == NOISE_ENABLE) {
		if (ts->old_mask != ts->new_mask) {
			TOUCH_I("Curr : CNS[%5d] IM[%5d] CID_IM[%5d] FREQ_SCAN_IM[%5d]\n",
					cns, im, cid_im, freq_scan_im);
		}
	}

	if ((i < MAX_NUM_OF_FINGERS && ts->new_mask == 0)
				|| (d->noise.im_sum >= ULONG_MAX
				|| d->noise.cns_sum >= ULONG_MAX
				|| d->noise.cid_im_sum >= ULONG_MAX
				|| d->noise.freq_scan_im_sum >= ULONG_MAX
				|| d->noise.cnt >= UINT_MAX)) {
		if (d->noise.noise_log == NOISE_ENABLE) {
			TOUCH_I("Aver : CNS[%5lu] IM[%5lu] CID_IM[%5lu] FREQ_SCAN_IM[%5lu] (cnt:%u)\n",
				d->noise.cns_sum / d->noise.cnt,
				d->noise.im_sum / d->noise.cnt,
				d->noise.cid_im_sum / d->noise.cnt,
				d->noise.freq_scan_im_sum / d->noise.cnt,
				d->noise.cnt);
		}

		d->noise.im_avg = d->noise.im_sum / d->noise.cnt;
		d->noise.cns_avg = d->noise.cns_sum / d->noise.cnt;
		d->noise.cid_im_avg = d->noise.cid_im_sum / d->noise.cnt;
		d->noise.freq_scan_im_avg = d->noise.freq_scan_im_sum / d->noise.cnt;
	}

	if (ts->old_mask == 0 && ts->new_mask != 0) {
		d->noise.cnt = d->noise.im_sum = d->noise.cns_sum =
			d->noise.cid_im_sum = d->noise.freq_scan_im_sum = 0;
		d->noise.im_avg = d->noise.cns_avg =
			d->noise.cid_im_avg = d->noise.freq_scan_im_avg = 0;
	}

	return 0;
}

static int synaptics_get_object_count(struct device *dev, u8 *object)
{
	struct synaptics_data *d = to_synaptics_data(dev);

	u8 object_to_read = d->num_of_fingers;
	u8 buf[2] = {0,};
	u16 object_attention = 0;
	int ret;

	ret = synaptics_read(dev, d->f12_reg.data[15],
			(u8 *) buf, sizeof(buf));

	if (ret < 0) {
		TOUCH_E("%s, %d : get object_attention data failed\n",
			__func__, __LINE__);
		return ret;
	}

        object_attention = (((u16)((buf[1] << 8) & 0xFF00) | (u16)((buf[0])&0xFF)));
	
	for (; object_to_read > 0 ;) {
		if (object_attention & (0x1 << (object_to_read - 1)))
			break;
        else
		    object_to_read--;
	}

	*object = object_to_read;
	return 0;
}

static int synaptics_irq_abs(struct device *dev)
{
	struct touch_core_data *ts = to_touch_core(dev);
	struct synaptics_data *d = to_synaptics_data(dev);
	struct synaptics_object objects[MAX_NUM_OF_FINGERS];
	struct synaptics_object *obj;
	struct touch_data *tdata;
	u8 i;
	u8 finger_index;
	u8 object_to_read = 0;
	int ret;

	ts->new_mask = 0;
	ret = synaptics_get_object_count(dev, &object_to_read);

	if (ret < 0) {
		TOUCH_E("faied to read object count\n");
		return ret;
	}

	TOUCH_D(ABS, "object_to_read: %d\n", object_to_read);

	if (object_to_read > 0) {
		ret = synaptics_read(dev, FINGER_DATA_REG,
				     objects, sizeof(*obj) * object_to_read);
		if (ret < 0) {
			TOUCH_E("faied to read finger data\n");
			return ret;
		}

		finger_index = 0;

		for (i = 0; i < object_to_read; i++) {
			obj = objects + i;

			if (obj->type == F12_NO_OBJECT_STATUS)
				continue;

			if (obj->type > F12_MAX_OBJECT)
				TOUCH_D(ABS, "id : %d, type : %d\n",
					i, obj->type);

			if (obj->type == F12_FINGER_STATUS) {
				ts->new_mask |= (1 << i);
				tdata = ts->tdata + i;

				tdata->id = i;
				tdata->type = obj->type;
				tdata->x = obj->x_lsb | obj->x_msb << 8;
				tdata->y = obj->y_lsb | obj->y_msb << 8;
				tdata->pressure = obj->z;

				if (obj->wx > obj->wy) {
					tdata->width_major = obj->wx;
					tdata->width_minor = obj->wy;
					tdata->orientation = 0;
				} else {
					tdata->width_major = obj->wy;
					tdata->width_minor = obj->wx;
					tdata->orientation = 1;
				}

				finger_index++;

				TOUCH_D(ABS,
					"tdata [id:%d t:%d x:%d y:%d z:%d-%d,%d,%d]\n",
					tdata->id,
					tdata->type,
					tdata->x,
					tdata->y,
					tdata->pressure,
					tdata->width_major,
					tdata->width_minor,
					tdata->orientation);
			}
		}

		ts->tcount = finger_index;
	}

	if (d->noise.check_noise == NOISE_ENABLE
		|| d->noise.noise_log == NOISE_ENABLE) {
		synaptics_noise_log(dev);
	}

	ts->intr_status |= TOUCH_IRQ_FINGER;

	return 0;
}

static int synaptics_tci_getdata(struct device *dev, int count)
{
	struct touch_core_data *ts = to_touch_core(dev);
	struct synaptics_data *d = to_synaptics_data(dev);
	u32 buffer[12];
	int i = 0;
	int ret;

	ts->lpwg.code_num = count;

	if (!count)
		return 0;

	ret = synaptics_read(dev, LPWG_DATA_REG,
					 buffer, sizeof(u32) * count);

	if (ret < 0)
		return ret;

	for (i = 0; i < count; i++) {
		ts->lpwg.code[i].x = buffer[i] & 0xffff;
		ts->lpwg.code[i].y = (buffer[i] >> 16) & 0xffff;

		TOUCH_I("LPWG data %d, %d\n",
			ts->lpwg.code[i].x, ts->lpwg.code[i].y);
	}

	ts->lpwg.code[i].x = -1;
	ts->lpwg.code[i].y = -1;

	return 0;
}

static int synaptics_irq_lpwg(struct device *dev)
{
	struct touch_core_data *ts = to_touch_core(dev);
	struct synaptics_data *d = to_synaptics_data(dev);
	u8 status;
	u8 buffer;
	int ret;

	ret = synaptics_set_page(dev, LPWG_PAGE);

	if (ret < 0)
		return ret;

	ret = synaptics_read(dev, LPWG_STATUS_REG, &status, 1);
	if (ret < 0)
		return ret;

	if (status & LPWG_STATUS_DOUBLETAP) {
		synaptics_tci_getdata(dev, ts->tci.info[0].tap_count);
		ts->intr_status |= TOUCH_IRQ_KNOCK;
	} else if (status & LPWG_STATUS_PASSWORD) {
		synaptics_tci_getdata(dev, ts->tci.info[1].tap_count);
		ts->intr_status |= TOUCH_IRQ_PASSWD;
	} else {
		/* Overtab */
		synaptics_read(dev, LPWG_OVER_TAPCOUNT, &buffer, 1);
		if (buffer > ts->tci.info[1].tap_count) {
			ts->lpwg.code_num = 1;
			synaptics_tci_getdata(dev, ts->lpwg.code_num);
			ts->intr_status |= TOUCH_IRQ_PASSWD;
			TOUCH_I("knock code fail to over tap count = %d\n", buffer);
		}
	}

	return synaptics_set_page(dev, DEFAULT_PAGE);
}

static int synaptics_check_data(struct device *dev)
{
        struct synaptics_data *d = to_synaptics_data(dev);
        int ret = 0;
        u8 read_reg = 0;
        u8 check_reset = 0;
        u8 check_palm = 0;
        u8 palm_old_mask = d->palm_old_mask;
        u8 palm_change_mask = 0;
        u8 official = 0;
        u8 version = 0;

        /* version check */
        official = d->fw.version[3] & 0x80 ? 1 : 0;
        version = d->fw.version[3] & 0x7F;
        if (((official == 1) && (version < 5)) || ((official == 0) && (version < 18))) {
                /* Not support to check palm status */
                return ret;
        }

        ret = synaptics_set_page(dev, LPWG_PAGE);
        if (ret < 0) {
                TOUCH_E("faied to set lpwg page\n");
                return ret;
        }

        ret = synaptics_read(dev, CHECK_DEBUG_DATA, &read_reg, sizeof(read_reg));
        if (ret < 0) {
                TOUCH_E("faied to get check debug data (ret: %d)\n", ret);
                return ret;
        }

        /* check reset */
        check_reset = read_reg & 0x01;
        if (check_reset > 0) {
                TOUCH_E("request reset from fw, because of tx error\n");
        }

        /* check palm */
        check_palm  = (read_reg >> 1) & 0x01;
        palm_change_mask = palm_old_mask ^ check_palm;

        if (palm_change_mask && check_palm == 1) {
                TOUCH_I("press palm\n");
        } else if (palm_change_mask && check_palm == 0) {
                TOUCH_I("release palm\n");
        }
        d->palm_old_mask = check_palm;

        ret = synaptics_set_page(dev, DEFAULT_PAGE);
        if (ret < 0) {
                TOUCH_E("faied to set default page\n");
                return ret;
        }
        return ret;
}

static int synaptics_irq_handler(struct device *dev)
{
	u8 dev_status;
	u8 irq_status;
	int ret;

	TOUCH_TRACE();

	ret = synaptics_check_data(dev);

	ret = synaptics_get_status(dev, &dev_status, &irq_status);

	if (irq_status & INTERRUPT_MASK_ABS0)
		ret = synaptics_irq_abs(dev);
	else if (irq_status & INTERRUPT_MASK_LPWG)
		ret = synaptics_irq_lpwg(dev);

	return ret;
}

void synaptics_rmidev_function(struct synaptics_exp_fn *rmidev_fn,
		bool insert)
{
	rmidev_fhandler.inserted = insert;

	if (insert) {
		rmidev_fhandler.exp_fn = rmidev_fn;
		rmidev_fhandler.inserted = true;
		rmidev_fhandler.initialized = false;
	} else {
		rmidev_fhandler.exp_fn = NULL;
		rmidev_fhandler.inserted = false;
		rmidev_fhandler.initialized = true;
	}

	return;
}

static int synaptics_rmidev_init(struct device *dev)
{
	int ret = 0;

	if (rmidev_fhandler.inserted) {
		ret = rmidev_fhandler.exp_fn->init(dev);

		if (ret < 0)
			TOUCH_E("Failed to rmi_dev init\n");
		else
			rmidev_fhandler.initialized = true;
	}

	return 0;
}

int synaptics_init(struct device *dev)
{
	struct touch_core_data *ts = to_touch_core(dev);

	synaptics_ic_info(dev);

	synaptics_lpwg_mode(dev);

	synaptics_irq_enable(dev, true);
	synaptics_irq_clear(dev);

	if (!ts->factory_boot)
		synaptics_rmidev_init(dev);

	return 0;
}

static int synaptics_power(struct device *dev, int ctrl)
{
	struct touch_core_data *ts = to_touch_core(dev);

	TOUCH_TRACE();

	switch (ctrl) {
	case POWER_OFF:
		TOUCH_I("%s, off\n", __func__);
		touch_gpio_direction_output(ts->reset_pin, 0);
		touch_power_vio(dev, 0);
		touch_power_vdd(dev, 0);
		break;

	case POWER_ON:
		TOUCH_I("%s, on\n", __func__);
		touch_power_vdd(dev, 1);
		touch_power_vio(dev, 1);
		touch_gpio_direction_output(ts->reset_pin, 1);
		break;


	case POWER_SLEEP:
		TOUCH_I("%s, sleep\n", __func__);
		break;

	case POWER_WAKE:
		TOUCH_I("%s, wake\n", __func__);
		break;
	}

	return 0;
}

static int synaptics_suspend(struct device *dev)
{
	struct touch_core_data *ts = to_touch_core(dev);

	int mfts_mode = lge_get_mfts_mode();

	if (touch_boot_mode() == TOUCH_CHARGER_MODE) {
		TOUCH_I("%s : Charger mode!!!\n", __func__);
		return 0;
	}

	if (mfts_mode && !ts->role.mfts_lpwg) {
		TOUCH_I("%s : touch_suspend - MFTS!!!\n", __func__);
		return 0;
	}

	synaptics_lpwg_mode(dev);

	return 0;
}

static int synaptics_resume(struct device *dev)
{
	struct touch_core_data *ts = to_touch_core(dev);
	struct synaptics_data *d = to_synaptics_data(dev);

	int mfts_mode = lge_get_mfts_mode();

	if (touch_boot_mode() == TOUCH_CHARGER_MODE) {
		TOUCH_I("%s : Charger mode!!!\n", __func__);
		/* Deep Sleep */
		synaptics_sleep_control(dev, 1);
		return -EPERM;
	}

	if (mfts_mode && !ts->role.mfts_lpwg) {
		TOUCH_I("%s, MFTS Touch power on\n", __func__);
		d->need_scan_pdt = true;
	}

	touch_gpio_direction_output(ts->reset_pin, 0);
	touch_gpio_direction_output(ts->reset_pin, 1);
	touch_msleep(ts->caps.hw_reset_delay);

	return 0;
}

static int synaptics_lpwg(struct device *dev, u32 code, void *param)
{
	struct touch_core_data *ts = to_touch_core(dev);

	int *value = (int *)param;

	TOUCH_TRACE();

	switch (code) {
	case LPWG_ACTIVE_AREA:
		ts->lpwg.area[0].x = value[0];
		ts->lpwg.area[0].y = value[2];
		ts->lpwg.area[1].x = value[1];
		ts->lpwg.area[1].y = value[3];
		TOUCH_I("LPWG AREA (%d,%d)-(%d,%d)\n",
			ts->lpwg.area[0].x, ts->lpwg.area[0].y,
			ts->lpwg.area[1].x, ts->lpwg.area[1].y);
		synaptics_tci_active_area(dev);
		break;

	case LPWG_TAP_COUNT:
		ts->tci.info[1].tap_count = value[0];
		break;

	case LPWG_DOUBLE_TAP_CHECK:
		ts->tci.double_tap_check = value[0];
		break;

	case LPWG_UPDATE_ALL:
		ts->lpwg.mode = value[0];
		ts->lpwg.screen = value[1];
		ts->lpwg.sensor = value[2];
		ts->lpwg.qcover = value[3];
		TOUCH_I("LPWG_UPDATE_ALL: mode[%d], screen[%s], sensor[%s], qcover[%s]\n",
			ts->lpwg.mode,
			ts->lpwg.screen ? "ON" : "OFF",
			ts->lpwg.sensor ? "FAR" : "NEAR",
			ts->lpwg.qcover ? "CLOSE" : "OPEN");
		synaptics_lpwg_mode(dev);
		break;
	}

	return 0;
}

static int synaptics_bin_fw_version(struct device *dev)
{
	struct synaptics_data *d = to_synaptics_data(dev);

	const struct firmware *fw = NULL;
	const u8 *firmware = NULL;
	int rc = 0;

	rc = request_firmware(&fw, d->fw.def_fw, dev);
	if (rc != 0) {
		TOUCH_E("[%s] request_firmware() failed %d\n", __func__, rc);
		return -EIO;
	}

	firmware = fw->data;

	memcpy(d->fw.img_product_id,
			&firmware[d->fw.fw_pid_addr], 6);
	memcpy(d->fw.img_version,
			&firmware[d->fw.fw_ver_addr], 4);

	release_firmware(fw);

	return rc;
}

static char *synaptics_productcode_parse(unsigned char *product)
{
	static char str[128] = {0};
	int len = 0;
	char inch[2] = {0};
	char paneltype = 0;
	char version[2] = {0};
	const char *str_panel[]
		= { "ELK", "Suntel", "Tovis", "Innotek", "JDI", "LGD", };
	const char *str_ic[] = { "Synaptics", };
	int i;

	i = (product[0] & 0xF0) >> 4;
	if (i < 6)
		len += snprintf(str + len, sizeof(str) - len,
				"%s\n", str_panel[i]);
	else
		len += snprintf(str + len, sizeof(str) - len,
				"Unknown\n");

	i = (product[0] & 0x0F);
	if (i < 5 && i != 1)
		len += snprintf(str + len, sizeof(str) - len,
				"%dkey\n", i);
	else
		len += snprintf(str + len, sizeof(str) - len,
				"Unknown\n");

	i = (product[1] & 0xF0) >> 4;
	if (i < 1)
		len += snprintf(str + len, sizeof(str) - len,
				"%s\n", str_ic[i]);
	else
		len += snprintf(str + len, sizeof(str) - len,
				"Unknown\n");

	inch[0] = (product[1] & 0x0F);
	inch[1] = ((product[2] & 0xF0) >> 4);
	len += snprintf(str + len, sizeof(str) - len,
			"%d.%d\n", inch[0], inch[1]);

	paneltype = (product[2] & 0x0F);
	len += snprintf(str + len, sizeof(str) - len,
			"PanelType %d\n", paneltype);

	version[0] = ((product[3] & 0x80) >> 7);
	version[1] = (product[3] & 0x7F);
	len += snprintf(str + len, sizeof(str) - len,
			"version : v%d.%02d\n", version[0], version[1]);

	return str;
}

static int synaptics_get_cmd_version(struct device *dev, char *buf)
{
	struct touch_core_data *ts = to_touch_core(dev);
	struct synaptics_data *d = to_synaptics_data(dev);

	int offset = 0;
	int ret = 0;

	TOUCH_TRACE();
	TOUCH_I("%s\n", __func__);

	ret = synaptics_ic_info(dev);
	ret += synaptics_bin_fw_version(dev);

	if (ret < 0) {
		offset += snprintf(buf + offset, PAGE_SIZE, "-1\n");
		offset += snprintf(buf + offset, PAGE_SIZE - offset,
				"Read Fail Touch IC Info\n");
		return offset;
	}

	offset = snprintf(buf + offset, PAGE_SIZE - offset,
				"\n======== Firmware Info ========\n");

	if (d->fw.version[0] > 0x50) {
		offset += snprintf(buf + offset, PAGE_SIZE - offset,
				"ic_version[%s]\n", d->fw.version);
	} else {
		offset += snprintf(buf + offset, PAGE_SIZE - offset,
				"ic_version RAW = %02X %02X %02X %02X\n",
				d->fw.version[0], d->fw.version[1],
				d->fw.version[2], d->fw.version[3]);
		offset += snprintf(buf + offset, PAGE_SIZE - offset,
				"=== ic_fw_version info ===\n%s",
				synaptics_productcode_parse(d->fw.version));
	}

	offset += snprintf(buf + offset, PAGE_SIZE - offset,
			"IC_product_id[%s]\n", d->fw.product_id);

	if (synaptics_is_product(d, "PLG591", 6))
		offset += snprintf(buf + offset, PAGE_SIZE - offset,
			"Touch IC : s3330\n\n");
	else
		offset += snprintf(buf + offset, PAGE_SIZE - offset,
			"Touch product ID read fail\n");

	if (!ts->factory_boot) {
		if (d->fw.img_version[0] > 0x50) {
			offset += snprintf(buf + offset, PAGE_SIZE - offset,
					"img_version[%s]\n", d->fw.img_version);
		} else {
			offset += snprintf(buf + offset, PAGE_SIZE - offset,
					"img_version RAW = %02X %02X %02X %02X\n",
					d->fw.img_version[0], d->fw.img_version[1],
					d->fw.img_version[2], d->fw.img_version[3]);
			offset += snprintf(buf + offset, PAGE_SIZE - offset,
					"=== img_version info ===\n%s",
					synaptics_productcode_parse(d->fw.img_version));
		}

		offset += snprintf(buf + offset, PAGE_SIZE - offset,
				"Img_product_id[%s]\n", d->fw.img_product_id);

		if (synaptics_is_img_product(d, "PLG591", 6))
			offset += snprintf(buf + offset, PAGE_SIZE - offset,
				"Touch IC : s3330\n\n");
		else
			offset += snprintf(buf + offset, PAGE_SIZE - offset,
				"Touch product ID read fail\n");
	}
	return offset;
}

static int synaptics_get_cmd_atcmd_version(struct device *dev, char *buf)
{
	struct synaptics_data *d = to_synaptics_data(dev);

	int offset = 0;
	int ret = 0;

	ret = synaptics_ic_info(dev);

	if (ret < 0) {
		offset += snprintf(buf + offset, PAGE_SIZE, "-1\n");
		offset += snprintf(buf + offset, PAGE_SIZE - offset,
			"Read Fail Touch IC Info\n");
		return offset;
	}

	if (d->fw.version[0] > 0x50) {
		offset += snprintf(buf + offset, PAGE_SIZE - offset,
				"%s\n", d->fw.version);
	} else {
		offset = snprintf(buf + offset, PAGE_SIZE - offset,
					"v%d.%02d(0x%X/0x%X/0x%X/0x%X)\n",
					(d->fw.version[3] & 0x80 ? 1 : 0),
					d->fw.version[3] & 0x7F,
					d->fw.version[0],
					d->fw.version[1],
					d->fw.version[2],
					d->fw.version[3]);
	}

	return offset;
}

static int synaptics_notify(struct device *dev, ulong event, void *data)
{
	struct touch_core_data *ts = to_touch_core(dev);
	struct synaptics_data *d = to_synaptics_data(dev);

	int mfts_mode = lge_get_mfts_mode();

	switch (event) {
	case LCD_EVENT_TOUCH_LPWG_ON:
		d->power_state = POWER_SLEEP;
		if (mfts_mode) {
			TOUCH_I("%s, MFTS LCD Sleep\n", __func__);
			if (ts->role.mfts_lpwg)
				synaptics_tci_report_enable(dev, true);
			else
				TOUCH_I("%s, MFTS Touch power off\n", __func__);
		} else {
			TOUCH_I("%s, LCD Sleep\n", __func__);
			synaptics_tci_report_enable(dev, true);
		}

		break;

	case LCD_EVENT_TOUCH_LPWG_OFF:
		TOUCH_I("%s, LCD_EVENT_TOUCH_LPWG_OFF\n", __func__);
		d->power_state = POWER_WAKE;
		break;
	}

	return 0;
}

static ssize_t show_pen_support(struct device *dev, char *buf)
{
	int ret = 0;

	ret += snprintf(buf + ret, PAGE_SIZE - ret, "%d\n", 1);

	return ret;
}

static ssize_t store_reg_ctrl(struct device *dev,
		const char *buf, size_t count)
{
	struct touch_core_data *ts = to_touch_core(dev);
	u8 buffer[50] = {0};
	char command[6] = {0};
	int page = 0;
	int reg = 0;
	int offset = 0;
	int value = 0;

	if (sscanf(buf, "%s %d %d %d %d ",
				command, &page, &reg, &offset, &value) <= 0)
		return count;

	mutex_lock(&ts->lock);
	synaptics_set_page(dev, page);
	if (!strcmp(command, "write")) {
		synaptics_read(dev, reg, buffer, offset + 1);
		buffer[offset] = (u8)value;
		synaptics_write(dev, reg, buffer, offset + 1);
	} else if (!strcmp(command, "read")) {
		synaptics_read(dev, reg, buffer, offset + 1);
		TOUCH_I("page[%d] reg[%d] offset[%d] = 0x%x\n",
				page, reg, offset, buffer[offset]);
	} else {
		TOUCH_E("Usage\n");
		TOUCH_E("Write page reg offset value\n");
		TOUCH_E("Read page reg offset\n");
	}
	synaptics_set_page(dev, DEFAULT_PAGE);
	mutex_unlock(&ts->lock);
	return count;
}

static ssize_t show_check_noise(struct device *dev, char *buf)
{
	struct synaptics_data *d = to_synaptics_data(dev);

	int offset = 0;

	offset += snprintf(buf + offset, PAGE_SIZE - offset,
			"Test Count : %d\n", d->noise.cnt);
	offset += snprintf(buf + offset, PAGE_SIZE - offset,
			"Current Noise State : %d\n", d->noise.cns_avg);
	offset += snprintf(buf + offset, PAGE_SIZE - offset,
			"Interference Metric : %d\n", d->noise.im_avg);
	offset += snprintf(buf + offset, PAGE_SIZE - offset,
			"CID IM : %d\n", d->noise.cid_im_avg);
	offset += snprintf(buf + offset, PAGE_SIZE - offset,
			"Freq Scan IM : %d\n", d->noise.freq_scan_im_avg);

	return offset;
}

static ssize_t store_check_noise(struct device *dev,
		const char *buf, size_t count)
{
	struct synaptics_data *d = to_synaptics_data(dev);

	int value = 0;

	if (sscanf(buf, "%d", &value) <= 0)
		return count;

	if ((d->noise.check_noise == NOISE_DISABLE)
		&& (value == NOISE_ENABLE)) {
		d->noise.check_noise = NOISE_ENABLE;
	} else if ((d->noise.check_noise == NOISE_ENABLE)
			&& (value == NOISE_DISABLE)) {
		d->noise.check_noise = NOISE_DISABLE;
	} else {
		TOUCH_I("Already enabled check_noise\n");
		TOUCH_I("check_noise = %d, value = %d\n",
				d->noise.check_noise, value);
		return count;
	}

	TOUCH_I("check_noise = %s\n", (d->noise.check_noise == NOISE_ENABLE)
			? "NOISE_CHECK_ENABLE" : "NOISE_CHECK_DISABLE");

	return count;
}

static ssize_t show_noise_log(struct device *dev, char *buf)
{
	struct synaptics_data *d = to_synaptics_data(dev);

	int offset = 0;

	offset += snprintf(buf + offset, PAGE_SIZE - offset, "%d\n",
				d->noise.noise_log);

	TOUCH_I("noise_log = %s\n", (d->noise.noise_log == NOISE_ENABLE)
			? "NOISE_LOG_ENABLE" : "NOISE_LOG_DISABLE");

	return offset;
}

static ssize_t store_noise_log(struct device *dev,
		const char *buf, size_t count)
{
	struct synaptics_data *d = to_synaptics_data(dev);

	int value = 0;

	if (sscanf(buf, "%d", &value) <= 0)
		return count;

	if ((d->noise.noise_log == NOISE_DISABLE)
		&& (value == NOISE_ENABLE)) {
		d->noise.noise_log = NOISE_ENABLE;
	} else if ((d->noise.noise_log == NOISE_ENABLE)
			&& (value == NOISE_DISABLE)) {
		d->noise.noise_log = NOISE_DISABLE;
	} else {
		TOUCH_I("Already enabled noise_log\n");
		TOUCH_I("noise_log = %d, value = %d\n",
				d->noise.noise_log, value);
		return count;
	}

	TOUCH_I("noise_log = %s\n", (d->noise.noise_log == NOISE_ENABLE)
			? "NOISE_LOG_ENABLE" : "NOISE_LOG_DISABLE");

	return count;
}

static TOUCH_ATTR(reg_ctrl, NULL, store_reg_ctrl);
static TOUCH_ATTR(pen_support, show_pen_support, NULL);
static TOUCH_ATTR(ts_noise, show_check_noise, store_check_noise);
static TOUCH_ATTR(ts_noise_log_enable, show_noise_log, store_noise_log);

static struct attribute *s3330_attribute_list[] = {
	&touch_attr_reg_ctrl.attr,
	&touch_attr_pen_support.attr,
	&touch_attr_ts_noise.attr,
	&touch_attr_ts_noise_log_enable.attr,
	NULL,
};

static const struct attribute_group s3330_attribute_group = {
	.attrs = s3330_attribute_list,
};

static int synaptics_register_sysfs(struct device *dev)
{
	struct touch_core_data *ts = to_touch_core(dev);

	int ret = 0;
	TOUCH_TRACE();

	ret = sysfs_create_group(&ts->kobj, &s3330_attribute_group);
	if (ret < 0)
		TOUCH_E("s3330 sysfs register failed\n");

	s3330_prd_register_sysfs(dev);

	return 0;
}

static int synaptics_set(struct device *dev, u32 cmd, void *input, void *output)
{
	TOUCH_TRACE();

	return 0;

}

static int synaptics_get(struct device *dev, u32 cmd, void *input, void *output)
{
	int ret = 0;

	TOUCH_I("%s : cmd %d\n", __func__, cmd);

	switch (cmd) {
	case CMD_VERSION:
		ret = synaptics_get_cmd_version(dev, (char *)output);
		break;

	case CMD_ATCMD_VERSION:
		ret = synaptics_get_cmd_atcmd_version(dev, (char *)output);
		break;

	default:
		break;
	}

	return ret;
}

static int synaptics_fw_compare(struct device *dev)
{
	struct touch_core_data *ts = to_touch_core(dev);
	struct synaptics_data *d = to_synaptics_data(dev);

	int i = 0;
	int update = 0;
	int mfts_mode = lge_get_mfts_mode();

	if (mfts_mode == 1) {
		TOUCH_I("need not f/w upgrade in mfts_mode\n");
		return 0;
	}

	if (ts->force_fwup) {
		update = 1;
	} else if (d->fw.version[0] > 0x50) {
		if (d->fw.img_version[0] > 0x50) {
			TOUCH_I("product_id[%s(ic):%s(img)] version[%s(ic):%s(img)]\n",
				d->fw.product_id, d->fw.img_product_id,
				d->fw.version, d->fw.img_version);
			if (strncmp(d->fw.version, d->fw.img_version, 4)) {
				TOUCH_I("fw version mismatch\n");
				update = 1;
			}
		} else {
			TOUCH_I("product_id[%s(ic):%s(img)] version[%s(ic):V%d.%02d(img)]\n",
				d->fw.product_id, d->fw.img_product_id,
				d->fw.version, (d->fw.img_version[3] & 0x80 ? 1 : 0),
				d->fw.img_version[3] & 0x7F);
			if (strncmp(d->fw.version, d->fw.img_version, 4)) {
				TOUCH_I("fw version mismatch.\n");
				update = 1;
			}
		}
	} else {
		if (!(d->fw.version[3] & 0x80)) {
			TOUCH_I("Test fw version[V%d.%02d]\n",
				(d->fw.version[3] & 0x80 ? 1 : 0), d->fw.version[3] & 0x7F);
			TOUCH_I("force upgrade test version to official release \n");
		} else if (d->fw.img_version[0] > 0x50) {
			TOUCH_I("product_id[%s(ic):%s(img)] fw_version[V%d.%02d(ic):%s(img)]\n",
				d->fw.product_id, d->fw.img_product_id,
				(d->fw.version[3] & 0x80 ? 1 : 0), d->fw.version[3] & 0x7F,
				d->fw.img_version);
			if (strncmp(d->fw.version, d->fw.img_version, 4)) {
				TOUCH_I("fw version mismatch.\n");
				update = 1;
			}
		} else {
			TOUCH_I("product_id[%s(ic):%s(img)]\n",
				d->fw.product_id, d->fw.img_product_id);
			TOUCH_I("ic_version[V%d.%02d(0x%02X 0x%02X 0x%02X 0x%02X)]\n ",
				(d->fw.version[3] & 0x80 ? 1 : 0), d->fw.version[3] & 0x7F,
				d->fw.version[0], d->fw.version[1],
				d->fw.version[2], d->fw.version[3]);
			TOUCH_I("img_version[V%d.%02d(0x%02X 0x%02X 0x%02X 0x%02X)]\n",
				(d->fw.img_version[3] & 0x80 ? 1 : 0), d->fw.img_version[3] & 0x7F,
				d->fw.img_version[0], d->fw.img_version[1],
				d->fw.img_version[2], d->fw.img_version[3]);
			for (i = 0; i < FW_VER_INFO_NUM; i++) {
				if (d->fw.version[i] != d->fw.img_version[i]) {
					TOUCH_I("version mismatch(ic_version[%d]:0x%02X, img_version[%d]:0x%02X)\n",
						i, d->fw.version[i], i, d->fw.img_version[i]);
					update = 1;
				}
			}
		}
	}

	return update;
}

static int synaptics_upgrade(struct device *dev)
{
	struct touch_core_data *ts = to_touch_core(dev);
	struct synaptics_data *d = to_synaptics_data(dev);

	int ret = 0;
	int update = 0;
	char fwpath[256] = {0};
	const struct firmware *fw = NULL;
	const u8 *firmware = NULL;

	if (!ts->force_fwup) {
		ts->test_fwpath[0] = '\0';
		d->fw.fw_pid_addr = 0x8f4;
		d->fw.fw_ver_addr = 0x8f0;
		d->fw.def_fw = ts->def_fwpath[0];
	}

	if (ts->test_fwpath[0]) {
		memcpy(fwpath, &ts->test_fwpath[0], sizeof(fwpath));
		TOUCH_I("get fwpath from test_fwpath:%s\n",
				&ts->test_fwpath[0]);
	} else if (ts->def_fwcnt) {
		memcpy(fwpath, d->fw.def_fw, sizeof(fwpath));
		TOUCH_I("ic_product_id = %s\n", d->fw.product_id);
	} else {
		TOUCH_E("no firmware file\n");
		return -EPERM;
	}

	if (fwpath == NULL) {
		TOUCH_E("error get fw path\n");
		return -EPERM;
	}

	TOUCH_I("fwpath[%s]\n", fwpath);

	ret = request_firmware(&fw, fwpath, dev);

	if (ret < 0) {
		TOUCH_E("fail to request_firmware fwpath: %s (ret:%d)\n",
			fwpath, ret);

		return ret;
	}

	firmware = fw->data;

	memcpy(d->fw.img_product_id,
			&firmware[d->fw.fw_pid_addr], 6);
	memcpy(d->fw.img_version,
			&firmware[d->fw.fw_ver_addr], 4);

	TOUCH_I("img_product_id : %s\n", d->fw.img_product_id);

	update = synaptics_fw_compare(dev);

	if (update) {
		d->need_scan_pdt = true;
		ret = FirmwareUpgrade(dev, fw);
		
		if (ret < 0) {
			release_firmware(fw);
			return ret;
		}
	} else {
		TOUCH_I("need not fw version upgrade\n");
	}

	release_firmware(fw);

	return 0;
}

static int synaptics_probe(struct device *dev)
{
	struct touch_core_data *ts = to_touch_core(dev);
	struct synaptics_data *d;

	TOUCH_TRACE();

	d = devm_kzalloc(dev, sizeof(*d), GFP_KERNEL);

	if (!d) {
		TOUCH_E("failed to allocate synaptics data\n");
		return -ENOMEM;
	}

	touch_set_device(ts, d);

	touch_gpio_init(ts->reset_pin, "touch_reset");
	touch_gpio_direction_output(ts->reset_pin, 0);

	touch_gpio_init(ts->int_pin, "touch_int");
	touch_gpio_direction_input(ts->int_pin);

	touch_power_init(dev);
	touch_bus_init(dev, 4096);

	synaptics_init_tci_info(dev);

	d->need_scan_pdt = true;

	return 0;
}

static struct touch_driver touch_driver = {
	.probe = synaptics_probe,
	.remove = synaptics_remove,
	.suspend = synaptics_suspend,
	.resume = synaptics_resume,
	.init = synaptics_init,
	.upgrade = synaptics_upgrade,
	.irq_handler = synaptics_irq_handler,
	.power = synaptics_power,
	.lpwg = synaptics_lpwg,
	.notify = synaptics_notify,
	.register_sysfs = synaptics_register_sysfs,
	.set = synaptics_set,
	.get = synaptics_get,
};


#define MATCH_NAME			"synaptics,s3330"

static struct of_device_id touch_match_ids[] = {
	{ .compatible = MATCH_NAME, },
};

static struct touch_hwif hwif = {
	.bus_type = HWIF_I2C,
	.name = LGE_TOUCH_NAME,
	.owner = THIS_MODULE,
	.of_match_table = touch_match_ids,
};

static int __init touch_device_init(void)
{
	TOUCH_TRACE();

	if (lge_get_panel_type() != PH2_JDI)
		return 0;

	return touch_bus_device_init(&hwif, &touch_driver);
}

static void __exit touch_device_exit(void)
{
	TOUCH_TRACE();
	touch_bus_device_exit(&hwif);
}

module_init(touch_device_init);
module_exit(touch_device_exit);

MODULE_AUTHOR("hoyeon.jang@lge.com");
MODULE_DESCRIPTION("LGE touch driver v3");
MODULE_LICENSE("GPL");
