/*
 * Lottery Scheduling
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/module.h>
#include <linux/proc_lottery.h>
#include <asm/uaccess.h>

#ifdef  CONFIG_SCHED_LOTTERY_POLICY

#define MAX_LOTTERY_STATS 1024

/**
 * @brief String for each action
 */
static char *action[] = {"ENQUEUE", "DEQUEUE", "CONTEXT_SWITCH", "PICK_NEXT", "PREEMPT", "TICK", "LOG"};

/**
 * @brief Directory entry for root of lottery proc entry
 */
static struct proc_dir_entry *lottery_dir;

/**
 * @brief Global data to manipulate file length
 */
static unsigned int length = 0;

/**
 * @brief Initializes the length to 0 while file open
 *
 * @param inode Pointer for the inode entry
 * @param file Pointer for the file
 *
 * @return Always 0.
 */
static int lottery_open(struct inode *inode, struct file *file)
{
	length = 0;
	return 0;
}

/**
 * @brief Initializes the length to 0 while file close
 *
 * @param inode Pointer for the inode entry
 * @param file Pointer for the file
 *
 * @return Always 0.
 */
static int lottery_release(struct inode *inode, struct file *file)
{
	length = 0;
	return 0;
}

/**
 * @brief Write resets the lottery stats data structures
 *
 * @param filp Pointer for the file
 * @param buf Buffer from user-space which is to be written
 * @param count Number of bytes to write
 * @param ppos Position in the file to write
 *
 * @return Number of bytes of data written
 */
static ssize_t lottery_stats_write(struct file *filp, const char __user *buf,
				   size_t count, loff_t *ppos)
{
	lottery_reset_stats();
	return count;
}

/**
 * @brief Reads the statistics information from lottery scheduler
 *
 * @param filp Pointer for the file
 * @param buf Buffer from user-space where read data is copied
 * @param count Number of bytes to read
 * @param ppos Position in the file to read
 *
 * @return Number of bytes read
 */
static ssize_t lottery_stats_read(struct file *filp, char __user *buf,
				  size_t count, loff_t *ppos)
{
	struct lottery_stats *stats;
	unsigned long long latency_per_cycle;;
	char *buffer = NULL;

	if(unlikely(!access_ok(VERIFY_WRITE, buf, count)))
		return -1;

	if (length != 0)
		return 0;

	stats = lottery_get_stats();

	/* Avoid divide by 0 crash */
	if (unlikely(stats->lottery_iteration == 0))
		latency_per_cycle = 0;
	else
		latency_per_cycle = stats->lottery_latency /
					stats->lottery_iteration;

	buffer = kmalloc (MAX_LOTTERY_STATS, GFP_KERNEL);
	if (unlikely(!buffer))
		return 0;
	length += snprintf(buffer, count, "%21s %12llu\n%21s %12llu NS\n%21s %12llu NS\n%21s %12llu\n%21s %12llu\n%21s %12llu\n%21s %12llu\n",
			   "PickNextTask",
			  stats->lottery_iteration,
			   "Latency",
			  stats->lottery_latency,
			  "Latency/PickNextTask",
			  latency_per_cycle,
			  "Enqueue",
			  stats->lottery_enqueue,
			  "Dequeue",
			  stats->lottery_dequeue,
			  "Yield",
			  stats->lottery_yield,
			  "Preempt",
			  stats->lottery_prempt);

	copy_to_user(buf, buffer, length);

	if (buffer)
		kfree(buffer);
	return length;
}


/**
 * @brief Write resets the lottery log data structures
 *
 * @param filp Pointer for the file
 * @param buf Buffer from user-space which is to be written
 * @param count Number of bytes to write
 * @param ppos Position in the file to write
 *
 * @return Number of bytes of data written
 */
static ssize_t lottery_log_write(struct file *filp, const char __user *buf,
				 size_t count, loff_t *ppos)
{
	struct lottery_event_log *log = NULL;

	log = get_lottery_event_log();

	/* Reset the head,tail,size,flag & cursors */
	log->cursor=log->head=
		log->tail=log->size=log->flag=0;

	return count;
}

/**
 * @brief Reads the log for the lottery scheduler
 *
 * @param filp Pointer for the file
 * @param buf Buffer from user-space which is to be written
 * @param count Number of bytes to write
 * @param ppos Position in the file to write
 *
 * @return Number of bytes of data written
 *
 * @return
 */
static ssize_t lottery_log_read(struct file *filep, const char __user *buf,
				size_t count, loff_t *ppos)
{
	char buffer[LOTTERY_MSG_SIZE];
	unsigned int len=0,idx=0;
	struct lottery_event_log *log=NULL;

	buffer[0]='\0';

	log=get_lottery_event_log();

	if(likely(log)){
		if(unlikely(!access_ok(VERIFY_WRITE, buf, count)))
			return -1;

		if(log->cursor == log->tail) {
			if(log->size < LOTTERY_MAX_EVENT_LINES)
				return len;
			else if(log->size == LOTTERY_MAX_EVENT_LINES) {
				if(log->flag > 0)
					return len;
				log->flag = 1;
			}
		}

		idx = log->cursor;
		len = snprintf(buffer, count, "%s[%llu] <%s>  {%s}\n",
			       buffer,
			       log->lottery_event[idx].timestamp,
			       action[log->lottery_event[idx].action],
			       log->lottery_event[idx].msg);
		idx = (idx + 1) % LOTTERY_MAX_EVENT_LINES;
		log->cursor=idx;
		if(len)
			copy_to_user(buf,buffer,len);

	}

	return len;
}

/**
 * @brief Handles for read/write stats proc entry
 */
static const struct file_operations proc_lottery_stats_operations = {
	.open           = lottery_open,
	.read           = lottery_stats_read,
	.write           = lottery_stats_write,
	.release        = lottery_release,
};

/**
 * @brief Handles for read/write lottery event logs
 */
static const struct file_operations proc_lottery_log_operations = {
	.open           = lottery_open,
	.read           = lottery_log_read,
	.write           = lottery_log_write,
	.release        = lottery_release,
};

/**
 * @brief Creates new proc fs stats entry
 */
static void create_lottery_stats_entry(void)
{
	proc_create("stats", 0, lottery_dir, &proc_lottery_stats_operations);
}

/**
 * @brief Creates new log fs entry
 */
static void create_lottery_log_entry() {
	proc_create("log", 0, lottery_dir, &proc_lottery_log_operations);
}


/**
 * @brief Called when init is called for this module
 *
 * @return Always 0
 */
int __init proc_lottery_tasks_init(void)
{
	lottery_dir = proc_mkdir("lottery", NULL);

	create_lottery_stats_entry();
	create_lottery_log_entry();

	return 0;
}

/**
 * @brief Registers the function to be called during init
 *
 * @param proc_lottery_tasks_init
 */
module_init(proc_lottery_tasks_init);

#endif
