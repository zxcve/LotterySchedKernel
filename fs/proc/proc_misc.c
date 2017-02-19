/*
 *  linux/fs/proc/proc_misc.c
 *
 *  linux/fs/proc/array.c
 *  Copyright (C) 1992  by Linus Torvalds
 *  based on ideas by Darren Senn
 *
 *  This used to be the part of array.c. See the rest of history and credits
 *  there. I took this into a separate file and switched the thing to generic
 *  proc_file_inode_operations, leaving in array.c only per-process stuff.
 *  Inumbers allocation made dynamic (via create_proc_entry()).  AV, May 1999.
 *
 * Changes:
 * Fulton Green      :  Encapsulated position metric calculations.
 *                      <kernel@FultonGreen.com>
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

static int lottery_open(struct inode *inode, struct file *file)
{
        return 0;
}

static int lottery_read(char *filp, char *buf, size_t count, loff_t *f_pos)
{
        char buffer[LOTTERY_MSG_SIZE];
        unsigned int len=0,k,i;
        struct lottery_event_log *log=NULL;
        buffer[0]='\0';
        log=get_lottery_event_log();
        if(log){
                if(log->cursor < log->lines){
                        k=(log->lines > (log->cursor + LOTTERY_MAX_CURSOR_LINES_EVENTS))?(log->cursor + LOTTERY_MAX_CURSOR_LINES_EVENTS):(log->lines);
                        for(i=log->cursor; i<k;i++){
                                len = snprintf(buffer, count, "%s%d,%llu,%s\n",
                                        buffer,
                                        log->lottery_event[i].action,
                                        log->lottery_event[i].timestamp,
                                        log->lottery_event[i].msg);
                        }
                        log->cursor=k;
                }
                if(len) 
                        copy_to_user(buf,buffer,len);

        }
        return len;
}

static int lottery_release(struct inode *inode, struct file *file)
{
        return 0;
}

static const struct file_operations proc_lottery_operations = {
        .open           = lottery_open,
        .read           = lottery_read,
        .release        = lottery_release,
};

int __init proc_lottery_init(void)
{
        /*struct proc_dir_entry *lottery_entry;

        lottery_entry = create_proc_entry("lottery_event", 0666, &proc_root);

        if (lottery_entry){
                lottery_entry->proc_fops = &proc_lottery_operations;
                lottery_entry->data=NULL;
		return 0;
        }

	return -1;*/

	proc_create("lottery_event", 0, NULL, &proc_lottery_operations);
        return 0;
}
module_init(proc_lottery_init);
#endif

