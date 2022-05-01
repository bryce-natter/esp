/*
 * Copyright (c) 2011-2022 Columbia University, System Level Design Group
 * SPDX-License-Identifier: Apache-2.0
 */

#include "prc.h"
#define DRV_NAME	"prc"

struct esp_prc_device {
	struct device 	*dev;
	struct resource res;
	struct module 	*module;
	void iomem 		*iomem;
} prc_dev;

static struct esp_driver prc_driver;
static struct prc_device prc_dev;
static struct pbs_map *pb_map;

static struct of_device_id esp_prc_device_ids[] = {
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
};


static inline int get_decoupler_addr(struct esp_device *dev, struct esp_device *decoupler)
{
	unsigned i;
	unsigned tile_id = 0xFF;
	unsigned dev_addr;
	unsigned dev_addr_trunc;
	unsigned dev_start_addr = 0x10000;

	const unsigned addr_incr = 0x100;
	const unsigned monitor_base = 0x90180;

	dev_addr = (unsigned) dev->addr;
	dev_addr_trunc = (dev_addr << 12) >> 12;
	//printf("device address %0x truncated addr %0x \n", dev_addr, dev_addr_trunc);
	//#ifdef ACCS_PRESENT
	//Obtain tile id
	for (i = 0; i < SOC_NACC; i++) {
		if(dev_start_addr == dev_addr_trunc) {
			tile_id = acc_locs[i].row * SOC_COLS + acc_locs[i].col;
			break;
		}
		else
			dev_start_addr += addr_incr;
	}

	if(tile_id == 0XFF) {
		fprintf(stderr, "Error: cannot find tile id\n");
		exit(EXIT_FAILURE);
	}

	//compute apb address for tile decoupler
	(*decoupler).addr = APB_BASE_ADDR + (monitor_base + tile_id * 0x200);
	//printf("tile_id is %0x decoupler addr is %0x \n", tile_id, (unsigned) esp_tile_decoupler.addr);
	return 0;
}

int decouple_acc(struct esp_device *dev, unsigned val)
{
	struct esp_device esp_tile_decoupler;
	get_decoupler_addr(dev, &esp_tile_decoupler);
	if (val == 0)
		iowrite32(0, prc_dev.iomem);
	else
		iowrite32(1, prc_dev.iomem);

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

static void init_prc()
{
    esp_prc.addr = (long long unsigned) APB_BASE_ADDR + PRC_OFFSET;

    pb_map = (struct pbs_map *) &bs_descriptor;

    //printf("bitstream addr %0x %08x \n", pb_map->pbs_size, (unsigned) pb_map->pbs_addr);
}

static void reconfigure(void)
{
	//   int status = 0;

	//init_prc();
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

static void set_trigger(int pbs_id)
{
	if (!shutdown_prc()) {
		iowrite32(&esp_prc, 0x60, 0x0);
		iowrite32(&esp_prc, 0x64, PBS_BASE_ADDR + pb_map[pbs_id].pbs_addr);
		iowrite32(&esp_prc, 0x68, pb_map[pbs_id].pbs_size);
		printf("PRC: Trigger armed \n");
	} else
		printf("PRC: Error arming trigger \n");
}

static long esp_prc_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
{
	switch (cmd) {
		case PRC_START:
			break;
		case PRC_STOP:
			break;
		case PRC_SET_TRIGGER:
			break;
		case PRC_RECONFIGURE:
			break;
}

static const struct file_operations esp_prc_fops = {
	.owner 		= THIS_MODULE,
	.unlocked_ioctl = esp_prc_ioctl,
};

static struct miscdevice esp_prc_misc_device = {
	.minor 	= MISC_DYNAMIC_MINOR,
	.name 	= DRV_NAME,
	.fops	= esp_prc_fops,
};

static int esp_prc_probe(struct platform_device *pdev)
{
	int ret;

	ret = misc_register(&esp_prc_misc_device);
	ret = of_address_to_resource(pdev->dev.of_node, 0, &prc_dev.res);
	if (ret) {
		ret = -ENOENT;
		goto deregister_res;
	}

	if (request_mem_region(prc_dev.res.start, resource_size(&prc_dev.res),
				DRV_NAME) == NULL)
	{
		ret = -EBUSY;
		goto deregister_res;
	}

	prc_dev.iomem = of_iomap(pdev->dev.of_node, 0);
	if (prc_dev.iomem == NULL) {
		ret = -ENOMEM;
		goto release_mem;
	}

	return 0;

release_mem:
	release_mem_region(prc_dev.res.start, resource_size(&prc_dev.res));
deregister_res:
	misc_deregister(&esp_prc_misc_device);
	return ret;
}

static int __exit esp_prc_remove(struct platform_deivce *pdev)
{
	iounmap(prc_dev.iomem);
	release_mem_region(prc_dev.res.start, resource_size(&prc_dev.res));
	misc_deregister(&esp_prc_misc_device);
	return 0;
}

static struct platform_driver esp_prc_driver = {
		.remove		= esp_prc_remove,
		.driver		= {
			.name 		= DRV_NAME,
			.owner		= THIS_MODULE,
			.of_match_table	= esp_prc_device_ids,
		},
};


static int __init esp_prc_init(void)
{
	pr_info(DRV_NAME ": init\n");
	return platform_driver_probe(&esp_prc_driver, esp_prc_probe);
}

static int __exit prc_exit(void)
{
	return esp_driver_unregister(&esp_driver);
}

module_init(esp_prc_init);
module_exit(esp_prc_exit);

MODULE_DEVICE_TABLE(of, esp_prc_device_ids);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Bryce Natter <brycenatter@pm.me");
MODULE_DESCRIPTION("esp PRC driver");
