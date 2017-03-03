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

#include <linux/random.h>
#include <linux/rbtree_augmented.h>
#include <linux/proc_lottery.h>

/**
 * @brief event buffer
 */
static struct lottery_event_log lottery_event_log;

/**
 * @brief Structure to hold statistics informations
 */
static struct lottery_stats stats;

/*Static declarations*/
void lottery_log(enum lottery_action action, char* format, ...);

/**
 * @brief Updates the start time and total run time
 *
 * @param rq Pointer to the run queue
 */
static void update_curr_lottery(struct rq* rq)
{
	struct task_struct *curr = rq->curr;
	u64 delta_exec;

	delta_exec = rq->clock - curr->se.exec_start;
	if (unlikely((s64)delta_exec < 0))
		delta_exec = 0;

	curr->se.sum_exec_runtime += delta_exec;

	curr->se.exec_start = rq->clock;
}


/**
 * @brief Registers the event to the event log
 *
 * @param t Timestamp for the log
 * @param a Action for the log
 * @param format Printf style format for the log
 * @param a_list Variable list of arguments
 */
static void register_lottery_event(unsigned long long t,
				   enum lottery_action  a,
				   char *format, va_list a_list)
{
	lottery_event_log.lottery_event[lottery_event_log.tail].action=a;
	lottery_event_log.lottery_event[lottery_event_log.tail].timestamp=t;

	vsnprintf(lottery_event_log.lottery_event[lottery_event_log.tail].msg,
		  LOTTERY_MSG_SIZE-1, format, a_list);

	lottery_event_log.tail = (lottery_event_log.tail + 1) %
		LOTTERY_MAX_EVENT_LINES;

	if(lottery_event_log.size == LOTTERY_MAX_EVENT_LINES) {
		if (lottery_event_log.cursor == lottery_event_log.head) {
			lottery_event_log.cursor =
				(lottery_event_log.cursor + 1) %
				LOTTERY_MAX_EVENT_LINES;
			lottery_event_log.flag = 0;
		}

		lottery_event_log.head =
			(lottery_event_log.head + 1) % LOTTERY_MAX_EVENT_LINES;
	} else {
		lottery_event_log.size++;
	}
}


/**
 * Functions for RbTree based run queue
 */
#ifndef CONFIG_SCHED_LOTTERY_RQ_LIST

/**
 * @brief Computes the total tickets in left subtree
 *
 * @param node Root for which left subtree ticket sum has to be calculated
 */
static inline unsigned long long
compute_subtree_left(struct sched_lottery_entity *node)
{
	struct sched_lottery_entity *tmp;

	if(unlikely(node == NULL))
		return 0;

	if (likely(node->lottery_rb_node.rb_left)) {
		tmp = rb_entry(node->lottery_rb_node.rb_left,
			       struct sched_lottery_entity, lottery_rb_node);

		return tmp->left_tickets + tmp->tickets + tmp->right_tickets;
	}
	return 0;
}


/**
 * @brief Computes the total tickets in right subtree
 *
 * @param node Root for which right subtree ticket sum has to be calculated
 */
static inline unsigned long long
compute_subtree_right(struct sched_lottery_entity *node)
{
	struct sched_lottery_entity *tmp;

	if (likely(node->lottery_rb_node.rb_right)) {
		tmp = rb_entry(node->lottery_rb_node.rb_right,
			       struct sched_lottery_entity, lottery_rb_node);

		return tmp->left_tickets + tmp->tickets + tmp->right_tickets;
	}
	return 0;
}

/**
 * @brief Propagates the left and right subtree ticket counts to root
 *
 * @param rb Node from where propagation has to start
 * @param stop Node at which propagation must stop (NULL for root)
 */
static void augment_propagate(struct rb_node *rb, struct rb_node *stop)
{
	while (rb != stop) {
		struct sched_lottery_entity *node =
			rb_entry(rb, struct sched_lottery_entity,
				 lottery_rb_node);

		node->left_tickets = compute_subtree_left(node);
		node->right_tickets = compute_subtree_right(node);

		rb = rb_parent(&node->lottery_rb_node);
	}
}

/**
 * @brief Copies the value from old root to new root while rotation
 *
 * @param rb_old Old root which got removed/rotated
 * @param rb_new New root which replaced old root
 */
static void augment_copy(struct rb_node *rb_old, struct rb_node *rb_new)
{
	struct sched_lottery_entity *old =
		rb_entry(rb_old, struct sched_lottery_entity, lottery_rb_node);
	struct sched_lottery_entity *new =
		rb_entry(rb_new, struct sched_lottery_entity, lottery_rb_node);

	new->left_tickets = old->left_tickets;
}

/**
 * @brief Performs update in left and right subtree ticket while rotation
 *
 * @param rb_old Old root which was rotated
 * @param rb_new New root which replaced old root
 */
static void augment_rotate(struct rb_node *rb_old, struct rb_node *rb_new)
{
	struct sched_lottery_entity *old =
		rb_entry(rb_old, struct sched_lottery_entity, lottery_rb_node);
	struct sched_lottery_entity *new =
		rb_entry(rb_new, struct sched_lottery_entity, lottery_rb_node);

	old->left_tickets = compute_subtree_left(old);
	old->right_tickets = compute_subtree_right(old);

	new->left_tickets = compute_subtree_left(new);
	new->right_tickets = compute_subtree_right(new);
}

/**
 * @brief Callbacks for Augmented rbtree to perform propagate, copy and rotate
 */
static const struct rb_augment_callbacks augment_callbacks = {
	augment_propagate, augment_copy, augment_rotate
};

/**
 * @brief Remove a node from rbtree run queue
 *
 * @param rq Pointer to the run queue
 * @param p Pointer to Lottery entity in task struct
 */
static void remove_lottery_task_rb_tree(struct lottery_rq *rq,
					struct sched_lottery_entity *p)
{
	rb_erase_augmented(&p->lottery_rb_node,
			   &rq->lottery_rb_root, &augment_callbacks);
}

/**
 * @brief Inserts a node to rbtree run queue
 *
 * @param rq Pointer to the run queue
 * @param p Pointer to Lottery entity in task struct
 */
static void insert_lottery_task_rb_tree(struct lottery_rq *rq,
					struct sched_lottery_entity *p)
{
	struct rb_node **link = &rq->lottery_rb_root.rb_node;
	struct rb_node *parent = NULL;
	struct sched_lottery_entity *myparent;

	/* Required for enqueue followed by dequeue */
	p->left_tickets = 0;
	p->right_tickets = 0;

	/* We dont care about equal key nodes as rotation of tree will take care
	 * of it and removal is always directly with pointer
	 */
	while (*link) {
		parent=*link;
		myparent = rb_entry(parent, struct sched_lottery_entity,
				    lottery_rb_node);
		if (myparent->tickets >= p->tickets) {
			myparent->left_tickets += p->tickets;
			link = &(*link)->rb_left;
		}
		else {
			myparent->right_tickets += p->tickets;
			link = &(*link)->rb_right;
		}
	}
	rb_link_node(&p->lottery_rb_node, parent, link);
	rb_insert_augmented(&p->lottery_rb_node,
			    &rq->lottery_rb_root, &augment_callbacks);
}
#endif

/**
 * @brief Conduct lottery for picking next suitable task
 *
 * @param trq Pointer to the run queue
 *
 * @return Pointer to Lottery entity which should be scheduled
 */
static struct sched_lottery_entity * conduct_lottery(struct rq *trq)
{
	unsigned long long lottery;
	struct lottery_rq *rq = &trq->lottery_rq;
	struct sched_lottery_entity *lottery_task=NULL;
#ifdef CONFIG_SCHED_LOTTERY_RQ_LIST
	struct list_head *ptr=NULL;
	unsigned long long iterator = 0;
#else
	struct rb_node *node = rq->lottery_rb_root.rb_node;
#endif

	if (likely(rq->max_tickets > 0)) {
		/* Creates a random number from 1 to max_tickets */
		get_random_bytes(&lottery, sizeof(unsigned long long));
		lottery = lottery % (rq->max_tickets - 1) + 1;
	}
	else {
		/* Required as linux periodically checks by calling if any task
		 * is ready to be scheduled.
		 */
		return NULL;
	}

#ifdef CONFIG_SCHED_LOTTERY_RQ_LIST
	/* Iterate across the list and get cumulative sum for each node.
	 * The winner will have cumulative sum greater than lottery_ticket.
	 */
	list_for_each(ptr,&rq->lottery_runnable_head){
		lottery_task=list_entry(ptr,struct sched_lottery_entity,
					lottery_runnable_node);

		iterator += lottery_task->tickets;

		if (iterator > lottery) {
			return lottery_task;
		}
	}
#else
	/* If left + curr_tickets is geq lottery_ticket then curr is winner.
	 * If lottery_ticket is leq than left_tickets then iterate in left
	 * direction.
	 * If lottery_ticket is greater then left_tickets + curr_tickets then
	 * iterate in right direction for lottery_ticket - (left_tickets +
	 * curr_tickets).
	 */
	while (node) {
		lottery_task = rb_entry(node, struct sched_lottery_entity,
					lottery_rb_node);
		if (lottery <= lottery_task->left_tickets)
			node = node->rb_left;
		else if (lottery <= (lottery_task->left_tickets +
				     lottery_task->tickets))
			return lottery_task;
		else {
			lottery -= (lottery_task->tickets +
				    lottery_task->left_tickets);
			node = node->rb_right;
		}
	}
#endif

	/* Should never hit */
	panic("No task found in run queue for lottery scheduling");
	return NULL;
}


/**
 * @brief If new process has more tickets than current process then ask for
 * reschedule
 *
 * @param rq Pointer to the run queue
 * @param p Task struct pointer for the new task
 * @param flags Not used
 */
static void check_preempt_curr_lottery(struct rq *rq,
				       struct task_struct *p, int flags)
{
	struct sched_lottery_entity *t=NULL;
	/* If more tickets then ask for resched */
	if(p->lt.tickets > rq->curr->lt.tickets) {
		lottery_log(LOTTERY_PREEMPT,
			    "Curr PID:%d with %llu tickets, Preempting PID:%d with %llu tickets",
			    rq->curr->pid, rq->curr->lt.tickets,
			    p->pid, p->lt.tickets);
		resched_task(rq->curr);
		stats.lottery_prempt++;
	}
}

/**
 * @brief Picks next task to run from lottery scheduler
 *
 * @param rq Pointer to the run queue
 *
 * @return Returns the pointer to task struct of selected task
 */
static struct task_struct *pick_next_task_lottery(struct rq *rq)
{
	struct sched_lottery_entity *t=NULL;
	unsigned long long old_time = sched_clock();

	t= conduct_lottery(rq);
	if(likely(t)){
		stats.lottery_latency += sched_clock() - old_time;
		stats.lottery_iteration++;
		t->task->se.exec_start = rq->clock;
		lottery_log(LOTTERY_PICK_TIME,
			    "PID:%d with %llu tickets",
			    t->task->pid, t->task->lt.tickets);
		return t->task;
	}
	return NULL;
}

/**
 * @brief Performs enqueue for the tasks in run queue
 *
 * @param rq Pointer to the run queue
 * @param p Pointer to the task struct to be enqueued
 * @param wakeup NOT USED
 * @param head NOT USED
 */
static void enqueue_task_lottery(struct rq *rq,
				 struct task_struct *p, int wakeup, bool head)
{
	if(likely(p)){
		rq->lottery_rq.max_tickets += p->lt.tickets;
#ifdef CONFIG_SCHED_LOTTERY_RQ_LIST
		list_add(&p->lt.lottery_runnable_node,
			 &rq->lottery_rq.lottery_runnable_head);
#else
		insert_lottery_task_rb_tree(&rq->lottery_rq, &p->lt);
#endif
		lottery_log(LOTTERY_ENQUEUE, "PID:%d with tickets %llu",
			    p->pid,p->lt.tickets);

		stats.lottery_enqueue++;
	}
}

/**
 * @brief Performs dequeue for the tasks in the run queue
 *
 * @param rq pointer to the run queue
 * @param p Pointer to the task struct to be dequeued
 * @param sleep NOT USED
 */
static void dequeue_task_lottery(struct rq *rq,
				 struct task_struct *p, int sleep)
{
	struct sched_lottery_entity *t=NULL;

	if(likely(p)){
		t = &p->lt;
		lottery_log(LOTTERY_DEQUEUE, "PID:%d with tickets %llu",p->pid,
			    t->tickets);

		update_curr_lottery(rq);
#ifdef CONFIG_SCHED_LOTTERY_RQ_LIST
		list_del(&(t->lottery_runnable_node));
#else
		remove_lottery_task_rb_tree(&rq->lottery_rq, t);
#endif
		rq->lottery_rq.max_tickets -= t->tickets;

		stats.lottery_dequeue++;
	}
}

/**
 * @brief Called for the prev task when the next task is picked
 *
 * @param rq pointer to the run queue
 * @param prev Pointer to the prev task
 */
static void put_prev_task_lottery(struct rq *rq, struct task_struct *prev)
{
	update_curr_lottery(rq);

	prev->se.exec_start = 0;
}


/**
 * @brief Called for every tick and re-schedules the current process every tick
 *
 * @param rq Pointer to the run queue
 * @param p NOT USED
 * @param queued NOT USED
 */
static void task_tick_lottery(struct rq *rq, struct task_struct *p, int queued)
{
	update_curr_lottery(rq);

	lottery_log(LOTTERY_TICK, "PID: %d with %llu tickets",
		    rq->curr->pid,
		    rq->curr->lt.tickets);

	/* Reschedule every tick, if current process is lucky then it will again
	 * execute
	 */
	resched_task(rq->curr);
}

/**
 * @brief Update start time for the current task
 *
 * @param rq Pointer to the run queue
 */
static void set_curr_task_lottery(struct rq *rq)
{
	rq->curr->se.exec_start = rq->clock;
}

/**
 * @brief Called when yield has to be performed
 *
 * @param rq Pointer to the run queue
 */
static void yield_task_lottery(struct rq *rq)
{
	update_curr_lottery(rq);

	/* Reschedules as it is going to sleep.
	 */
	resched_task(rq->curr);

	stats.lottery_yield++;
}

/**
 * @brief Called when priority has changed. We dont do any special handling as
 * enqueue and dequeue performed for changed priority handles our use-case.
 *
 * @param rq Pointer to the run queue.
 * @param p Pointer of the task struct of process whose priority changed.
 * @param oldprio NOT USED
 * @param running If the process is running then re-sched it.
 */
static void prio_changed_lottery(struct rq *rq, struct task_struct *p,
				 int oldprio, int running)
{
	update_curr_lottery(rq);

	if (running)
		resched_task(rq->curr);
}


#ifdef CONFIG_SMP
/**
 * @brief This function is not used as we dont support SMP
 */
static unsigned long load_balance_lottery(struct rq *this_rq,
					  int this_cpu, struct rq *busiest,
					  unsigned long max_load_move,
					  struct sched_domain *sd,
					  enum cpu_idle_type idle,
					  int *all_pinned, int *this_best_prio)
{
	return 0;
}

/**
 * @brief This function is not used as we dont support SMP
 */
static int move_one_task_lottery(struct rq *this_rq,
				 int this_cpu, struct rq *busiest,
				 struct sched_domain *sd,
				 enum cpu_idle_type idle)
{
	return 0;
}

/**
 * @brief This function is not used as we dont support SMP
 */
static int select_task_rq_lottery(struct rq *rq,
				  struct task_struct *p,
				  int sd_flag, int flags)
{
	if (sd_flag != SD_BALANCE_WAKE)
		return smp_processor_id();

	return task_cpu(p);
}

/**
 * @brief This function is not used as we dont support SMP
 */
static void set_cpus_allowed_lottery(struct task_struct *p,
				     const struct cpumask *new_mask)
{

}

/**
 * @brief This function is not used as we dont support SMP
 */
static void rq_online_lottery(struct rq *rq)
{

}

/**
 * @brief This function is not used as we dont support SMP
 */
static void rq_offline_lottery(struct rq *rq)
{

}

/**
 * @brief This function is not used as we dont support SMP
 */
static void pre_schedule_lottery(struct rq *rq, struct task_struct *prev)
{

}

/**
 * @brief This function is not used as we dont support SMP
 */
static void post_schedule_lottery(struct rq *rq)
{

}

/**
 * @brief This function is not used as we dont support SMP
 */
static void task_woken_lottery(struct rq *rq, struct task_struct *p)
{

}

/**
 * @brief This function is not used as we dont support SMP
 */
static void switched_from_lottery(struct rq *rq, struct task_struct *p,
				  int running)
{

}

#endif

/*No special handling when switched to lottery*/
static void switched_to_lottery(struct rq *rq, struct task_struct *p,
				int running)
{

}

/**
 * @brief Returns the timeslice for each task
 *
 * @param rq Pointer to the run queue
 * @param task Task for which time slice is needed
 *
 * @return HZ as each process runs for only 1 tick
 */
static unsigned int get_rr_interval_lottery(struct rq *rq,
					    struct task_struct *task)
{
	return HZ;
}

/**
 * @brief Returns the event log buffer head
 *
 * @return the head of the event log buffer
 */
struct lottery_event_log * get_lottery_event_log(void)
{
	return &lottery_event_log;
}

/**
 * @brief Resets the statistics structure
 */
void lottery_reset_stats(void) {
	stats.lottery_iteration = 0;
	stats.lottery_latency = 0;
	stats.lottery_enqueue = 0;
	stats.lottery_dequeue = 0;
	stats.lottery_yield = 0;
	stats.lottery_prempt = 0;
}

/**
 * @brief Queries the strucuture for statistics
 *
 * @return Returns the pointer for statistics
 */
struct lottery_stats *lottery_get_stats(void)
{
	return &stats;
}


/**
 * @brief Logs the event based on logging flag
 *
 * @param action Action to be logged
 * @param format Printf style formating for the log
 * @param ... Variable parameter
 */
void lottery_log(enum lottery_action action, char* format, ...)
{
#ifdef CONFIG_SCHED_LOTTERY_LOGGING
	va_list a_list;
	va_start(a_list, format);
	register_lottery_event(sched_clock(), action, format, a_list);
	va_end(a_list);
#endif
}

/**
 * @brief Initiliazes the event log head,tail,cursor
 */
void init_lottery_event_log(void)
{
	lottery_event_log.head = lottery_event_log.tail =
		lottery_event_log.cursor =
			lottery_event_log.size = lottery_event_log.flag = 0;
	lottery_log(LOTTERY_MSG, "Initialize event log for lottery scheduling");
}


/**
 * @brief Initializes the run queue
 *
 * @param lottery_rq Pointer to the run queue
 */
void init_lottery_rq(struct lottery_rq *lottery_rq)
{
#ifdef CONFIG_SCHED_LOTTERY_RQ_LIST
	INIT_LIST_HEAD(&lottery_rq->lottery_runnable_head);
#else
	lottery_rq->lottery_rb_root=RB_ROOT;
#endif
}


/*
 * Simple, special scheduling class for the per-CPU lottery tasks:
 */
static const struct sched_class lottery_sched_class = {
	.next			= &fair_sched_class,
	.enqueue_task		= enqueue_task_lottery,
	.dequeue_task		= dequeue_task_lottery,

	.check_preempt_curr	= check_preempt_curr_lottery,

	.pick_next_task		= pick_next_task_lottery,
	.put_prev_task		= put_prev_task_lottery,

#ifdef CONFIG_SMP
	.load_balance		= load_balance_lottery,
	.move_one_task		= move_one_task_lottery,

	.select_task_rq		= select_task_rq_lottery,
	.set_cpus_allowed       = set_cpus_allowed_lottery,
	.rq_online              = rq_online_lottery,
	.rq_offline             = rq_offline_lottery,
	.pre_schedule		= pre_schedule_lottery,
	.post_schedule		= post_schedule_lottery,
	.task_woken		= task_woken_lottery,
	.switched_from		= switched_from_lottery,
#endif

	.set_curr_task          = set_curr_task_lottery,
	.task_tick		= task_tick_lottery,

	.switched_to		= switched_to_lottery,

	.yield_task		= yield_task_lottery,
	.get_rr_interval	= get_rr_interval_lottery,

	.prio_changed		= prio_changed_lottery,
};
