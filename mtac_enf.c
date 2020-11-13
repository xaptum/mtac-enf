#define DRIVER_VERSION  "v0.1.0"
#define DRIVER_AUTHOR   "David R. Bild <david.bild@xaptum.com>"
#define DRIVER_DESC     "Xaptum ENF Access Accessory Card"
#define DRIVER_NAME     "mtac-enf"

#include <linux/types.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/platform_device.h>
#include <linux/kmod.h>
#include <linux/bitops.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/mtac.h>
#include <linux/mts_io.h>

#define PRODUCT_ID_MTAC_ENF				"XAP-EA-004"

static struct gpio_pin gpio_pins_mtcdt_mtac_enf[] = {
	// gpio pins for Accessory Card 1
	{
		.name = "AP1_RESET",
		.pin = {
			.gpio = AT91_PIN_PB12,
			.flags = GPIOF_OUT_INIT_HIGH,
			.label = "ap1-reset",
		}
	},


	// gpio pins for Accessory Card 2
	{
		.name = "AP2_RESET",
		.pin = {
			.gpio = AT91_PIN_PB13,
			.flags = GPIOF_OUT_INIT_HIGH,
			.label = "ap2-reset",
		}
	},
	{ },
};

static char* enf_gpio_pin_name_by_attr_name(const char* name, int port) {
	switch (port) {
		case port_1:
			if (! strcmp(name, "reset")) {
				return "ap1-reset";
			} else {
				log_error("attribute name [%s] is invalid for ENF in port %d", name, port);
				return "";
			}

		case port_2:
			if (! strcmp(name, "reset")) {
				return "ap2-reset";
			} else {
				log_error("attribute name [%s] is invalid for ENF in port %d", name, port);
				return "";
			}
	}
	/* NOTREACHED */
	return "";
}

// 1 vendor-id
// 1 product-id
// 1 device-id
// 1 hw-version
// 1 reset
// NULL
static size_t ap_enf_attrs_size = 6;

static bool enf_setup(enum ap port) {
	int i;
	int port_index = port - 1;
	int index = 0;
	int count = 0;
	int ret;
	char buf[32];
	struct attribute **attrs;
	struct kobj_attribute* attr;
	struct kobject *subdir;

	log_info("loading ENF accessory card in port %d", port);

	sprintf(buf, "ap%d", port);
	subdir = kobject_create_and_add(buf, &mts_io_platform_device->dev.kobj);
	if (! subdir) {
		log_error("kobject_create_and_add for ENF in port %d failed", port);
		return false;
	}

	mtac_set_port_pins(port_index,gpio_pins_mtcdt_mtac_enf,subdir);

	// create the link to the apX directory this card is in
	// if we're in the first slot, we get plain "enf"
	// if we're in a different slot, we might need to use "enf-2" to differentiate
	if (port > 1) {
		for (i = 1; i < port; i++) {
			if (mtac_port_info[i - 1]) {
				if (strstr(mtac_port_info[i - 1]->product_id, PRODUCT_ID_MTAC_ENF)) {
					count++;
				}
			}
		}
	}
	if (count > 0) {
		sprintf(buf, "enf-%d", count + 1);
	} else {
		sprintf(buf, "enf");
	}
	ret = sysfs_create_link(mtac_port_info[port_index]->subdirs->parent, mtac_port_info[port_index]->subdirs, buf);
	if (ret) {
		log_error("failed to link [%s] to [%s], %d", buf, mtac_port_info[port_index]->subdirs->name, ret);
	}

	attrs = kzalloc(sizeof(struct attribute*) * ap_enf_attrs_size, GFP_KERNEL);
	if (! attrs) {
		log_error("failed to allocate attribute space for port %d", port);
		return false;
	}

	sprintf(buf, "reset");
	attr = mtac_create_attribute(buf, MTS_ATTR_MODE_RW);
	if (! attr) {
		log_error("failed to create attribute [%s] for ENF in port %d", buf, port);
		kfree(attrs);
		return false;
	}
	mtac_port_info[port_index]->attr_group.attrs = attrs;

	attr->show = mtac_attr_show_ap_gpio_pin;
	attr->store = mtac_attr_store_ap_gpio_pin;
	attrs[index++] = &attr->attr;

	// add attributes for eeprom contents
	if (! mtac_add_product_info_attributes(port, attrs, &index)) {
		log_error("failed to add product info attributes for ENF in port %d", port);
		return false;
	}
	attrs[index] = NULL;

	if (sysfs_create_group(mtac_port_info[port_index]->subdirs, &mtac_port_info[port_index]->attr_group)) {
		log_error("sysfs_create_group failed for ENF in port %d", port);
		return false;
	}

	return true;
}

static bool enf_teardown(enum ap port) {
	int i;
	int port_index = port - 1;
	struct attribute **attrs = mtac_port_info[port_index]->attr_group.attrs;

	mtac_port_info[port_index]->attr_group.attrs = NULL;

	log_info("unloading ENF accessory card in port %d", port);

	// clean up allocated memory for attributes
	for (i = 0; i < ap_enf_attrs_size; i++) {
		if (attrs[i]) {
			if (attrs[i]->name)
				kfree(attrs[i]->name);

			kfree(attrs[i]);
		}
	}

	kfree(attrs);

	// clean up our "apX/" kobject if it exists
	if (mtac_port_info[port_index]->subdirs) {
		kobject_put(mtac_port_info[port_index]->subdirs);
	}
	mtac_clear_port_pins(port_index);
	return true;
}

void set_enf_info(struct ap_info* info) {
	snprintf(info->product_id, 32, "%s", PRODUCT_ID_MTAC_ENF);
	info->setup = &enf_setup;
	info->teardown = &enf_teardown;
	info->gpio_pin_name_by_attr_name = &enf_gpio_pin_name_by_attr_name;
}
/*
 * Loop through all the slots, and set up the
 * mtac-enf driver for all slots.
 */
static int __init mtac_enf_init(void)
{
	int slot_count = 0;
	slot_count = mtac_find(set_enf_info,PRODUCT_ID_MTAC_ENF);

	if (slot_count < 1) {
		log_debug("No MTAC ENF found");
		if (slot_count < 0)
			return slot_count;
		else
			return -ENXIO;

	}

	return 0;
}

/* We can only tear down our own device */
static void __exit mtac_enf_exit(void)
{
	mtac_free(PRODUCT_ID_MTAC_ENF,enf_setup,"enf");
	log_info("exiting");
}

module_init(mtac_enf_init);
module_exit(mtac_enf_exit);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_VERSION(DRIVER_VERSION);
MODULE_LICENSE("GPL");
