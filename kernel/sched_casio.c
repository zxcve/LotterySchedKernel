/*
 * casio-task scheduling class.
 *
 * 
 */
/*
 * log functions.
 */

struct casio_event_log casio_event_log;

struct casio_event_log * get_casio_event_log(void)
{
	return &casio_event_log;
}
void init_casio_event_log(void)
{
	char msg[CASIO_MSG_SIZE];
	casio_event_log.lines=casio_event_log.cursor=0;
	snprintf(msg,CASIO_MSG_SIZE,"init_casio_event_log:(%lu:%lu)", casio_event_log.lines, casio_event_log.cursor); 
	register_casio_event(sched_clock(), msg, CASIO_MSG);

}
void register_casio_event(unsigned long long t, char *m, int a)
{

	if(casio_event_log.lines < CASIO_MAX_EVENT_LINES){
		casio_event_log.casio_event[casio_event_log.lines].action=a;
		casio_event_log.casio_event[casio_event_log.lines].timestamp=t;
		strncpy(casio_event_log.casio_event[casio_event_log.lines].msg,m,CASIO_MSG_SIZE-1);
		casio_event_log.lines++;
	}
	else{
		printk(KERN_ALERT "register_casio_event: full\n");
	}

}
/*
 *casio tasks and casio rq
 */
void init_casio_rq(struct casio_rq *casio_rq)
{
	casio_rq->casio_rb_root=RB_ROOT;
	INIT_LIST_HEAD(&casio_rq->casio_list_head);
	atomic_set(&casio_rq->nr_running,0);
}
void add_casio_task_2_list(struct casio_rq *rq, struct task_struct *p)
{
	struct list_head *ptr=NULL;
	struct casio_task *new=NULL, *casio_task=NULL;
	char msg[CASIO_MSG_SIZE];
	if(rq && p){
		new=(struct casio_task *) kzalloc(sizeof(struct casio_task),GFP_KERNEL);
		if(new){
			casio_task=NULL;
			new->task=p;
			new->absolute_deadline=0;
			list_for_each(ptr,&rq->casio_list_head){
				casio_task=list_entry(ptr,struct casio_task, casio_list_node);
				if(casio_task){
					if(new->task->casio_id < casio_task->task->casio_id){
						list_add(&new->casio_list_node,ptr);
					}
				}
			}
			list_add(&new->casio_list_node,&rq->casio_list_head);
			snprintf(msg,CASIO_MSG_SIZE,"add_casio_task_2_list: %d:%d:%llu",new->task->casio_id,new->task->pid,new->absolute_deadline); 
			register_casio_event(sched_clock(), msg, CASIO_MSG);
		}
		else{
			printk(KERN_ALERT "add_casio_task_2_list: kzalloc\n");
		}
	}
	else{
		printk(KERN_ALERT "add_casio_task_2_list: null pointers\n");
	}
}
struct casio_task * find_casio_task_list(struct casio_rq *rq, struct task_struct *p)
{
	struct list_head *ptr=NULL;
	struct casio_task *casio_task=NULL;
	if(rq && p){
		list_for_each(ptr,&rq->casio_list_head){
			casio_task=list_entry(ptr,struct casio_task, casio_list_node);
			if(casio_task){
				if(casio_task->task->casio_id == p->casio_id){
					return casio_task;
				}
			}
		}
	}
	return NULL;
}
void rem_casio_task_list(struct casio_rq *rq, struct task_struct *p)
{
	struct list_head *ptr=NULL,*next=NULL;
	struct casio_task *casio_task=NULL;
	char msg[CASIO_MSG_SIZE];
	if(rq && p){
		list_for_each_safe(ptr,next,&rq->casio_list_head){
			casio_task=list_entry(ptr,struct casio_task, casio_list_node);
			if(casio_task){
				if(casio_task->task->casio_id == p->casio_id){
					list_del(ptr);
					snprintf(msg,CASIO_MSG_SIZE,"rem_casio_task_list: %d:%d:%llu",casio_task->task->casio_id,casio_task->task->pid,casio_task->absolute_deadline); 
					register_casio_event(sched_clock(), msg, CASIO_MSG);
					kfree(casio_task);
					return;
				}
			}
		}
	}
}
/*
 * rb_tree functions.
 */

void remove_casio_task_rb_tree(struct casio_rq *rq, struct casio_task *p)
{
	rb_erase(&(p->casio_rb_node),&(rq->casio_rb_root));
	p->casio_rb_node.rb_left=p->casio_rb_node.rb_right=NULL;
}
void insert_casio_task_rb_tree(struct casio_rq *rq, struct casio_task *p)
{
	struct rb_node **node=NULL;
	struct rb_node *parent=NULL;
	struct casio_task *entry=NULL;
	node=&rq->casio_rb_root.rb_node;
	while(*node!=NULL){
		parent=*node;
		entry=rb_entry(parent, struct casio_task,casio_rb_node);
		if(entry){
			if(p->absolute_deadline < entry->absolute_deadline){
				node=&parent->rb_left;
			}else{
				node=&parent->rb_right;
			}
		}
	}
	rb_link_node(&p->casio_rb_node,parent,node);
	rb_insert_color(&p->casio_rb_node,&rq->casio_rb_root);
}
struct casio_task * earliest_deadline_casio_task_rb_tree(struct casio_rq *rq)
{
	struct rb_node *node=NULL;
	struct casio_task *p=NULL;
	node=rq->casio_rb_root.rb_node;
	if(node==NULL)
		return NULL;

	while(node->rb_left!=NULL){
		node=node->rb_left;
	}
	p=rb_entry(node, struct casio_task,casio_rb_node);
	return p;
}

static void check_preempt_curr_casio(struct rq *rq, struct task_struct *p, int flags)
{
	struct casio_task *t=NULL,*curr=NULL;
	if(rq->curr->policy!=SCHED_CASIO){
		resched_task(rq->curr);
	}
	else{
		t=earliest_deadline_casio_task_rb_tree(&rq->casio_rq);
		if(t){
			curr=find_casio_task_list(&rq->casio_rq,rq->curr);
			if(curr){
				if(t->absolute_deadline < curr->absolute_deadline)
					resched_task(rq->curr);
			}
			else{
				printk(KERN_ALERT "check_preempt_curr_casio\n");
			}
		}
	}
}

static struct task_struct *pick_next_task_casio(struct rq *rq)
{
	struct casio_task *t=NULL;
	t=earliest_deadline_casio_task_rb_tree(&rq->casio_rq);
	if(t){
		return t->task;
	}
	return NULL;
}

static void enqueue_task_casio(struct rq *rq, struct task_struct *p, int wakeup, bool head)
{
	struct casio_task *t=NULL;
	char msg[CASIO_MSG_SIZE];
	if(p){
		t=find_casio_task_list(&rq->casio_rq,p);
		if(t){
			t->absolute_deadline=sched_clock()+p->deadline;
			insert_casio_task_rb_tree(&rq->casio_rq, t);
			atomic_inc(&rq->casio_rq.nr_running);
			snprintf(msg,CASIO_MSG_SIZE,"(%d:%d:%llu)",p->casio_id,p->pid,t->absolute_deadline); 
			register_casio_event(sched_clock(), msg, CASIO_ENQUEUE);
		}
		else{
			printk(KERN_ALERT "enqueue_task_casio\n");
		}
	}
}

static void dequeue_task_casio(struct rq *rq, struct task_struct *p, int sleep)
{
	struct casio_task *t=NULL;
	char msg[CASIO_MSG_SIZE];
	if(p){
		t=find_casio_task_list(&rq->casio_rq,p);
		if(t){
			snprintf(msg,CASIO_MSG_SIZE,"(%d:%d:%llu)",t->task->casio_id,t->task->pid,t->absolute_deadline); 
			register_casio_event(sched_clock(), msg, CASIO_DEQUEUE);	
			remove_casio_task_rb_tree(&rq->casio_rq, t);
			atomic_dec(&rq->casio_rq.nr_running);
			if(t->task->state==TASK_DEAD || t->task->state==EXIT_DEAD || t->task->state==EXIT_ZOMBIE){
				rem_casio_task_list(&rq->casio_rq,t->task);
			}
		}
		else{
			printk(KERN_ALERT "dequeue_task_casio\n");
		}
	}

}

static void put_prev_task_casio(struct rq *rq, struct task_struct *prev)
{

}

#ifdef CONFIG_SMP
static unsigned long load_balance_casio(struct rq *this_rq, int this_cpu, struct rq *busiest,
		  unsigned long max_load_move,
		  struct sched_domain *sd, enum cpu_idle_type idle,
		  int *all_pinned, int *this_best_prio)
{
	return 0;
}

static int move_one_task_casio(struct rq *this_rq, int this_cpu, struct rq *busiest,
		   struct sched_domain *sd, enum cpu_idle_type idle)
{
	return 0;
}
#endif

static void task_tick_casio(struct rq *rq, struct task_struct *p, int queued)
{
	//check_preempt_curr_casio(rq, p);
}

static void set_curr_task_casio(struct rq *rq)
{

}


/*
 * When switching a task to RT, we may overload the runqueue
 * with RT tasks. In this case we try to push them off to
 * other runqueues.
 */
static void switched_to_casio(struct rq *rq, struct task_struct *p,
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


unsigned int get_rr_interval_casio(struct rq *rq, struct task_struct *task)
{
	/*
         * Time slice is 0 for SCHED_FIFO tasks
         */
        if (task->policy == SCHED_RR)
                return DEF_TIMESLICE;
        else
                return 0;
}

static void yield_task_casio(struct rq *rq)
{

}


/*
 * Priority of the task has changed. This may cause
 * us to initiate a push or pull.
 */
static void prio_changed_casio(struct rq *rq, struct task_struct *p,
			    int oldprio, int running)
{

}

static int select_task_rq_casio(struct rq *rq, struct task_struct *p, int sd_flag, int flags)
{

//	struct rq *rq = task_rq(p);

	if (sd_flag != SD_BALANCE_WAKE)
		return smp_processor_id();

	return task_cpu(p);
}


static void set_cpus_allowed_casio(struct task_struct *p,
				const struct cpumask *new_mask)
{

}

/* Assumes rq->lock is held */
static void rq_online_casio(struct rq *rq)
{

}

/* Assumes rq->lock is held */
static void rq_offline_casio(struct rq *rq)
{

}

static void pre_schedule_casio(struct rq *rq, struct task_struct *prev)
{

}

static void post_schedule_casio(struct rq *rq)
{

}
/*
 * If we are not running and we are not going to reschedule soon, we should
 * try to push tasks away now
 */
static void task_woken_casio(struct rq *rq, struct task_struct *p)
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
static void switched_from_casio(struct rq *rq, struct task_struct *p,
			   int running)
{

}

/*
 * Simple, special scheduling class for the per-CPU casio tasks:
 */
static const struct sched_class casio_sched_class = {
	.next 			= &rt_sched_class,
	.enqueue_task		= enqueue_task_casio,
	.dequeue_task		= dequeue_task_casio,

	.check_preempt_curr	= check_preempt_curr_casio,

	.pick_next_task		= pick_next_task_casio,
	.put_prev_task		= put_prev_task_casio,

#ifdef CONFIG_SMP
	.load_balance		= load_balance_casio,
	.move_one_task		= move_one_task_casio,

	.select_task_rq		= select_task_rq_casio,
	.set_cpus_allowed       = set_cpus_allowed_casio,
	.rq_online              = rq_online_casio,
	.rq_offline             = rq_offline_casio,
	.pre_schedule		= pre_schedule_casio,
	.post_schedule		= post_schedule_casio,
	.task_woken		= task_woken_casio,
	.switched_from		= switched_from_casio,
#endif

	.set_curr_task          = set_curr_task_casio,
	.task_tick		= task_tick_casio,

	.switched_to		= switched_to_casio,

	.yield_task		= yield_task_casio,
	.get_rr_interval	= get_rr_interval_casio,

	.prio_changed		= prio_changed_casio,
};
