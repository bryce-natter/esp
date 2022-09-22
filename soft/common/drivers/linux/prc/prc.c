/*
 * Copyright (c) 2011-2022 Columbia University, System Level Design Group
 * SPDX-License-Identifier: Apache-2.0
 */

#include <asm/byteorder.h>
#include <linux/io.h>
#include <asm/irq.h> 
#include <linux/of_irq.h>
#include <linux/list.h>
#include <linux/string.h>
#include <esp.h>
#include "prc.h"
#include <linux/delay.h>
#include <linux/workqueue.h>
#include <linux/ktime.h>

#define DRV_NAME "prc"

#define PRC_WRITE(base, offset, value) iowrite32(value, base.prc_base + offset)
#define PRC_READ(base, offset) ioread32(base.prc_base + offset)


DEFINE_MUTEX(prc_lock);

uint32_t rtile = 0;
struct pbs_struct *curr;
struct pbs_struct *next; 

struct esp_prc_device {
	struct device 	*dev;
	struct resource res;
	struct module 	*module;
	void __iomem 	*prc_base;
	int		irq;
} prc_dev;


struct list_head pbs_list[5];


ktime_t start_time, stop_time, elapsed_time;

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

//struct dpr_tile tiles[5] = {};

void tiles_setup(void)
{
	int i;
	unsigned long dphys; 

	for(i = 0; i < 5; i++)
	{
		INIT_LIST_HEAD(&pbs_list[i]);
	}

}

static int prc_start(void)
{
	uint32_t prc_status;
	uint32_t bit1 = 1;

	bit1 = cpu_to_le32(bit1);
	PRC_WRITE(prc_dev, 0x0, bit1);

	prc_status = PRC_READ(prc_dev, 0x0);
	prc_status = le32_to_cpu(prc_status);
	prc_status &= (1<<7);

	if (prc_status) {
		pr_info("PRC (start): error starting controller \n");
		return 1;
	}

	return 0;

}

static int prc_stop(void)
{
	uint32_t prc_status;

	PRC_WRITE(prc_dev, 0x0, 0x0);

	prc_status = PRC_READ(prc_dev, 0x0);
	prc_status = le32_to_cpu(prc_status);
	prc_status &= (1<<7);

	if (!prc_status) {
		pr_info("PRC (stop): error shutting controller \n");
		return 1;
	}
	return 0;
}

static int prc_set_trigger(void *pbs_addr, uint32_t pbs_size)
{
	uint32_t le_addr = cpu_to_le32((uint32_t)pbs_addr);
	uint32_t le_size = cpu_to_le32(pbs_size);
	if (!prc_stop()) {
		PRC_WRITE(prc_dev, TRIGGER_OFFSET + 0x0, 0x0);
		PRC_WRITE(prc_dev, TRIGGER_OFFSET + 0x4, le_addr);
		PRC_WRITE(prc_dev, TRIGGER_OFFSET + 0x8, le_size);
		return 0;
	} else {
		pr_info("PRC: Error arming trigger \n");
		return 1;
	}
}


static int prc_reconfigure(pbs_struct *pbs)
{
	//   int status = 0;
	rtile = pbs->tile_id;
	
	//wait_for_tile(pbs->tile_id);
	reinit_completion(&prc_completion);
	mutex_lock(&prc_lock);

	start_time = ktime_get();

	decouple(pbs->tile_id); //Signal to decouple Acc
	prc_set_trigger(pbs->phys_loc, pbs->size);//pbs_id);

	if(!(prc_start())) {
		pr_info(DRV_NAME ": Starting Reconfiguration \n");
		PRC_WRITE(prc_dev, 0x4, 0); //send reconfig trigger
	}

	else {
		pr_info(DRV_NAME ": Error reconfiguring FPGA \n");
		return 1;
	}

	wait_for_completion(&prc_completion);
	mutex_unlock(&prc_lock); // unlock when reconfiguration is actually done

	pr_info(DRV_NAME ": Finished Reconfiguration\n");
}


static long esp_prc_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
{
	int i = 0;

	pbs_arg user_pbs;
	decouple_arg d;
	pbs_struct *pbs_entry;
	struct esp_device *esp;
	struct esp_driver *drv;
	struct list_head *ele;

	/**
	 * TODO:
	 *	- Add remove pbs entry
	 *	- Add return pbs_entry list
	 *	- move cmds to seperate functions
	 */
	switch (cmd) {
		case PRC_LOAD_BS:
			if (copy_from_user(&user_pbs, (pbs_arg *) arg, sizeof(pbs_arg))) {
				pr_info("Failed to copy pbs_arg\n");
				return -EACCES;
			}

			pr_info("pbs_size is 0x%08x\n", user_pbs.pbs_size);
			if (!user_pbs.pbs_size)
				return -EACCES;


			list_for_each_entry(pbs_entry, &pbs_list[user_pbs.pbs_tile_id], list){
				if(pbs_entry->tile_id == user_pbs.pbs_tile_id && !strcmp(pbs_entry->name, user_pbs.name)) {
					pr_info("\nAlready Loaded: %s - %s...\n", pbs_entry->name, user_pbs.name);
					return 0;
					//load_driver(pbs_entry->esp_drv, pbs_entry->tile_id);
					//return -EACCES;
				}
			}

			pbs_entry = kmalloc(sizeof(pbs_struct), GFP_KERNEL);
			pbs_entry->file = kmalloc(user_pbs.pbs_size, GFP_DMA | GFP_KERNEL);
			if (!pbs_entry->file)
				return -ENOMEM;

			pr_info("Looking for %s...\n", user_pbs.driver);
			spin_lock(&esp_drivers_lock);
			list_for_each(ele, &esp_drivers) { drv = list_entry(ele, struct esp_driver, list);
				//pr_info("Comparing [%s] with [%s]\n", drv->plat.driver.name, user_pbs.driver);
				if (!strcmp(drv->plat.driver.name, user_pbs.driver)) {
					pr_info("Found %s driver in driver list\n", user_pbs.driver);
					pbs_entry->esp_drv = drv;
				}
			}

			if (!pbs_entry->esp_drv)
				return -ENODEV;

			spin_unlock(&esp_drivers_lock);

			if (copy_from_user(pbs_entry->file, user_pbs.pbs_mmap, user_pbs.pbs_size)) {
				kfree(pbs_entry->file);
				return -EACCES;
			}

			//Fill in rest of the pbs_struct fields
			pbs_entry->size		= user_pbs.pbs_size;
			pbs_entry->tile_id	= user_pbs.pbs_tile_id;
			pbs_entry->phys_loc	= (void *)(virt_to_phys(pbs_entry->file));
			memcpy(pbs_entry->name, user_pbs.name, LEN_DEVNAME_MAX);
			memcpy(pbs_entry->driver, user_pbs.driver, LEN_DRVNAME_MAX);
			INIT_LIST_HEAD(&pbs_entry->list);

			//If first pbs in list, reconfigure device to use it...
			if (list_empty(&pbs_list[pbs_entry->tile_id])) {

				tiles[pbs_entry->tile_id].next = pbs_entry; 

				mutex_lock(&tiles[user_pbs.pbs_tile_id].esp_dev.dpr_lock);
				prc_reconfigure(pbs_entry);
				mutex_unlock(&tiles[user_pbs.pbs_tile_id].esp_dev.dpr_lock);
			}

			//Add to list:
			list_add(&pbs_entry->list, &pbs_list[user_pbs.pbs_tile_id]);

			pr_info(DRV_NAME ": Successfully Read Arguments...\n");
			break;

		case PRC_RECONFIGURE:
			if (copy_from_user(&user_pbs, (pbs_arg *) arg, sizeof(pbs_arg))) {
				pr_info("Failed to copy pbs_arg\n");
				return -EACCES;
			}
			list_for_each_entry(pbs_entry, &pbs_list[user_pbs.pbs_tile_id], list){
				if(pbs_entry->tile_id == user_pbs.pbs_tile_id && !strcmp(pbs_entry->name, user_pbs.name)){
					pr_info("\nFound match!\n");
					if (!strcmp(tiles[user_pbs.pbs_tile_id].curr->name, pbs_entry->name)) {
						pr_info("Tile already currently using this pbs...\n");
						return 0;
					}

					tiles[user_pbs.pbs_tile_id].next = pbs_entry;
					pr_info(DRV_NAME ": unregistering %s\n", tiles[user_pbs.pbs_tile_id].curr->driver);
					wait_for_tile(user_pbs.pbs_tile_id);
					mutex_lock(&tiles[user_pbs.pbs_tile_id].esp_dev.dpr_lock);
					unload_driver(user_pbs.pbs_tile_id);

					//esp_driver_unregister(tiles[user_pbs.pbs_tile_id].curr->esp_drv);

					prc_reconfigure(pbs_entry);
					//pr_info("Please register %s\n", pbs_entry->esp_drv->plat.driver.name);
					mutex_unlock(&tiles[user_pbs.pbs_tile_id].esp_dev.dpr_lock);
					return 0;
				}
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

static irqreturn_t prc_irq(int irq, void *dev)
{
	int status;
	uint32_t byte3= 0x3;
	byte3 = cpu_to_le32(byte3);

	status = PRC_READ(prc_dev, 0x0);

	PRC_WRITE(prc_dev, 0x0, byte3); //clear interrupt 

	if(status == 0x07000000){
		stop_time = ktime_get();
		elapsed_time= ktime_sub(stop_time, start_time);
		couple(rtile);
		pr_info(DRV_NAME ": Reconfigured Complete triggered\n");
		pr_info(DRV_NAME ": Elapsed time: %lldns\n",  ktime_to_ns(elapsed_time));
		schedule_work(&tiles[rtile].reg_drv_work);
		//complete(&prc_completion);
	}

	return IRQ_HANDLED;
}

static int esp_prc_probe(struct platform_device *pdev)
{
	int ret;
	int rc;
	int irq_num;


	prc_loaded = true; 
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

	rc = request_irq(PRC_IRQ, prc_irq, IRQF_SHARED, DRV_NAME, pdev);
	if (rc) {
		pr_info(DRV_NAME " cannot request IRQ \n");
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
	tiles_setup();
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
