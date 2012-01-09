/*
 * This file is part of the zlog Library.
 *
 * Copyright (C) 2011 by Hardy Simpson <HardySimpson1984@gmail.com>
 *
 * The zlog Library is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * The zlog Library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with the zlog Library. If not, see <http://www.gnu.org/licenses/>.
 */

#include <pthread.h>
#include <errno.h>

#include "zc_defs.h"
#include "event.h"
#include "buf.h"
#include "thread.h"
#include "mdc.h"

/*******************************************************************************/
static void zlog_thread_del(zlog_thread_t * a_thread)
{
	if (a_thread->mdc)
		zlog_mdc_del(a_thread->mdc);
	if (a_thread->event)
		zlog_event_del(a_thread->event);
	if (a_thread->pre_path_buf)
		zlog_buf_del(a_thread->pre_path_buf);
	if (a_thread->path_buf)
		zlog_buf_del(a_thread->path_buf);
	if (a_thread->pre_msg_buf)
		zlog_buf_del(a_thread->pre_msg_buf);
	if (a_thread->msg_buf)
		zlog_buf_del(a_thread->msg_buf);

	zc_debug("free a thread at[%p]", a_thread);
	free(a_thread);
	return;
}

static zlog_thread_t * zlog_thread_new(size_t buf_size_min, size_t buf_size_max)
{
	int rc = 0;
	zlog_thread_t *a_thread;

	a_thread = calloc(1, sizeof(zlog_thread_t));
	if (!a_thread) {
		zc_error("calloc fail, errno[%d]", errno);
		return NULL;
	}

	a_thread->mdc = zlog_mdc_new();
	if (!a_thread->mdc) {
		zc_error("zlog_mdc_new fail");
		rc = -1;
		goto zlog_thread_init_exit;
	}

	a_thread->event = zlog_event_new();
	if (!a_thread->event) {
		zc_error("zlog_event_new fail");
		rc = -1;
		goto zlog_thread_init_exit;
	}

	a_thread->pre_path_buf = zlog_buf_new(MAXLEN_PATH + 1, MAXLEN_PATH + 1, NULL);
	if (!a_thread->pre_path_buf) {
		zc_error("zlog_buf_new fail");
		rc = -1;
		goto zlog_thread_init_exit;
	}

	a_thread->path_buf = zlog_buf_new(MAXLEN_PATH + 1, MAXLEN_PATH + 1, NULL);
	if (!a_thread->path_buf) {
		zc_error("zlog_buf_new fail");
		rc = -1;
		goto zlog_thread_init_exit;
	}

	a_thread->pre_msg_buf = zlog_buf_new(buf_size_min, buf_size_max, "..."FILE_NEWLINE);
	if (!a_thread->pre_msg_buf) {
		zc_error("zlog_buf_new fail");
		rc = -1;
		goto zlog_thread_init_exit;
	}

	a_thread->msg_buf = zlog_buf_new(buf_size_min, buf_size_max, "..."FILE_NEWLINE);
	if (!a_thread->msg_buf) {
		zc_error("zlog_buf_new fail");
		rc = -1;
		goto zlog_thread_init_exit;
	}

    zlog_thread_init_exit:

	if (rc) {
		zlog_thread_del(a_thread);
		return NULL;
	} else {
		zc_debug("init a thread at[%p]", a_thread);
		return a_thread;
	}
}

static int zlog_thread_update(zlog_thread_t * a_thread, size_t buf_size_min, size_t buf_size_max)
{
	int rc = 0;

	if (a_thread->pre_msg_buf)
		zlog_buf_del(a_thread->pre_msg_buf);
	if (a_thread->msg_buf)
		zlog_buf_del(a_thread->msg_buf);

	a_thread->pre_msg_buf = zlog_buf_new(buf_size_min, buf_size_max, "..."FILE_NEWLINE);
	if (!a_thread->pre_msg_buf) {
		zc_error("zlog_buf_new fail");
		rc = -1;
		goto zlog_thread_update_exit;
	}

	a_thread->msg_buf = zlog_buf_new(buf_size_min, buf_size_max, "..."FILE_NEWLINE);
	if (!a_thread->msg_buf) {
		zc_error("zlog_buf_new fail");
		rc = -1;
		goto zlog_thread_update_exit;
	}

    zlog_thread_update_exit:
	if (rc) {
		zlog_thread_del(a_thread);
		return -1;
	} else {
		zc_debug("update a thread at[%p]", a_thread);
		return 0;
	}
}

/*******************************************************************************/

int zlog_tmap_init(zlog_tmap_t * a_tmap)
{
	zc_hashtable_t *a_tab;

	zc_assert(a_tmap, -1);
	a_tab = zc_hashtable_new(20,
				   (zc_hashtable_hash_fn) zc_hashtable_tid_hash,
				   (zc_hashtable_equal_fn) zc_hashtable_tid_equal,
				   NULL, (zc_hashtable_del_fn) zlog_thread_del);
	if (!a_tab) {
		zc_error("init hashtable fail");
		return -1;
	} else {
		a_tmap->tab = a_tab;
		return 0;
	}
}

int zlog_tmap_update(zlog_tmap_t *a_tmap, size_t buf_size_min, size_t buf_size_max)
{
	int rc = 0;
	zc_hashtable_entry_t *a_entry;
	zlog_thread_t *a_thread;

	zc_assert(a_tmap, -1);
	zc_hashtable_foreach(a_tmap->tab, a_entry) {
		a_thread = (zlog_thread_t *)a_entry->value;
		rc = zlog_thread_update(a_thread, buf_size_min, buf_size_max);
		if (rc) {
			zc_error("zlog_thread_update fail");
			return -1;
		}
	}

	return 0;
}

void zlog_tmap_fini(zlog_tmap_t * a_tmap)
{
	zc_assert(a_tmap, );

	if (a_tmap->tab)
		zc_hashtable_del(a_tmap->tab);
	memset(a_tmap, 0x00, sizeof(zlog_tmap_t));
	return;
}

/*******************************************************************************/

zlog_thread_t * zlog_tmap_get_thread(zlog_tmap_t * a_tmap)
{
	pthread_t tid;
	zlog_thread_t *a_thread;

	zc_assert(a_tmap, NULL);

	tid = pthread_self();
	a_thread = zc_hashtable_get(a_tmap->tab, (void *)&tid);
	if (!a_thread) {
		zc_debug("thread[%ld] not found, maybe not create", tid);
		return NULL;
	} else {
		return a_thread;
	}
} 

zlog_thread_t * zlog_tmap_new_thread(zlog_tmap_t * a_tmap, size_t buf_size_min, size_t buf_size_max)
{
	int rc = 0;
	zlog_thread_t *a_thread;

	zc_assert(a_tmap, NULL);

	a_thread = zlog_thread_new(buf_size_min, buf_size_max);
	if (!a_thread) {
		zc_error("zlog_thread_new fail");
		return NULL;
	}

	rc = zc_hashtable_put(a_tmap->tab, (void *)&(a_thread->event->tid), (void *)a_thread);
	if (rc) {
		zc_error("zc_hashtable_put fail");
		goto zlog_threads_create_thread_exit;
	}

      zlog_threads_create_thread_exit:
	if (rc) {
		zlog_thread_del(a_thread);
		return NULL;
	} else {
		return a_thread;
	}
}

void zlog_tmap_profile(zlog_tmap_t * a_tmap)
{
	zc_hashtable_entry_t *a_entry;
	zlog_thread_t *a_thread;

	zc_assert(a_tmap, );
	zc_error("---tmap[%p]---", a_tmap);
	zc_hashtable_foreach(a_tmap->tab, a_entry) {
		a_thread = (zlog_thread_t *)a_entry->value;
		zc_error("thread:[%ld]", a_thread->event->tid);
	}
	return;
}

/*******************************************************************************/