/*
  Proc fs for Lottery Scheduling

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#ifndef _LINUX_PROC_LOTTERY_H
#define _LINUX_PROC_LOTTERY_H

#ifdef	CONFIG_SCHED_LOTTERY_POLICY

#define LOTTERY_MSG_SIZE		400

#define LOTTERY_MAX_EVENT_LINES		10000

enum lottery_action {
	LOTTERY_ENQUEUE	= 0,
	LOTTERY_DEQUEUE,
	LOTTERY_CONTEXT_SWITCH,
	LOTTERY_PICK_TIME,
	LOTTERY_PREEMPT,
	LOTTERY_TICK,
	LOTTERY_MSG
};

struct lottery_stats{
	unsigned long long lottery_iteration;
	unsigned long long lottery_latency;
	unsigned long long lottery_enqueue;
	unsigned long long lottery_dequeue;
	unsigned long long lottery_yield;
	unsigned long long lottery_prempt;
};
struct lottery_event{
	enum lottery_action action;
	unsigned long long timestamp;
	char msg[LOTTERY_MSG_SIZE];
};

struct lottery_event_log{
	struct lottery_event lottery_event[LOTTERY_MAX_EVENT_LINES];
	unsigned int tail;
	unsigned int head;
	unsigned int size;
	unsigned int cursor;
	unsigned int flag;
};

/**
 * @brief Initializes the lottery log data structures
 */
void init_lottery_event_log(void);

/**
 * @brief Get the buffer for lottery scheduling logs
 *
 * @return pointer to the buffer for lottery scheduling logs
 */
struct lottery_event_log * get_lottery_event_log(void);

/**
 * @brief Resets the statistics related data structure
 */
void lottery_reset_stats(void);

/**
 * @brief Gets the pointer to structure for statistics
 *
 * @return Returns the pointer to structure for statistics
 */
struct lottery_stats *lottery_get_stats(void);


#endif
#endif
