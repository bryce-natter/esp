#ifndef _PRC_H_     
#define _PRC_H_     

#ifdef __KERNEL__          
#include <linux/ioctl.h>   
#include <linux/types.h>   
#else                      
#include <sys/ioctl.h>     
#include <stdint.h>        
#ifndef __user             
#define __user
#endif
#endif /* __KERNEL__ */    

#include <esp.h>           

#define PRC_MAGIC 		'P'
#define PRC_BASE_ADDR_OFFSET 	0xE400
#define PRC_START		0x1
#define PRC_STOP		0x0
#define MONITOR_BASE_ADDR 	0x90180

#define PRC_SET_TRIGGER	_IOW(PRC_MAGIC, 0, unsigned long)
#define PRC_RECONFIGURE	_IOW(PRC_MAGIC, 1, unsigned long)
#define PRC_START	_IO(PRC_MAGIC, 2)
#define PRC_STOP	_IO(PRC_MAGIC, 3)

#endif /* _PRC_H_ */
