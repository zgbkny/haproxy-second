/*
 * Protocol registration and listener management functions.
 *
 * Copyright 2000-2010 Willy Tarreau <w@1wt.eu>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <common/config.h>
#include <common/errors.h>
#include <common/mini-clist.h>
#include <common/standard.h>
#include <common/time.h>

#include <types/global.h>

#include <proto/acl.h>
#include <proto/fd.h>
#include <proto/freq_ctr.h>
#include <proto/log.h>
#include <proto/task.h>

/* List head of all registered protocols */
static struct list protocols = LIST_HEAD_INIT(protocols);

/* This function adds the specified listener's file descriptor to the polling
 * lists if it is in the LI_LISTEN state. The listener enters LI_READY or
 * LI_FULL state depending on its number of connections.
 */
void enable_listener(struct listener *listener)
{
	if (listener->state == LI_LISTEN) {
		if (listener->nbconn < listener->maxconn) {
			EV_FD_SET(listener->fd, DIR_RD);
			listener->state = LI_READY;
		} else {
			listener->state = LI_FULL;
		}
	}
}

/* This function removes the specified listener's file descriptor from the
 * polling lists if it is in the LI_READY or in the LI_FULL state. The listener
 * enters LI_LISTEN.
 */
void disable_listener(struct listener *listener)
{
	if (listener->state < LI_READY)
		return;
	if (listener->state == LI_READY)
		EV_FD_CLR(listener->fd, DIR_RD);
	if (listener->state == LI_LIMITED)
		LIST_DEL(&listener->wait_queue);
	listener->state = LI_LISTEN;
}

/* This function tries to temporarily disable a listener, depending on the OS
 * capabilities. Linux unbinds the listen socket after a SHUT_RD, and ignores
 * SHUT_WR. Solaris refuses either shutdown(). OpenBSD ignores SHUT_RD but
 * closes upon SHUT_WR and refuses to rebind. So a common validation path
 * involves SHUT_WR && listen && SHUT_RD. In case of success, the FD's polling
 * is disabled. It normally returns non-zero, unless an error is reported.
 */
int pause_listener(struct listener *l)
{
	if (l->state <= LI_PAUSED)
		return 1;

	if (shutdown(l->fd, SHUT_WR) != 0)
		return 0; /* Solaris dies here */

	if (listen(l->fd, l->backlog ? l->backlog : l->maxconn) != 0)
		return 0; /* OpenBSD dies here */

	if (shutdown(l->fd, SHUT_RD) != 0)
		return 0; /* should always be OK */

	if (l->state == LI_LIMITED)
		LIST_DEL(&l->wait_queue);

	EV_FD_CLR(l->fd, DIR_RD);
	l->state = LI_PAUSED;
	return 1;
}

/* This function tries to resume a temporarily disabled listener. Paused, full,
 * limited and disabled listeners are handled, which means that this function
 * may replace enable_listener(). The resulting state will either be LI_READY
 * or LI_FULL. 0 is returned in case of failure to resume (eg: dead socket).
 */
int resume_listener(struct listener *l)
{
	if (l->state < LI_PAUSED)
		return 0;

	if (l->state == LI_PAUSED &&
	    listen(l->fd, l->backlog ? l->backlog : l->maxconn) != 0)
		return 0;

	if (l->state == LI_READY)
		return 1;

	if (l->state == LI_LIMITED)
		LIST_DEL(&l->wait_queue);

	if (l->nbconn >= l->maxconn) {
		l->state = LI_FULL;
		return 1;
	}

	EV_FD_SET(l->fd, DIR_RD);
	l->state = LI_READY;
	return 1;
}

/* Marks a ready listener as full so that the session code tries to re-enable
 * it upon next close() using resume_listener().
 */
void listener_full(struct listener *l)
{
	if (l->state >= LI_READY) {
		if (l->state == LI_LIMITED)
			LIST_DEL(&l->wait_queue);

		EV_FD_CLR(l->fd, DIR_RD);
		l->state = LI_FULL;
	}
}

/* Marks a ready listener as limited so that we only try to re-enable it when
 * resources are free again. It will be queued into the specified queue.
 */
void limit_listener(struct listener *l, struct list *list)
{
	if (l->state == LI_READY) {
		LIST_ADDQ(list, &l->wait_queue);
		EV_FD_CLR(l->fd, DIR_RD);
		l->state = LI_LIMITED;
	}
}

/* This function adds all of the protocol's listener's file descriptors to the
 * polling lists when they are in the LI_LISTEN state. It is intended to be
 * used as a protocol's generic enable_all() primitive, for use after the
 * fork(). It puts the listeners into LI_READY or LI_FULL states depending on
 * their number of connections. It always returns ERR_NONE.
 */
int enable_all_listeners(struct protocol *proto)
{
	struct listener *listener;

	list_for_each_entry(listener, &proto->listeners, proto_list)
		enable_listener(listener);
	return ERR_NONE;
}

/* This function removes all of the protocol's listener's file descriptors from
 * the polling lists when they are in the LI_READY or LI_FULL states. It is
 * intended to be used as a protocol's generic disable_all() primitive. It puts
 * the listeners into LI_LISTEN, and always returns ERR_NONE.
 */
int disable_all_listeners(struct protocol *proto)
{
	struct listener *listener;

	list_for_each_entry(listener, &proto->listeners, proto_list)
		disable_listener(listener);
	return ERR_NONE;
}

/* Dequeues all of the listeners waiting for a resource in wait queue <queue>. */
void dequeue_all_listeners(struct list *list)
{
	struct listener *listener, *l_back;

	list_for_each_entry_safe(listener, l_back, list, wait_queue) {
		/* This cannot fail because the listeners are by definition in
		 * the LI_LIMITED state. The function also removes the entry
		 * from the queue.
		 */
		resume_listener(listener);
	}
}

/* This function closes the listening socket for the specified listener,
 * provided that it's already in a listening state. The listener enters the
 * LI_ASSIGNED state. It always returns ERR_NONE. This function is intended
 * to be used as a generic function for standard protocols.
 */
int unbind_listener(struct listener *listener)
{
	if (listener->state == LI_READY)
		EV_FD_CLR(listener->fd, DIR_RD);

	if (listener->state == LI_LIMITED)
		LIST_DEL(&listener->wait_queue);

	if (listener->state >= LI_PAUSED) {
		fd_delete(listener->fd);
		listener->state = LI_ASSIGNED;
	}
	return ERR_NONE;
}

/* This function closes all listening sockets bound to the protocol <proto>,
 * and the listeners end in LI_ASSIGNED state if they were higher. It does not
 * detach them from the protocol. It always returns ERR_NONE.
 */
int unbind_all_listeners(struct protocol *proto)
{
	struct listener *listener;

	list_for_each_entry(listener, &proto->listeners, proto_list)
		unbind_listener(listener);
	return ERR_NONE;
}

/* Delete a listener from its protocol's list of listeners. The listener's
 * state is automatically updated from LI_ASSIGNED to LI_INIT. The protocol's
 * number of listeners is updated. Note that the listener must have previously
 * been unbound. This is the generic function to use to remove a listener.
 */
void delete_listener(struct listener *listener)
{
	if (listener->state != LI_ASSIGNED)
		return;
	listener->state = LI_INIT;
	LIST_DEL(&listener->proto_list);
	listener->proto->nb_listeners--;
}

/* This function is called on a read event from a listening socket, corresponding
 * to an accept. It tries to accept as many connections as possible, and for each
 * calls the listener's accept handler (generally the frontend's accept handler).
 */
int listener_accept(int fd)
{
	struct listener *l = fdtab[fd].owner;
	struct proxy *p = l->frontend;
	int max_accept = global.tune.maxaccept;
	int cfd;
	int ret;

	if (unlikely(l->nbconn >= l->maxconn)) {
		listener_full(l);
		return 0;
	}

	if (global.cps_lim && !(l->options & LI_O_UNLIMITED)) {
		int max = freq_ctr_remain(&global.conn_per_sec, global.cps_lim, 0);

		if (unlikely(!max)) {
			/* frontend accept rate limit was reached */
			limit_listener(l, &global_listener_queue);
			task_schedule(global_listener_queue_task, tick_add(now_ms, next_event_delay(&global.conn_per_sec, global.cps_lim, 0)));
			return 0;
		}

		if (max_accept > max)
			max_accept = max;
	}

	if (p && p->fe_sps_lim) {
		int max = freq_ctr_remain(&p->fe_sess_per_sec, p->fe_sps_lim, 0);

		if (unlikely(!max)) {
			/* frontend accept rate limit was reached */
			limit_listener(l, &p->listener_queue);
			task_schedule(p->task, tick_add(now_ms, next_event_delay(&p->fe_sess_per_sec, p->fe_sps_lim, 0)));
			return 0;
		}

		if (max_accept > max)
			max_accept = max;
	}

	/* Note: if we fail to allocate a connection because of configured
	 * limits, we'll schedule a new attempt worst 1 second later in the
	 * worst case. If we fail due to system limits or temporary resource
	 * shortage, we try again 100ms later in the worst case.
	 */
	while (max_accept--) {
		struct sockaddr_storage addr;
		socklen_t laddr = sizeof(addr);

		if (unlikely(actconn >= global.maxconn) && !(l->options & LI_O_UNLIMITED)) {
			limit_listener(l, &global_listener_queue);
			task_schedule(global_listener_queue_task, tick_add(now_ms, 1000)); /* try again in 1 second */
			return 0;
		}

		if (unlikely(p && p->feconn >= p->maxconn)) {
			limit_listener(l, &p->listener_queue);
			return 0;
		}

		cfd = accept(fd, (struct sockaddr *)&addr, &laddr);
		if (unlikely(cfd == -1)) {
			switch (errno) {
			case EAGAIN:
			case EINTR:
			case ECONNABORTED:
				return 0;	    /* nothing more to accept */
			case ENFILE:
				if (p)
					send_log(p, LOG_EMERG,
						 "Proxy %s reached system FD limit at %d. Please check system tunables.\n",
						 p->id, maxfd);
				limit_listener(l, &global_listener_queue);
				task_schedule(global_listener_queue_task, tick_add(now_ms, 100)); /* try again in 100 ms */
				return 0;
			case EMFILE:
				if (p)
					send_log(p, LOG_EMERG,
						 "Proxy %s reached process FD limit at %d. Please check 'ulimit-n' and restart.\n",
						 p->id, maxfd);
				limit_listener(l, &global_listener_queue);
				task_schedule(global_listener_queue_task, tick_add(now_ms, 100)); /* try again in 100 ms */
				return 0;
			case ENOBUFS:
			case ENOMEM:
				if (p)
					send_log(p, LOG_EMERG,
						 "Proxy %s reached system memory limit at %d sockets. Please check system tunables.\n",
						 p->id, maxfd);
				limit_listener(l, &global_listener_queue);
				task_schedule(global_listener_queue_task, tick_add(now_ms, 100)); /* try again in 100 ms */
				return 0;
			default:
				return 0;
			}
		}

		/* if this connection comes from a known monitoring system, we want to ignore
		 * it as soon as possible, which means closing it immediately if it is only a
		 * TCP-based monitoring check.
		 */
		if (unlikely((l->options & LI_O_CHK_MONNET) &&
			     (p->mode == PR_MODE_TCP) &&
			     addr.ss_family == AF_INET &&
			     (((struct sockaddr_in *)&addr)->sin_addr.s_addr & p->mon_mask.s_addr) == p->mon_net.s_addr)) {
			close(cfd);
			continue;
		}

		if (unlikely(cfd >= global.maxsock)) {
			send_log(p, LOG_EMERG,
				 "Proxy %s reached the configured maximum connection limit. Please check the global 'maxconn' value.\n",
				 p->id);
			close(cfd);
			limit_listener(l, &global_listener_queue);
			task_schedule(global_listener_queue_task, tick_add(now_ms, 1000)); /* try again in 1 second */
			return 0;
		}

		/* increase the per-process number of cumulated connections */
		if (!(l->options & LI_O_UNLIMITED)) {
			update_freq_ctr(&global.conn_per_sec, 1);
			if (global.conn_per_sec.curr_ctr > global.cps_max)
				global.cps_max = global.conn_per_sec.curr_ctr;
			actconn++;
		}

		jobs++;
		totalconn++;
		l->nbconn++;

		if (l->counters) {
			if (l->nbconn > l->counters->conn_max)
				l->counters->conn_max = l->nbconn;
		}

		ret = l->accept(l, cfd, &addr);
		if (unlikely(ret <= 0)) {
			/* The connection was closed by session_accept(). Either
			 * we just have to ignore it (ret == 0) or it's a critical
			 * error due to a resource shortage, and we must stop the
			 * listener (ret < 0).
			 */
			if (!(l->options & LI_O_UNLIMITED))
				actconn--;
			jobs--;
			l->nbconn--;
			if (ret == 0) /* successful termination */
				continue;

			limit_listener(l, &global_listener_queue);
			task_schedule(global_listener_queue_task, tick_add(now_ms, 100)); /* try again in 100 ms */
			return 0;
		}

		if (l->nbconn >= l->maxconn) {
			listener_full(l);
			return 0;
		}

	} /* end of while (p->feconn < p->maxconn) */

	return 0;
}

/* Registers the protocol <proto> */
void protocol_register(struct protocol *proto)
{
	LIST_ADDQ(&protocols, &proto->list);
}

/* Unregisters the protocol <proto>. Note that all listeners must have
 * previously been unbound.
 */
void protocol_unregister(struct protocol *proto)
{
	LIST_DEL(&proto->list);
	LIST_INIT(&proto->list);
}

/* binds all listeners of all registered protocols. Returns a composition
 * of ERR_NONE, ERR_RETRYABLE, ERR_FATAL.
 */
int protocol_bind_all(char *errmsg, int errlen)
{
	struct protocol *proto;
	int err;

	err = 0;
	list_for_each_entry(proto, &protocols, list) {
		if (proto->bind_all) {
			err |= proto->bind_all(proto, errmsg, errlen);
			if ( err & ERR_ABORT )
				break;
		}
	}
	return err;
}

/* unbinds all listeners of all registered protocols. They are also closed.
 * This must be performed before calling exit() in order to get a chance to
 * remove file-system based sockets and pipes.
 * Returns a composition of ERR_NONE, ERR_RETRYABLE, ERR_FATAL, ERR_ABORT.
 */
int protocol_unbind_all(void)
{
	struct protocol *proto;
	int err;

	err = 0;
	list_for_each_entry(proto, &protocols, list) {
		if (proto->unbind_all) {
			err |= proto->unbind_all(proto);
		}
	}
	return err;
}

/* enables all listeners of all registered protocols. This is intended to be
 * used after a fork() to enable reading on all file descriptors. Returns a
 * composition of ERR_NONE, ERR_RETRYABLE, ERR_FATAL.
 */
int protocol_enable_all(void)
{
	struct protocol *proto;
	int err;

	err = 0;
	list_for_each_entry(proto, &protocols, list) {
		if (proto->enable_all) {
			err |= proto->enable_all(proto);
		}
	}
	return err;
}

/* disables all listeners of all registered protocols. This may be used before
 * a fork() to avoid duplicating poll lists. Returns a composition of ERR_NONE,
 * ERR_RETRYABLE, ERR_FATAL.
 */
int protocol_disable_all(void)
{
	struct protocol *proto;
	int err;

	err = 0;
	list_for_each_entry(proto, &protocols, list) {
		if (proto->disable_all) {
			err |= proto->disable_all(proto);
		}
	}
	return err;
}

/* Returns the protocol handler for socket family <family> or NULL if not found */
struct protocol *protocol_by_family(int family)
{
	struct protocol *proto;

	list_for_each_entry(proto, &protocols, list) {
		if (proto->sock_domain == family)
			return proto;
	}
	return NULL;
}

/************************************************************************/
/*           All supported ACL keywords must be declared here.          */
/************************************************************************/

/* set temp integer to the number of connexions to the same listening socket */
static int
acl_fetch_dconn(struct proxy *px, struct session *l4, void *l7, unsigned int opt,
                const struct arg *args, struct sample *smp)
{
	smp->type = SMP_T_UINT;
	smp->data.uint = l4->listener->nbconn;
	return 1;
}

/* set temp integer to the id of the socket (listener) */
static int
acl_fetch_so_id(struct proxy *px, struct session *l4, void *l7, unsigned int opt,
                const struct arg *args, struct sample *smp)
{
	smp->type = SMP_T_UINT;
	smp->data.uint = l4->listener->luid;
	return 1;
}

/* Note: must not be declared <const> as its list will be overwritten.
 * Please take care of keeping this list alphabetically sorted.
 */
static struct acl_kw_list acl_kws = {{ },{
	{ "dst_conn",   acl_parse_int,   acl_fetch_dconn,    acl_match_int, ACL_USE_NOTHING, 0 },
	{ "so_id",      acl_parse_int,   acl_fetch_so_id,    acl_match_int, ACL_USE_NOTHING, 0 },
	{ NULL, NULL, NULL, NULL },
}};

__attribute__((constructor))
static void __protocols_init(void)
{
	acl_register_keywords(&acl_kws);
}

/*
 * Local variables:
 *  c-indent-level: 8
 *  c-basic-offset: 8
 * End:
 */
