/*
 * Copyright (c) 2011-2022 Columbia University, System Level Design Group
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Tile Manager
 * Used to manage registering and unregistering of device drivers per tile. 
 * Keeps a list of "boilerplate" esp drivers 
 * Takes in a esp_driver, clones relevant information to respective tile driver
 * This allows for tile-unique drivers.
 *
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


#include <esp_accelerator.h>
#define DRV_NAME "dpr_tile_manger"


struct dpr_tile tiles[5] = {}; 
EXPORT_SYMBOL_GPL(tiles);


//static struct dpr_tile * to_dpr_tile(struct platform_device *pdev)
//{
//	struct device_driver *driver = pdev->dev.driver;
//	struct platform_driver *plat_drv = to_platform_driver(driver);
//	struct esp_driver *esp_drv = container_of(plat_drv, struct esp_driver, plat);
//	struct dpr_tile *tile = container_of(esp_drv, struct dpr_tile, esp_drv);
//	return dpr_tile;
//
//}
//
//


//struct pbs_struct * get_pbs(int tile, char *name)
//{
//	pbs_struct *pbs_entry;
//	list_for_each_entry(pbs_entry, &pbs_list[user_pbs.pbs_tile_id], list){
//		if(pbs_entry->tile_id == tile && !strcmp(pbs_entry->name, name)){
//			pr_info("\nFound match!\n");
//			return pbs_entry;
//		}
//	}
//
//	return NULL;
//
//}
//
//int add_pbs(int tile, struct pbs_struct *pbs)
//{
//	int ret; 
//	struct pbs_struct pbs_entry = get_pbs(pbs->tile_id, pbs->name);
//
//	if(pbs_entry)
//		return -1;
//
//	list_add(&pbs->list, &tiles[tile].pbs_list);
//	return 0;
//
//}

int __decoupler(int tile, int status)
{
	unsigned long decoupler_phys;
	void *decoupler;
	int ret;

	status = __cpu_to_le32(status);
	iowrite32(status, tiles[tile].decoupler);
	ret = ioread32(tiles[tile].decoupler);
	return ret;
}


int decouple(int tile_loc)
{
	return __decoupler(tile_loc, 1);
}
EXPORT_SYMBOL_GPL(decouple);

int couple(int tile_loc)
{
	return __decoupler(tile_loc, 0);
}
EXPORT_SYMBOL_GPL(couple);

static int tile_probe(struct platform_device *pdev)
{
	int rc; 
	struct device_driver *driver = pdev->dev.driver;
	struct platform_driver *plat_drv = to_platform_driver(driver);
	struct esp_driver *esp_drv = container_of(plat_drv, struct esp_driver, plat);
	struct dpr_tile *tile = container_of(esp_drv, struct dpr_tile, esp_drv);
	struct esp_device *esp = &(tile->esp_dev);

	pr_info(DRV_NAME": probe\n");
	pr_info(DRV_NAME": MAYBE THIS? [%s]\n", pdev->dev.driver->name);

	esp->module = THIS_MODULE;
	esp->number = 0;
	esp->driver = esp_drv;

	rc = esp_device_register(esp, pdev);

	return 0;
}

static int __exit tile_remove(struct platform_device *pdev)
{
	int rc; 
	struct esp_device *esp = platform_get_drvdata(pdev);

	esp_device_unregister(esp);
	return 0;
}

void unload_driver(int tile_num)
{
	esp_driver_unregister(&(tiles[tile_num].esp_drv));

}
EXPORT_SYMBOL_GPL(unload_driver);


void load_driver(struct esp_driver *esp, int tile_num)
{
	int ret;

	pr_info(DRV_NAME ": loading driver [%s] for tile [%d] :)\n",
			esp->plat.driver.name, tile_num);
	strcpy(tiles[tile_num].esp_drv.plat.driver.name, esp->plat.driver.name);
	strcat(tiles[tile_num].esp_drv.plat.driver.name, "_");
	strcat(tiles[tile_num].esp_drv.plat.driver.name, tiles[tile_num].tile_id);


	strcpy(tiles[tile_num].esp_drv.plat.driver.of_match_table[0].name, esp->plat.driver.name);
	strcpy(tiles[tile_num].esp_drv.plat.driver.of_match_table[1].name, tiles[tile_num].tile_id);
	pr_info("Copied [%s], now holds: [%s]\n", tiles[tile_num].tile_id,  tiles[tile_num].esp_drv.plat.driver.of_match_table[1].name);
	

	tiles[tile_num].esp_drv.plat.probe	= tile_probe;
	tiles[tile_num].esp_drv.plat.remove	= tile_remove;
	tiles[tile_num].esp_drv.xfer_input_ok	= esp->xfer_input_ok;
	tiles[tile_num].esp_drv.prep_xfer	= esp->prep_xfer;
	tiles[tile_num].esp_drv.ioctl_cm		= esp->ioctl_cm;
	tiles[tile_num].esp_drv.arg_size		= esp->arg_size;
	tiles[tile_num].esp_drv.dpr		= true;
	//tiles[tile_num].esp_drv.esp		= &test_esp_device;

	ret = esp_driver_register(&(tiles[tile_num].esp_drv));
	pr_info(DRV_NAME ": esp_drv_reg returned: %d\n", ret);
}
EXPORT_SYMBOL_GPL(load_driver);


void wait_for_tile(int tile)
{
	u32 status, run;

	if(!tiles[tile].esp_dev.iomem)
		pr_info("IOMEM NOT MAPPED...");
	pr_info("IOMEM: 0x%08x\n", tiles[tile].esp_dev.iomem);


	status = ioread32be(tiles[tile].esp_dev.iomem + STATUS_REG);
	run = status & STATUS_MASK_RUN;
	pr_info("STATUS: 0x%08x\n", status);

	if (run) {
		pr_info("ACC ALREADYING RUNNING, WAIT!\n");
		wait_for_completion_interruptible(&(tiles[tile].esp_dev.completion));
	}



}
EXPORT_SYMBOL_GPL(wait_for_tile);

void register_tile_driver(struct work_struct * work)
{
	struct dpr_tile *tile = container_of(work, struct dpr_tile, reg_drv_work);
	tile->curr = tile->next;
	pr_info(DRV_NAME ": Current now equals -  %s\n", tile->curr->driver);
	load_driver(tile->next->esp_drv, tile->tile_num);
}

void tiles_setup(void)
{
	int i;
	unsigned long dphys; 

	for(i = 0; i < 5; i++)
	{
		INIT_LIST_HEAD(&tiles[i].pbs_list);
		INIT_WORK(&tiles[i].reg_drv_work, register_tile_driver); 
		tiles[i].tile_num = i;
		dphys= (unsigned long) (APB_BASE_ADDR + (MONITOR_BASE_ADDR + i * 0x200));
		tiles[i].decoupler = ioremap(dphys, 1); 
		tiles[i].esp_drv.plat.probe = tile_probe;
		tiles[i].esp_drv.plat.driver.name = "empty-oops";
		tiles[i].esp_drv.plat.driver.owner = THIS_MODULE;

		strcpy(tiles[i].device_ids[2].compatible , "sld");
		tiles[i].esp_drv.plat.driver.of_match_table = tiles[i].device_ids;

		init_completion(&tiles[i].prc_completion);
	}

	strcpy(tiles[2].tile_id, "eb_122");
	strcpy(tiles[4].tile_id, "eb_056");
}

static int __init tile_manager_init(void)
{
	pr_info(DRV_NAME ": init\n");
	tiles_setup();
	return 0;
}

static void __exit tile_manager_exit(void)
{
//	platform_driver_unregister(&tile_manager_driver);
}

static int __exit tile_manager_remove(struct platform_device *pdev)
{
	return 0;
}

module_init(tile_manager_init);
module_exit(tile_manager_exit);


MODULE_LICENSE("GPL");
MODULE_AUTHOR("Bryce Natter <brycenatter@pm.me");
MODULE_DESCRIPTION("esp PRC driver");
