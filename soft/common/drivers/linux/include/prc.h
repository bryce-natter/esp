#ifndef _PRC_H_     
#define _PRC_H_     

#ifdef __KERNEL__          
#include <linux/ioctl.h>   
#include <linux/types.h>   
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
#else                      
#include <sys/ioctl.h>     
#include <stdint.h>        
#ifndef __user             
#define __user
#endif
#endif /* __KERNEL__ */    


#include <esp.h>           

#define PRC_MAGIC 		'P'
#define PRC_OFFSET		0xE400
#define TRIGGER_OFFSET		0x60
#define MONITOR_BASE_ADDR 	0x90180
#define APB_BASE_ADDR		0x80000000


#define LEN_DEVNAME_MAX 32
typedef struct pbs_map {
	char	name [LEN_DEVNAME_MAX];    
	void	*pbs_addr;
	uint32_t	pbs_size;
	uint32_t	pbs_tile_id;
	void	*pbs_mmap; //userspace
}pbs_map;

typedef struct decouple_arg {
	int	tile_id;
	char	status;
}decouple_arg;

#define PRC_RECONFIGURE	_IOW(PRC_MAGIC, 0, pbs_map *)
#define DECOUPLE	_IOW(PRC_MAGIC, 1, pbs_map *)

//#define PRC_SET_TRIGGER	_IOW(PRC_MAGIC, 0, unsigned long)
//#define PRC_START	_IO(PRC_MAGIC, 2)
//#define PRC_STOP	_IO(PRC_MAGIC, 3)

#endif /* _PRC_H_ */
