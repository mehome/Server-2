#ifndef MODULE_H
#define MODULE_H

#include "Cycle.h"
#include "Connection.h"
#include "ngx_event_timer.h"
#include "ngx_event_posted.h"
#include "ngx_times.h"

inline int add_connection(connection_t *conn)
{
	int ret =  action_add(conn->cycle->core,&conn->so,NGX_READ_EVENT,0);
	if(ret == 0)
	{
		conn->cycle->connection_count++;
	}
	return ret;
}

inline int del_connection(connection_t *conn)
{
	int ret = action_del(conn->cycle->core,&conn->so);
	if(ret == 0)
	{
		conn->cycle->connection_count--;
	}
	return ret;
}

#define add_event(cycle,ev) ngx_post_event(ev,&cycle->posted)
#define del_event(cycle,ev) ngx_delete_posted_event(ev)

typedef void (*safe_event_handle_pt)(cycle_t * cycle,event_t *ev);

typedef struct safe_event_s{
	event_t self;
	cycle_t *cycle;
	event_t *event;
	safe_event_handle_pt handler;
}safe_event_t;

static inline void safe_event_handler(event_t *ev)
{
	safe_event_t * sev = (safe_event_t*)ev->data;
	sev->handler(sev->cycle,sev->event);
	FREE(sev);
}

inline void safe_add_event(cycle_t *cycle,event_t * ev,safe_event_handle_pt handler)
{
	safe_event_t * sev = (safe_event_t*)MALLOC(sizeof(safe_event_t));
	sev->cycle = cycle;
	sev->event = ev;
	sev->handler = handler;
	initEvent(&sev->self,safe_event_handler,sev);
	ngx_spinlock(&cycle->accept_posted_lock,1,0);
	ngx_post_event(&sev->self,&cycle->accept_posted);
	cycle->accept_posted_index += 1;
	ngx_unlock(&cycle->accept_posted_lock);
}

inline void safe_process_event(cycle_t *cycle)
{
	if(cycle->accept_posted_index > 0)
	{
		ngx_spinlock(&cycle->accept_posted_lock,1,0);
		// if(ngx_trylock(&cycle->accept_lock))
		{
			ngx_queue_add(&cycle->posted,&cycle->accept_posted);
			ngx_queue_init(&cycle->accept_posted);

			cycle->accept_posted_index = 0;
			ngx_unlock(&cycle->accept_posted_lock);
		}
	}
}


#define add_timer(cycle,ev,timer) ngx_event_add_timer(&cycle->timeout,ev,timer)
#define del_timer(cycle,ev) ngx_event_del_timer(&cycle->timeout,ev)

#define timer_is_empty(cycle) (cycle->timeout.root == cycle->timeout.sentinel)
#define event_is_empty(cycle) ngx_queue_empty(&cycle->posted)

inline int cicle_process(cycle_t * cycle)
{
	LOGD("cicle_process begin.\n");
	while(1){
		ngx_time_update();
		ngx_msec_t timeout = ngx_event_find_timer(&cycle->timeout);
		if(timeout == NGX_TIMER_INFINITE)
		{
			timeout = 10;
		}
		int ret = action_process(cycle->core,timeout);
		if(ret == -1)
		{
			break;
		}else if(ret > 0)
		{
			// LOGD("action_process :%d\n",ret);
		}
		ngx_time_update();
		ngx_event_expire_timers(&cycle->timeout);
		safe_process_event(cycle);
		ngx_event_process_posted(&cycle->posted);

		if(cycle->connection_count == 0 && event_is_empty(cycle) && timer_is_empty(cycle))
		{
			break;
		}
	}
	LOGD("cicle_process end.\n");
	return 0;
}

inline int cicle_process_loop(cycle_t * cycle)
{
	LOGD("cicle_process_loop begin.\n");
	while(1){
		ngx_msec_t timeout = ngx_event_find_timer(&cycle->timeout);
		if(timeout == NGX_TIMER_INFINITE)
		{
			timeout = 10;
		}
		int ret = action_process(cycle->core,timeout);
		if(ret == -1)
		{
			break;
		}else if(ret > 0)
		{
			// LOGD("action_process :%d\n",ret);
		}
		ngx_event_expire_timers(&cycle->timeout);
		safe_process_event(cycle);
		ngx_event_process_posted(&cycle->posted);
	}
	LOGD("cicle_process_loop end.\n");
	return 0;
}

#endif