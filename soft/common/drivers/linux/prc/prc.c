/*
 * Copyright (c) 2011-2022 Columbia University, System Level Design Group
 * SPDX-License-Identifier: Apache-2.0
 */

#include <asm/byteorder.h>
#include <linux/io.h>
#include <linux/list.h>
#include <linux/string.h>
#include "prc.h"
#include <esp.h>

#define DRV_NAME "prc"

#define PRC_WRITE(base, offset, value) iowrite32(value, base.prc_base + offset)
#define PRC_READ(base, offset) ioread32(base.prc_base + offset)


//static struct pbs_struct pbs_entries;

struct esp_prc_device {
	struct device 	*dev;
	struct resource res;
	struct module 	*module;
	void __iomem 	*prc_base;
	void __iomem	*decoupler_base;
} prc_dev;

static struct pbs_arg *pb_map;

static LIST_HEAD(pbs_list);


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

static int prc_start(void)
{
	uint32_t prc_status;
	uint32_t bit1 = 1;

	bit1 = cpu_to_le32(bit1);

	//pr_info("PRC (start): restarting PRC\n");
	PRC_WRITE(prc_dev, 0x0, bit1);

	prc_status = PRC_READ(prc_dev, 0x0);
	prc_status = le32_to_cpu(prc_status);
	//pr_info("PRC (start): read status:0x%08x \n", prc_status);
	prc_status &= (1<<7);
	//pr_info("PRC (start): status check:0x%08x \n", prc_status);
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
	//pr_info("PRC (stop): read status:0x%08x \n", prc_status);
	prc_status &= (1<<7);
	if (!prc_status) {
		pr_info("PRC (stop): error shutting controller \n");
		return 1;
	}
	//pr_info("PRC (stop): success shutting controller \n");

	return 0;
}

static int prc_set_trigger(void *pbs_addr, uint32_t pbs_size)
{
	uint32_t le_addr = cpu_to_le32((uint32_t)pbs_addr);
	uint32_t le_size = cpu_to_le32(pbs_size);
	if (!prc_stop()) {
		PRC_WRITE(prc_dev, TRIGGER_OFFSET + 0x0, 0x0);
		//pr_info("PRC Trigger [0x%08x] Wrote: [0x%08x]\n", prc_dev.prc_base + TRIGGER_OFFSET + 0x0, 0x0);

		PRC_WRITE(prc_dev, TRIGGER_OFFSET + 0x4, le_addr);
		//pr_info("PRC Trigger [0x%08x] Wrote: [0x%08x]\n", prc_dev.prc_base + TRIGGER_OFFSET + 0x4, le_addr);

		PRC_WRITE(prc_dev, TRIGGER_OFFSET + 0x8, le_size);
		//pr_info("PRC Trigger [0x%08x] Wrote: [0x%08x]\n", prc_dev.prc_base + TRIGGER_OFFSET + 0x8, le_size);

		//pr_info("PRC: Trigger armed \n");
		return 0;
	} else {
		pr_info("PRC: Error arming trigger \n");
		return 1;
	}
}


static int prc_reconfigure(pbs_struct *pbs)
{
	//   int status = 0;

	prc_set_trigger(pbs->phys_loc, pbs->size);//pbs_id);

	if(!(prc_start())) {
		//decouple_acc(dev, 1); //decouple tile
		pr_info("PRC: Starting Reconfiguration \n");
		PRC_WRITE(prc_dev, 0x4, 0); //send reconfig trigger
	}

	else {
		pr_info("PRC: Error reconfiguring FPGA \n");
		return 1;
	}

//	status = PRC_READ(prc_dev, 0x0);
//	status = le32_to_cpu(status);
//
//	while(!status){
//		status = PRC_READ(prc_dev, 0x0);
//		status = le32_to_cpu(status);
//		printk(DRV_NAME ": Read status: 0x%0x\n", status);
//		status &= 6;//(1 << 2);
//	}

	pr_info("PRC: Reconfigured FPGA \n");
}

int decoupler(int tile_id, int status)
{
	unsigned long decoupler_phys;
	void *decoupler;
	int ret;

	decoupler_phys = (unsigned long) (APB_BASE_ADDR + (MONITOR_BASE_ADDR + tile_id * 0x200));

	//REQUEST DECOUPLER IO REGION
	decoupler = ioremap(decoupler_phys, 1);

	status = __cpu_to_le32(status);
	iowrite32(status, decoupler);
	ret = ioread32(decoupler);

	printk(DRV_NAME ": Reading decoupler @(0x%08x -> 0x%08x): 0x%0x\n", decoupler_phys, decoupler, ret);

	ret = ioread32(decoupler);
	iounmap(decoupler);
	return ret;

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
	int i = 0;

	pbs_arg user_pbs;
	decouple_arg d;
	pbs_struct *pbs_entry;
	struct esp_device *esp;

	switch (cmd) {
		case PRC_LOAD_BS:
			if (copy_from_user(&user_pbs, (pbs_arg *) arg, sizeof(pbs_arg))) {
				pr_info("Failed to copy pbs_arg\n");
				return -EACCES;
			}

			pr_info("pbs_size is 0x%08x\n", user_pbs.pbs_size);
			if (!user_pbs.pbs_size)
				return -EACCES;

			list_for_each_entry(pbs_entry, &pbs_list, list){
				if(pbs_entry->tile_id == user_pbs.pbs_tile_id && !strcmp(pbs_entry->name, user_pbs.name)) {
					pr_info("\nAlready Loaded: %s - %s...\n", pbs_entry->name, user_pbs.name);
					return -EACCES;
				}
			}

			pbs_entry = kmalloc(sizeof(pbs_struct), GFP_KERNEL);
			pbs_entry->file = kmalloc(user_pbs.pbs_size, GFP_DMA | GFP_KERNEL);
			if (!pbs_entry->file)
				return -ENOMEM;

			if (copy_from_user(pbs_entry->file, user_pbs.pbs_mmap, user_pbs.pbs_size)) {
				kfree(pbs_entry->file);
				return -EACCES;
			}

			//Fill in rest of the pbs_struct fields
			pbs_entry->size		= user_pbs.pbs_size;
			pbs_entry->tile_id	= user_pbs.pbs_tile_id;
			pbs_entry->phys_loc	= (void *)(virt_to_phys(pbs_entry->file));
			memcpy(pbs_entry->name, user_pbs.name, LEN_DEVNAME_MAX);
			INIT_LIST_HEAD(&pbs_entry->list);

			//Add to list:
			list_add(&pbs_entry->list, &pbs_list);

			pr_info("Read Arguments...\n");
			break;

		case DECOUPLE:
			if ( copy_from_user(&d, (decouple_arg *) arg, sizeof(decouple_arg)))
				return -EACCES;
			return decoupler(d.tile_id, d.status);

			break;

		case PRC_RECONFIGURE:
			if (copy_from_user(&user_pbs, (pbs_arg *) arg, sizeof(pbs_arg))) {
				pr_info("Failed to copy pbs_arg\n");
				return -EACCES;
			}
			list_for_each_entry(pbs_entry, &pbs_list, list){
				if(pbs_entry->tile_id == user_pbs.pbs_tile_id && !strcmp(pbs_entry->name, user_pbs.name)){
					pr_info("\nFound match!\n");
					prc_reconfigure(pbs_entry);
					return 0;
				}
			}

			if (user_pbs.pbs_tile_id == 1)
				esp_driver_register(prc_fir_driver);
			if (user_pbs.pbs_tile_id == 2)
			{
				//esp = platform_get_drvdata(pd);
				//esp_device_unregister(esp);
				esp_driver_unregister(prc_fir_driver);
			}	
			if (user_pbs.pbs_tile_id == 3)
				esp_driver_register(prc_mac_driver);
			if (user_pbs.pbs_tile_id == 4)
			{
				esp = platform_get_drvdata(pd);
				esp_device_unregister(esp);
				esp_driver_unregister(prc_mac_driver);
			}

			pr_info("\nBitstream not loaded..\n");
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

	//Device tree to request IO
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

	prc_dev.prc_base = of_iomap(pdev->dev.of_node, 0);
	if (prc_dev.prc_base == NULL) {
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
	iounmap(prc_dev.prc_base);
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
