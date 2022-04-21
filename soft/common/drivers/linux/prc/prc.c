/*
 * Copyright (c) 2011-2022 Columbia University, System Level Design Group
 * SPDX-License-Identifier: Apache-2.0
 */

#include <esp.h>

#include <linux/of_device.h>
#include <linux/mm.h>

#include <linux/module.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/miscdevice.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/fs.h>
#include <linux/uaccess.h>

struct prc_device {
	struct esp_device esp;
}

static struct esp_driver prc_driver;
static struct prc_device prc_dev;
static struct pbs_map *pb_map;

static struct of_device_id <accelerator_name>_device_ids[] = {
	{
		.name = "SLD_PRC",
	},
	{
		.name = "eb_100",
	},
	{
		.compatible = "sld,prc",
	},
	{ },

static inline struct prc_device *to_prc(struct esp_device *esp)
{
	return container_of(esp, struct prc_device, esp);
}

int decouple_acc(struct esp_device *dev, unsigned val)
{
	get_decoupler_addr(dev, &esp_tile_decoupler);
	if (val == 0)
		iowrite32(&esp_tile_decoupler, 0, 0);
	else 
		iowrite32(&esp_tile_decoupler, 0, BIT(0));

	return 0;
}

static void start(struct esp_device *esp, void *arg)
{
	int prc_status;

	printf("PRC: restarting PRC\n");
	iowrite32(&esp_prc, 0x0, 0x1);

	prc_status = ioread32(&esp_prc, 0x0);
	prc_status &= (1<<7);
	if (prc_status) {
		printf("PRC: error starting controller \n");
		return 1;
	}

	return 0;

}

static void stop(struct esp_device *esp, void *arg)
{
	int prc_status;

	printf("PRC: Shutting down PRC\n");
	iowrite32(&esp_prc, 0x0, 0x0);

	prc_status = ioread32(&esp_prc, 0x0);
	prc_status &= (1<<7);
	if (!prc_status) {
		printf("PRC: error shutting controller \n");
		return 1;
	}

	return 0;
}

static void reconfigure(struct esp_device *esp, void *arg)
{
	//   int status = 0;

	init_prc();
	set_trigger(pbs_id);

	if(!(start_prc())) {
		//decouple_acc(dev, 1); //decouple tile
		printf("PRC: Starting Reconfiguration \n");
		iowrite32(&esp_prc, 0x4, 0); //send reconfig trigger
	}

	else {
		fprintf(stderr, "PRC: Error reconfiguring FPGA \n");
		exit(EXIT_FAILURE);
	}
	/*
	   while(!status){
	   status = ioread32(&esp_prc, 0x0);
	   status &= (1 << 2);
	   }
	   */
	printf("PRC: Reconfigured FPGA \n");
}

static void set_trigger(struct esp_device *esp, void *arg)
{
	if (!shutdown_prc()) {
		iowrite32(&esp_prc, 0x60, 0x0);
		iowrite32(&esp_prc, 0x64, PBS_BASE_ADDR + pb_map[pbs_id].pbs_addr);
		iowrite32(&esp_prc, 0x68, pb_map[pbs_id].pbs_size);
		printf("PRC: Trigger armed \n");
	} else 
		printf("PRC: Error arming trigger \n");
}

static void prc_prep_xfer(struct esp_device *esp, void *arg)
{
	return;
}

static bool prc_xfer_input_ok(struct esp_device *esp, void *arg)
{
	return true;
}

static int __exit prc_remove(struct platform_deivce *pdev)
{
}

static int prc_probe(struct platform_deivce *pdev)
{
	struct prc_device *prc;
	struct esp_device *esp;
	int rc;

	prc = kzalloc(sizeof(*prc), GFP_KERNEL);
	if (prc == NULL)
		return -ENOMEM;
	esp = &prc->esp;
	esp->module = THIS_MODULE;
	esp->number = 0;
	esp->driver = prc_driver;
	esp->addr = (long long unsigned) APB_BASE_ADDR + 0xE400;
	pb_map = (struct pbs_map *) &bs_descriptor;

	rc = esp_deivce_register(esp, pdev);
	if (rc)
		goto err;
	return 0;
err:
	kfree(mac);
	return rc;

}

static struct esp_driver prc_driver = {
	.plat = {
		.probe		= prc_probe,
		.remove		= prc_probe,
		.driver		= {
			.name 		= DRV_NAME,
			.owner		= THIS_MODULE,
			.of_match_table	= prc_device_id,
		},
	},
	.xfer_input_ok	= prc_xfer_input_ok,
	.prep_xfer 	= prc_prep_xfer,
	.ioctl_cm	= LIST_HEAD(ioctl_cm_list),
	.arg_size	= sizeof(struct prc_access),
};

static struct esp_ioctl_cm set_trigger_cm = {
	.list INIT_LIST_HEAD(prc_driver.ioctl_cm.list),
	.cm = PRC_SET_TRIGGER,
	.handle = set_trigger;
};

list_add(<accelerator_name>_driver.ioctl_cm, <accelerator_name>_ioctl_cm);
static int __init prc_init(void)
{
	return esp_driver_register(&esp_driver);
}

static int __exit prc_exit(void)
{
	return esp_driver_unregister(&esp_driver);
}

module_init(prc_init);
module_init(prc_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Bryce Natter <brycenatter@pm.me");
MODULE_DESCRIPTION("PRC driver");
