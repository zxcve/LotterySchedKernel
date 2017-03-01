/*
 *  linux/fs/proc/proc_lottery.c
 *
 *  Copyright (C) 1992  by Linus Torvalds
 *  based on ideas by Darren Senn
 *
 *  This used to be the part of array.c. See the rest of history and credits
 *  there. I took this into a separate file and switched the thing to generic
 *  proc_file_inode_operations, leaving in array.c only per-process stuff.
 *  Inumbers allocation made dynamic (via create_proc_entry()).  AV, May 1999.
 *
 */
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/time.h>
#include <linux/kernel.h>
#include <linux/kernel_stat.h>
#include <linux/fs.h>
#include <linux/tty.h>
#include <linux/string.h>
#include <linux/mman.h>
#include <linux/proc_fs.h>
#include <linux/ioport.h>
#include <linux/mm.h>
#include <linux/mmzone.h>
#include <linux/pagemap.h>
#include <linux/swap.h>
#include <linux/slab.h>
#include <linux/smp.h>
#include <linux/signal.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/seq_file.h>
#include <linux/times.h>
#include <linux/profile.h>
#include <linux/utsname.h>
#include <linux/blkdev.h>
#include <linux/hugetlb.h>
#include <linux/jiffies.h>
#include <linux/sysrq.h>
#include <linux/vmalloc.h>
#include <linux/crash_dump.h>
#include <linux/pid_namespace.h>
#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/io.h>
#include <asm/tlb.h>
#include <asm/div64.h>
#include "internal.h"

#ifdef  CONFIG_SCHED_LOTTERY_POLICY
#define LOTTERY_MAX_CURSOR_LINES_EVENTS   1

void lottery_reset_latency(void);
void lottery_get_latency(unsigned long long *iteration, unsigned long long *latency);

struct proc_dir_entry *lottery_dir;
unsigned int length = 0;

static int lottery_open(struct inode *inode, struct file *file)
{
	length = 0;
        return 0;
}

static int lottery_release(struct inode *inode, struct file *file)
{
	length = 0;
        return 0;
}

static ssize_t lottery_latency_write(struct file *filp, const char __user *buf,
				size_t count, loff_t *ppos)
{
	lottery_reset_latency();
	return count;
}

static ssize_t lottery_latency_read(struct file *filp, char __user *buf,
				size_t count, loff_t *ppos)
{
	unsigned long long iteration;
	unsigned long long latency;
	unsigned long long latency_per_cycle;;
	char buffer[200];
	lottery_get_latency(&iteration, &latency);

	if (length != 0)
		return 0;
	if (iteration == 0)
		latency_per_cycle = 0;
	else
		latency_per_cycle = latency / iteration;

	length = snprintf(buffer, count, "Iteration-> %llu\nLatency -> %llu NS\nLatency_Per_Iteration -> %lluNS\n", 
			iteration, latency, latency_per_cycle);

	copy_to_user(buf, buffer, length);
	return length;
}


static ssize_t lottery_log_write(struct file *filp, const char __user *buf,
				size_t count, loff_t *ppos)
{
	struct lottery_event_log *log = NULL;
	log = get_lottery_event_log();
	log->cursor=log->head=log->tail=log->size = 0;
	return count;	
}

static ssize_t lottery_log_read(struct file *filep, const char __user *buf,
				size_t count, loff_t *ppos)
{
        char buffer[LOTTERY_MSG_SIZE];
        unsigned int len=0,k,i,lines=0, idx=0, ctail;
        struct lottery_event_log *log=NULL;
        buffer[0]='\0';
        log=get_lottery_event_log();
        if(log){
		//printk(KERN_INFO "h:%u t:%u c:%u s:%u\n", log->head, log->tail, log->cursor, log->size);
		if(log->cursor == log->tail)
		{
			if(log->size < LOTTERY_MAX_EVENT_LINES)
				goto exit;
			else if(log->size == LOTTERY_MAX_EVENT_LINES)
			{
				if(log->flag > 0) goto exit;
				log->flag = 1;
			}
		}

		idx = log->cursor;
		len = snprintf(buffer, count, "%s%d,%llu,%s\n",
                       	    buffer,
                            log->lottery_event[idx].action,
                            log->lottery_event[idx].timestamp,
                            log->lottery_event[idx].msg);
		idx = (idx + 1) % LOTTERY_MAX_EVENT_LINES;
                log->cursor=idx;
                if(len)
                        copy_to_user(buf,buffer,len);

        }

exit:
	//printk(KERN_INFO "h:%u t:%u c:%u s:%u %u\n", log->head, log->tail, log->cursor, log->size, len);
        return len;
}

/*
void create_lottery_process_entry(pid_t pid)
{
	char str[10];
	int count = 0;
	while (pid) {
		pid = pid / 10;
		count++;
	}

	str[count] = '\0';
	while (count--) {
		str[count] = pid % 10;
		pid = pid / 10;
	}

	proc_create(str, 0, lottery_dir, &proc_lottery_operations);
}
*/

static const struct file_operations proc_lottery_latency_operations = {
        .open           = lottery_open,
        .read           = lottery_latency_read,
        .write           = lottery_latency_write,
        .release        = lottery_release,
};

static const struct file_operations proc_lottery_log_operations = {
        .open           = lottery_open,
        .read           = lottery_log_read,
        .write           = lottery_log_write,
        .release        = lottery_release,
};

static void create_lottery_latency_entry(void)
{
	proc_create("latency", 0, lottery_dir, &proc_lottery_latency_operations);
}

static void create_lottery_log_entry() {
	proc_create("log", 0, lottery_dir, &proc_lottery_log_operations);
}


int __init proc_lottery_tasks_init(void)
{
	lottery_dir = proc_mkdir("lottery", NULL);

	create_lottery_latency_entry();
	create_lottery_log_entry();

	return 0;
}
module_init(proc_lottery_tasks_init);
#endif
