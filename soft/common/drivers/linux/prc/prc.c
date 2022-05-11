/*
 * Copyright (c) 2011-2022 Columbia University, System Level Design Group
 * SPDX-License-Identifier: Apache-2.0
 */



#include "prc.h"
#include <asm/byteorder.h>
//#include "soc_defs.h"
//#include "soc_locs.h"
//#include "pbs_map.h"

#define DRV_NAME "prc"

pbs_map bs_descriptor [2] = { 
	{"fir_vivado_2", 1508564, 0, 2}, 
	{"mac_vivado_2", 1508564, 1520852, 2}, 
};


#define PRC_WRITE(base, offset, value) iowrite32(value, base.iomem + offset)
#define PRC_READ(base, offset) ioread32(base.iomem + offset)

struct esp_prc_device {
	struct device 	*dev;
	struct resource res;
	struct module 	*module;
	void __iomem 	*iomem;
} prc_dev;

static struct pbs_map *pb_map;

static struct of_device_id esp_prc_device_ids[] = {
	{
		.name = "XILINX_PRC",
	},
	{
		.name = "ef_031",
	},
	{
		.compatible = "sld,prc",
	},
	{ },
};


//static inline int get_decoupler_addr(struct esp_device *dev, struct esp_device *decoupler)
//{
//	unsigned i;
//	unsigned tile_id = 0xFF;
//	unsigned dev_addr;
//	unsigned dev_addr_trunc;
//	unsigned dev_start_addr = 0x10000;
//
//	const unsigned addr_incr = 0x100;
//	const unsigned monitor_base = 0x90180;
//
//	dev_addr = (unsigned) dev->addr;
//	dev_addr_trunc = (dev_addr << 12) >> 12;
//	//printf("device address 0x%08x truncated addr 0x%08x \n", dev_addr, dev_addr_trunc);
//	//#ifdef ACCS_PRESENT
//	//Obtain tile id
//	for (i = 0; i < SOC_NACC; i++) {
//		if(dev_start_addr == dev_addr_trunc) {
//			tile_id = acc_locs[i].row * SOC_COLS + acc_locs[i].col;
//			break;
//		}
//		else
//			dev_start_addr += addr_incr;
//	}
//
//	if(tile_id == 0XFF) {
//		fprintf(stderr, "Error: cannot find tile id\n");
//		exit(EXIT_FAILURE);
//	}
//
//	//compute apb address for tile decoupler
//	(*decoupler).addr = APB_BASE_ADDR + (monitor_base + tile_id * 0x200);
//	//printf("tile_id is 0x%08x decoupler addr is 0x%08x \n", tile_id, (unsigned) esp_tile_decoupler.addr);
//	return 0;
//}
//
//int decouple_acc(struct esp_device *dev, unsigned val)
//{
//	struct esp_device esp_tile_decoupler;
//	get_decoupler_addr(dev, &esp_tile_decoupler);
//	if (val == 0)
//		PRC_WRITE(esp_tile_decoupler, 0, 0);
//	else
//		PRC_WRITE(esp_tile_decoupler, 0, 1);
//
//	return 0;
//}

static int prc_start(void)
{
	uint32_t prc_status;
	uint32_t bit1 = 1;

	bit1 = cpu_to_le32(bit1);

	pr_info("PRC (start): restarting PRC\n");
	PRC_WRITE(prc_dev, 0x0, bit1);

	prc_status = PRC_READ(prc_dev, 0x0);
	prc_status = le32_to_cpu(prc_status);
	pr_info("PRC (start): read status:0x%08x \n", prc_status);
	prc_status &= (1<<7);
	pr_info("PRC (start): status check:0x%08x \n", prc_status);
	if (prc_status) {
		pr_info("PRC (start): error starting controller \n");
		return 1;
	}

	return 0;

}

static int prc_stop(void)
{
	uint32_t prc_status;

	//pr_info("PRC: Shutting down PRC\n");
	PRC_WRITE(prc_dev, 0x0, 0x0);

	prc_status = PRC_READ(prc_dev, 0x0);
	prc_status = le32_to_cpu(prc_status);
	pr_info("PRC (stop): read status:0x%08x \n", prc_status);
	prc_status &= (1<<7);
	if (!prc_status) {
		pr_info("PRC (stop): error shutting controller \n");
		return 1;
	}
	pr_info("PRC (stop): success shutting controller \n");

	return 0;
}

static int prc_set_trigger(void *pbs_addr, uint32_t pbs_size)
{
	uint32_t le_addr = cpu_to_le32((uint32_t)pbs_addr);
	uint32_t le_size = cpu_to_le32(pbs_size);
	if (!prc_stop()) {
		PRC_WRITE(prc_dev, TRIGGER_OFFSET + 0x0, 0x0);
		pr_info("PRC Trigger [0x%08x] Wrote: [0x%08x]\n", prc_dev.iomem + TRIGGER_OFFSET + 0x0, 0x0);

		PRC_WRITE(prc_dev, TRIGGER_OFFSET + 0x4, le_addr);
		pr_info("PRC Trigger [0x%08x] Wrote: [0x%08x]\n", prc_dev.iomem + TRIGGER_OFFSET + 0x4, le_addr);

		PRC_WRITE(prc_dev, TRIGGER_OFFSET + 0x8, le_size);
		pr_info("PRC Trigger [0x%08x] Wrote: [0x%08x]\n", prc_dev.iomem + TRIGGER_OFFSET + 0x8, le_size);

		pr_info("PRC: Trigger armed \n");
		return 0;
	} else {
		pr_info("PRC: Error arming trigger \n");
		return 1;
	}
}


static int prc_reconfigure(pbs_map *pbs, void *pbs_file)
{
	//   int status = 0;

	//init_prc();
	prc_set_trigger(pbs_file, pbs->pbs_size);//pbs_id);

	if(!(prc_start())) {
		//decouple_acc(dev, 1); //decouple tile
		pr_info("PRC: Starting Reconfiguration \n");
		PRC_WRITE(prc_dev, 0x4, 0); //send reconfig trigger
		return 0;
	}

	else {
		pr_info("PRC: Error reconfiguring FPGA \n");
		return 1;
		//exit(EXIT_FAILURE);
	}

	/*
	while(!status){
	status = PRC_READ(prc_dev, 0x0);
	status &= (1 << 2);
	}
	*/
	//pr_info("PRC: Reconfigured FPGA \n");
}


static long esp_prc_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
{
	pbs_map pbs;
	void *pbs_file;

	switch (cmd) {
		//These probably shouldn't be called from userspace actually
		//case PRC_START:
		//	prc_start();
		//	break;
		//case PRC_STOP:
		//	prc_stop();
		//	break;
		//case PRC_SET_TRIGGER:
		//	prc_set_trigger(0);
		//	break;
		case PRC_RECONFIGURE:
			if (copy_from_user(&pbs, (pbs_map *) arg, sizeof(pbs_map))) {
				pr_info("Failed to copy pbs_map\n");
				return -EACCES;
			}

			pr_info("pbs_size is 0x%08x\n", pbs.pbs_size);
			if (!pbs.pbs_size)
				return -EACCES;

			pbs_file = kmalloc(pbs.pbs_size, GFP_DMA | GFP_KERNEL);
			if (!pbs_file)
				return -ENOMEM;

			if (copy_from_user(pbs_file, pbs.pbs_mmap, pbs.pbs_size)) {
				kfree(pbs_file);
				return -EACCES;
			}

			pr_info("Read Arguments...\n");

			prc_reconfigure(&pbs, pbs_file);
			break;
		default:
			return -EINVAL;
	}
	return 0;
}

static const struct file_operations esp_prc_fops = {
	.owner 		= THIS_MODULE,
	.unlocked_ioctl = esp_prc_ioctl,
};

static struct miscdevice esp_prc_misc_device = {
	.minor 	= MISC_DYNAMIC_MINOR,
	.name 	= DRV_NAME,
	.fops	= &esp_prc_fops,
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


static int __exit esp_prc_remove(struct platform_device *pdev)
{
	iounmap(prc_dev.iomem);
	release_mem_region(prc_dev.res.start, resource_size(&prc_dev.res));
	misc_deregister(&esp_prc_misc_device);
	return 0;
}

static struct platform_driver esp_prc_driver = {
		.driver		= {
			.name 		= DRV_NAME,
			.owner		= THIS_MODULE,
			.of_match_table	= of_match_ptr(esp_prc_device_ids),
		},
		.remove	= __exit_p(esp_prc_remove),
};


static int __init esp_prc_init(void)
{
	pr_info(DRV_NAME ": init\n");
	pb_map = (struct pbs_map *) &bs_descriptor;
	return platform_driver_probe(&esp_prc_driver, esp_prc_probe);
}

static void __exit esp_prc_exit(void)
{
	platform_driver_unregister(&esp_prc_driver);
}

module_init(esp_prc_init);
module_exit(esp_prc_exit);

MODULE_DEVICE_TABLE(of, esp_prc_device_ids);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Bryce Natter <brycenatter@pm.me");
MODULE_DESCRIPTION("esp PRC driver");
