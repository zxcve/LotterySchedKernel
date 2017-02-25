/*
 * lottery-task scheduling class.
 *
 * 
 */
/*
 * log functions.
 */
#include <linux/random.h>
#include <linux/list.h>

unsigned long long max_tickets;

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
		printk(KERN_ALERT "register_lottery_event: full\n");
	}

}
/*
 *lottery tasks and lottery rq
 */
void init_lottery_rq(struct lottery_rq *lottery_rq)
{
	INIT_LIST_HEAD(&lottery_rq->lottery_list_head);
	atomic_set(&lottery_rq->nr_running,0);
}


/*
static struct sched_lottery_entity * conduct_lottery(struct lottery_rq *rq)
{
	struct list_head *ptr=NULL;
	struct sched_lottery_entity *lottery_task=NULL;
	unsigned long iterator = 0;
	unsigned long lottery;

	if (max_tickets > 0)
		lottery = get_random_int() % max_tickets;
	else
		return NULL;

	list_for_each(ptr,&rq->lottery_runnable_head){
		lottery_task=list_entry(ptr,struct sched_lottery_entity, lottery_runnable_node);

		iterator += lottery_task->tickets;

		if (iterator > lottery) {
			printk("ticket %lu winner %llu total %d \n", lottery, lottery_task->tickets, rq->nr_running);
			return lottery_task;
		}
	}
	return NULL;
}
*/

static struct task_struct *pick_next_task_lottery(struct rq *rq);

static void check_preempt_curr_lottery(struct rq *rq, struct task_struct *p, int flags)
{
	
	struct task_struct *t=NULL;
	t=pick_next_task_lottery(rq);
	if(t){
		if(p != t) {
			resched_task(rq->curr);
		}
	}
}

static struct task_struct *pick_next_task_lottery(struct rq *rq)
{
	//declare variables
	struct lottery_rq *lrq = &rq->lrq;
	struct list_head *queue = &lrq->lottery_list_head;
	struct list_head *temp;	
	struct task_struct *p=NULL, *next=NULL;
	struct lottery_entity *e;
	unsigned long totalTickets = 0;
	unsigned long luckyTicket = 0;
	unsigned long totalSoFar = 0;

	//Count total number of tickets
	temp=queue;
	list_for_each(temp,queue){
		e = list_entry(temp,struct lottery_entity,lottery_node);
		p = e->t;
		totalTickets+=p->numberOfTickets;
	}

	//Draw a lucky ticket randomly
	do{
		get_random_bytes(&luckyTicket,sizeof(unsigned long));
		if(totalTickets==0){
			return NULL;
		}
		luckyTicket = (luckyTicket%totalTickets)+1;
	}while(luckyTicket<0);

	//Search a process with the lucky ticket
	temp=queue;
	list_for_each(temp,queue){
		e = list_entry(temp,struct lottery_entity,lottery_node);
		p = e->t;
		totalSoFar += p->numberOfTickets;
		if(totalSoFar>=luckyTicket) 
			break;
	}

	//Set the next process to the process that has the lucky ticket
	next=p;
	return next;
}

static void enqueue_task_lottery(struct rq *rq, struct task_struct *p, int wakeup, bool head)
{
	char msg[LOTTERY_MSG_SIZE];
	if(p){
		max_tickets += p->numberOfTickets;
		struct lottery_entity *new =(struct lottery_entity *) kzalloc(sizeof(struct lottery_entity),GFP_KERNEL);
		new->t = p;
		list_add_tail(&new->lottery_node , &rq->lrq.lottery_list_head);
		atomic_inc(&rq->lrq.nr_running);
		snprintf(msg,LOTTERY_MSG_SIZE,"(%d:%llu)",p->pid,p->numberOfTickets); 
		register_lottery_event(sched_clock(), msg, LOTTERY_ENQUEUE);
	}
}

static void dequeue_task_lottery(struct rq *rq, struct task_struct *p, int sleep)
{
	struct lottery_entity *t=NULL;
	char msg[LOTTERY_MSG_SIZE];
	struct lottery_rq *lrq = &rq->lrq;
        struct list_head *queue = &lrq->lottery_list_head;
        struct list_head *temp, *next;
        struct lottery_entity *e;
	if(p){
		snprintf(msg,LOTTERY_MSG_SIZE,"(%d:%llu)",p->pid,p->numberOfTickets); 
		register_lottery_event(sched_clock(), msg, LOTTERY_DEQUEUE);	
		atomic_dec(&rq->lrq.nr_running);
		max_tickets -= p->numberOfTickets;
		list_for_each_safe(temp,next,queue){
			e = list_entry(temp,struct lottery_entity,lottery_node);
			if(e->t == p) {
				list_del_init(temp);
				break;
			}
		}
	}
}

static void put_prev_task_lottery(struct rq *rq, struct task_struct *prev)
{

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
/*
	struct sched_lottery_entity *t=NULL;

	t=conduct_lottery(&rq->lottery_rq);
	if(t){
		if(&p->lt != t) {
			resched_task(rq->curr);
		}
	}
	*/
}

static void set_curr_task_lottery(struct rq *rq)
{

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
	struct lottery_entity *t=NULL;
        char msg[LOTTERY_MSG_SIZE];
        struct lottery_rq *lrq = &rq->lrq;
        struct list_head *queue = &lrq->lottery_list_head;
        struct list_head *temp, *next;
        struct lottery_entity *e;
       	list_for_each_safe(temp,next,queue){
		e = list_entry(temp,struct lottery_entity,lottery_node);
                list_del_init(temp);
                break;
        }
	list_add_tail(&e->lottery_node , &rq->lrq.lottery_list_head);
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
