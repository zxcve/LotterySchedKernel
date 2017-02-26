/*
 * lottery-task scheduling class.
 *
 * 
 */
/*
 * log functions.
 */
#include <linux/random.h>
#include <linux/rbtree_augmented.h>
#include <linux/latencytop.h>

//#define USE_LIST
struct lottery_event_log lottery_event_log;

struct lottery_event_log * get_lottery_event_log(void)
{
	return &lottery_event_log;
}
void init_lottery_event_log(void)
{
	char msg[LOTTERY_MSG_SIZE];
	lottery_event_log.lines=lottery_event_log.cursor=0;
	snprintf(msg,LOTTERY_MSG_SIZE,"init_lottery_event_log:(%lu:%lu)", lottery_event_log.lines, lottery_event_log.cursor); 
	register_lottery_event(sched_clock(), msg, LOTTERY_MSG);

}
void register_lottery_event(unsigned long long t, char *m, int a)
{

	if(lottery_event_log.lines < LOTTERY_MAX_EVENT_LINES){
		lottery_event_log.lottery_event[lottery_event_log.lines].action=a;
		lottery_event_log.lottery_event[lottery_event_log.lines].timestamp=t;
		strncpy(lottery_event_log.lottery_event[lottery_event_log.lines].msg,m,LOTTERY_MSG_SIZE-1);
		lottery_event_log.lines++;
	}
	else{
	//	printk(KERN_ALERT "register_lottery_event: full\n");
	}

}

/*
 *lottery tasks and lottery rq
 */
void init_lottery_rq(struct lottery_rq *lottery_rq)
{
#ifdef USE_LIST
	INIT_LIST_HEAD(&lottery_rq->lottery_runnable_head);
#else
	lottery_rq->lottery_rb_root=RB_ROOT;
#endif
	atomic_set(&lottery_rq->nr_running,0);
}

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

/*
 * rb_tree functions.
 */

#ifndef USE_LIST

static inline unsigned long long
compute_subtree_left(struct sched_lottery_entity *node)
{
	struct sched_lottery_entity *tmp;
	if (node->lottery_rb_node.rb_left) {
		tmp = rb_entry(node->lottery_rb_node.rb_left,
			struct sched_lottery_entity, lottery_rb_node);
		return tmp->left_tickets + tmp->tickets + tmp->right_tickets;
	}
	return 0;
}


static inline unsigned long long
compute_subtree_right(struct sched_lottery_entity *node)
{
	struct sched_lottery_entity *tmp;
	if (node->lottery_rb_node.rb_right) {
		tmp = rb_entry(node->lottery_rb_node.rb_right,
			struct sched_lottery_entity, lottery_rb_node);
		return tmp->left_tickets + tmp->tickets + tmp->right_tickets;
	}
	return 0;
}


static void augment_propagate(struct rb_node *rb, struct rb_node *stop)
{
	while (rb != stop) {
		struct sched_lottery_entity *node =
			rb_entry(rb, struct sched_lottery_entity, lottery_rb_node);

		node->left_tickets = compute_subtree_left(node);
		node->right_tickets = compute_subtree_right(node);

		rb = rb_parent(&node->lottery_rb_node);
	}
}

static void augment_copy(struct rb_node *rb_old, struct rb_node *rb_new)
{
	struct sched_lottery_entity *old =
		rb_entry(rb_old, struct sched_lottery_entity, lottery_rb_node);
	struct sched_lottery_entity *new =
		rb_entry(rb_new, struct sched_lottery_entity, lottery_rb_node);

	new->left_tickets = old->left_tickets;
}

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

static const struct rb_augment_callbacks augment_callbacks = {
	augment_propagate, augment_copy, augment_rotate
};

void remove_lottery_task_rb_tree(struct lottery_rq *rq, struct sched_lottery_entity *p)
{
	rb_erase_augmented(&p->lottery_rb_node, &rq->lottery_rb_root, &augment_callbacks);
}

void insert_lottery_task_rb_tree(struct lottery_rq *rq, struct sched_lottery_entity *p)
{
	struct rb_node **link = &rq->lottery_rb_root.rb_node;
	struct rb_node *parent = NULL;
	struct sched_lottery_entity *myparent;

	p->left_tickets = 0;
	p->right_tickets = 0;

	while (*link) {
		parent=*link;
		myparent = rb_entry(parent, struct sched_lottery_entity, lottery_rb_node);
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
	rb_insert_augmented(&p->lottery_rb_node, &rq->lottery_rb_root, &augment_callbacks);
}
#endif

static struct sched_lottery_entity * conduct_lottery(struct rq *trq)
{
	unsigned long long lottery;
	unsigned long long lottery_t;
	struct lottery_rq *rq = &trq->lottery_rq;
	struct sched_lottery_entity *lottery_task=NULL;
	char msg[LOTTERY_MSG_SIZE];
#ifdef USE_LIST
	struct list_head *ptr=NULL;
	unsigned long long iterator = 0;
#else
	struct rb_node *node = rq->lottery_rb_root.rb_node;
#endif
	if ( rq->max_tickets > 0) {
		get_random_bytes(&lottery, sizeof(unsigned long long));
		lottery = lottery % (rq->max_tickets - 1) + 1;
	}
	else
		return NULL;
#ifdef USE_LIST
	list_for_each(ptr,&rq->lottery_runnable_head){
		lottery_task=list_entry(ptr,struct sched_lottery_entity, lottery_runnable_node);

		iterator += lottery_task->tickets;

		if (iterator > lottery) {
			return lottery_task;
		}
	}
#else
	lottery_t = lottery;
	while (node) {
		lottery_task = rb_entry(node, struct sched_lottery_entity, lottery_rb_node);
		if (lottery <= lottery_task->left_tickets)
			node = node->rb_left;
		else if (lottery <= (lottery_task->left_tickets + lottery_task->tickets))
			return lottery_task;
		else {
			lottery -= (lottery_task->tickets + lottery_task->left_tickets);
			node = node->rb_right;
		}
	}
	lottery_task = rb_entry(rq->lottery_rb_root.rb_node, struct sched_lottery_entity, lottery_rb_node);
	snprintf(msg, LOTTERY_MSG_SIZE,
			"found null lot %llu max %llu root %llu %llu %llu \n", lottery_t, rq->max_tickets, lottery_task->tickets, lottery_task->left_tickets, lottery_task->right_tickets);
	register_lottery_event(sched_clock(), msg, LOTTERY_PICK_TIME);

	lottery = lottery_t;
	node = rq->lottery_rb_root.rb_node;
	while (node) {
		lottery_task = rb_entry(node, struct sched_lottery_entity, lottery_rb_node);
		snprintf(msg, LOTTERY_MSG_SIZE, "lottery %llu val %llu left %llu right %llu",
				lottery, lottery_task->tickets, lottery_task->left_tickets, lottery_task->right_tickets);
		register_lottery_event(sched_clock(), msg, LOTTERY_PICK_TIME);
		if (lottery <= lottery_task->left_tickets)
			node = node->rb_left;
		else if (lottery <= (lottery_task->left_tickets + lottery_task->tickets))
			return lottery_task;
		else {
			lottery -= (lottery_task->tickets + lottery_task->left_tickets);
			node = node->rb_right;
		}
	}

#endif
	return NULL;
}


static void check_preempt_curr_lottery(struct rq *rq, struct task_struct *p, int flags)
{
	struct sched_lottery_entity *t=NULL;
	char msg[LOTTERY_MSG_SIZE];
	t=conduct_lottery(rq);
	if(t){
		if(&p->lt != t) {
			snprintf(msg,LOTTERY_MSG_SIZE, "prempt %d\n", rq->curr->pid);
			register_lottery_event(sched_clock(), msg, LOTTERY_PICK_TIME);
			resched_task(rq->curr);
		}
	}
}

unsigned long long iteration = 0;
unsigned long long curr_time = 0;

static struct task_struct *pick_next_task_lottery(struct rq *rq)
{
	struct sched_lottery_entity *t=NULL;
	unsigned long long old_time = sched_clock();
	char msg[LOTTERY_MSG_SIZE];
	struct task_struct *task;

	t= conduct_lottery(rq);
	if(t){
		curr_time += sched_clock() - old_time;
		iteration++;
		if (iteration % 1000 == 0)  {
			snprintf(msg,LOTTERY_MSG_SIZE, "iteration->%llu pick_time -> %llu /iteration curr %d winner %d", iteration, curr_time/iteration, rq->curr->pid, t->task->pid); 
			register_lottery_event(sched_clock(), msg, LOTTERY_PICK_TIME);
		}
		t->task->se.exec_start = rq->clock;
		return t->task;
	}
	return NULL;
}

static void enqueue_task_lottery(struct rq *rq, struct task_struct *p, int wakeup, bool head)
{
	char msg[LOTTERY_MSG_SIZE];
	if(p){
		rq->lottery_rq.max_tickets += p->lt.tickets;
#ifdef USE_LIST
		list_add(&p->lt.lottery_runnable_node,&rq->lottery_rq.lottery_runnable_head);
#else
		insert_lottery_task_rb_tree(&rq->lottery_rq, &p->lt);
#endif
		atomic_inc(&rq->lottery_rq.nr_running);
		snprintf(msg,LOTTERY_MSG_SIZE,"(%d:%llu)",p->pid,p->lt.tickets); 
		register_lottery_event(sched_clock(), msg, LOTTERY_ENQUEUE);
	}
}

static void dequeue_task_lottery(struct rq *rq, struct task_struct *p, int sleep)
{
	struct sched_lottery_entity *t=NULL;
	char msg[LOTTERY_MSG_SIZE];
	if(p){
		t = &p->lt;
		snprintf(msg,LOTTERY_MSG_SIZE,"(%d:%llu)",p->pid,t->tickets); 
		register_lottery_event(sched_clock(), msg, LOTTERY_DEQUEUE);
		update_curr_lottery(rq);
#ifdef USE_LIST
		list_del(&(t->lottery_runnable_node));
#else
		remove_lottery_task_rb_tree(&rq->lottery_rq, t);
#endif
		atomic_dec(&rq->lottery_rq.nr_running);
		rq->lottery_rq.max_tickets -= t->tickets;
	}
}

static void put_prev_task_lottery(struct rq *rq, struct task_struct *prev)
{
	update_curr_lottery(rq);
	prev->se.exec_start = 0;
}

#ifdef CONFIG_SMP
static unsigned long load_balance_lottery(struct rq *this_rq, int this_cpu, struct rq *busiest,
		  unsigned long max_load_move,
		  struct sched_domain *sd, enum cpu_idle_type idle,
		  int *all_pinned, int *this_best_prio)
{
	return 0;
}

static int move_one_task_lottery(struct rq *this_rq, int this_cpu, struct rq *busiest,
		   struct sched_domain *sd, enum cpu_idle_type idle)
{
	return 0;
}
#endif

static void task_tick_lottery(struct rq *rq, struct task_struct *p, int queued)
{
	char msg[LOTTERY_MSG_SIZE];

	update_curr_lottery(rq);
//	snprintf(msg,LOTTERY_MSG_SIZE, "tick %d \n", rq->curr->pid);
//	register_lottery_event(sched_clock(), msg, LOTTERY_PICK_TIME);
	resched_task(rq->curr);
}

static void set_curr_task_lottery(struct rq *rq)
{
	rq->curr->se.exec_start = rq->clock;
}


/*
 * When switching a task to RT, we may overload the runqueue
 * with RT tasks. In this case we try to push them off to
 * other runqueues.
 */
static void switched_to_lottery(struct rq *rq, struct task_struct *p,
                           int running)
{
        /*
         * If we are already running, then there's nothing
         * that needs to be done. But if we are not running
         * we may need to preempt the current running task.
         * If that current running task is also an RT task
         * then see if we can move to another run queue.
         */
}


unsigned int get_rr_interval_lottery(struct rq *rq, struct task_struct *task)
{
	/*
         * Time slice is 0 for SCHED_FIFO tasks
         */
        if (task->policy == SCHED_RR)
                return DEF_TIMESLICE;
        else
                return 0;
}

static void yield_task_lottery(struct rq *rq)
{
	update_curr_lottery(rq);
}


/*
 * Priority of the task has changed. This may cause
 * us to initiate a push or pull.
 */
static void prio_changed_lottery(struct rq *rq, struct task_struct *p,
			    int oldprio, int running)
{

}

static int select_task_rq_lottery(struct rq *rq, struct task_struct *p, int sd_flag, int flags)
{

//	struct rq *rq = task_rq(p);

	if (sd_flag != SD_BALANCE_WAKE)
		return smp_processor_id();

	return task_cpu(p);
}


static void set_cpus_allowed_lottery(struct task_struct *p,
				const struct cpumask *new_mask)
{

}

/* Assumes rq->lock is held */
static void rq_online_lottery(struct rq *rq)
{

}

/* Assumes rq->lock is held */
static void rq_offline_lottery(struct rq *rq)
{

}

static void pre_schedule_lottery(struct rq *rq, struct task_struct *prev)
{

}

static void post_schedule_lottery(struct rq *rq)
{

}
/*
 * If we are not running and we are not going to reschedule soon, we should
 * try to push tasks away now
 */
static void task_woken_lottery(struct rq *rq, struct task_struct *p)
{
/*        if (!task_running(rq, p) &&
            !test_tsk_need_resched(rq->curr) &&
            has_pushable_tasks(rq) &&
            p->rt.nr_cpus_allowed > 1)
                push_rt_tasks(rq);
*/
}

/*
 * When switch from the rt queue, we bring ourselves to a position
 * that we might want to pull RT tasks from other runqueues.
 */
static void switched_from_lottery(struct rq *rq, struct task_struct *p,
			   int running)
{

}

/*
 * Simple, special scheduling class for the per-CPU lottery tasks:
 */
static const struct sched_class lottery_sched_class = {
	.next 			= &fair_sched_class,
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
