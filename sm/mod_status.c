/*
 * jabberd mod_status - Jabber Open Source Server
 * Copyright (c) 2004 Lucas Nussbaum <lucas@lucas-nussbaum.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA02111-1307USA
 */

/** @file sm/mod_status.c
  * @brief status info management
  * @author Lucas Nussbaum 
  * $Date: 2004/09/01 $
  * $Revision: 1.3 $
  */

/* for strndup */
#define _GNU_SOURCE
#include <string.h>
#include "sm.h"

typedef struct _status_st {
    sm_t    sm;
    char    *resource;
} *status_t;

static void _status_os_replace(storage_t st, const unsigned char *jid, char *status, char *show, time_t *lastlogin, time_t *lastlogout) {
    os_t os = os_new();
    os_object_t o = os_object_new(os);
    os_object_put(o, "status", status, os_type_STRING);
    os_object_put(o, "show", show, os_type_STRING);
    os_object_put(o, "last-login", (void **) lastlogin, os_type_INTEGER);
    os_object_put(o, "last-logout", (void **) lastlogout, os_type_INTEGER);
    storage_replace(st, "status", jid, NULL, os);
    os_free(os);
}

static int _status_sess_start(mod_instance_t mi, sess_t sess) {
    module_t mod = mi->mod;
    status_t st = (status_t) mod->private;
    time_t t, lastlogout;
    os_t os;
    os_object_t o;
    st_ret_t ret;

    ret = storage_get(sess->user->sm->st, "status", jid_user(sess->jid), NULL, &os);
    if (ret == st_SUCCESS)
    {
        if (os_iter_first(os))
        {
            o = os_iter_object(os);
            os_object_get_time(os, o, "last-logout", &lastlogout);
        }
        os_free(os);
    }
    else
    {
        lastlogout = (time_t) 0;
    }
    
    t = time(NULL);
    _status_os_replace(sess->user->sm->st, jid_user(sess->jid), "online", "", &t, &lastlogout);

    return mod_PASS;
}

static void _status_sess_end(mod_instance_t mi, sess_t sess) {
    module_t mod = mi->mod;
    status_t st = (status_t) mod->private;
    time_t t, lastlogin;
    os_t os;
    os_object_t o;
    st_ret_t ret;

    ret = storage_get(sess->user->sm->st, "status", jid_user(sess->jid), NULL, &os);
    if (ret == st_SUCCESS)
    {
        if (os_iter_first(os))
        {
            o = os_iter_object(os);
            os_object_get_time(os, o, "last-login", &lastlogin);
        }
        os_free(os);
    }
    else
    {
        lastlogin = (time_t) 0;
    }

    t = time(NULL);
    _status_os_replace(sess->user->sm->st, jid_user(sess->jid), "offline", "", &lastlogin, &t);
}

static void _status_user_delete(mod_instance_t mi, jid_t jid) {
    module_t mod = mi->mod;
    status_t st = (status_t) mod->private;

    storage_delete(mi->sm->st, "status", jid_user(jid), NULL);
}

static void _status_store(storage_t st, const unsigned char *jid, pkt_t pkt, time_t *lastlogin, time_t *lastlogout) {
    char *show;
    int show_free = 0;

    switch(pkt->type) 
    {
        int elem;
        case pkt_PRESENCE_UN:
            show = "unavailable";
            break;
        default:
            elem = nad_find_elem(pkt->nad, 1, NAD_ENS(pkt->nad, 1), "show", 1);
            if (elem < 0)
            {
                show = "";
            }
            else
            {    
                if (NAD_CDATA_L(pkt->nad, elem) <= 0 || NAD_CDATA_L(pkt->nad, elem) > 19)
                    show = "";
                else
                {
                    show = strndup(NAD_CDATA(pkt->nad, elem), NAD_CDATA_L(pkt->nad, elem));
                    show_free = 1;
                }
            }
    }

    _status_os_replace(st, jid, "online", show, lastlogin, lastlogout);
    if(show_free) free(show);
}

static mod_ret_t _status_in_sess(mod_instance_t mi, sess_t sess, pkt_t pkt) {
    module_t mod = mi->mod;
    status_t st = (status_t) mod->private;
    time_t lastlogin, lastlogout;
    os_t os;
    os_object_t o;
    st_ret_t ret;

    /* only handle presence */
    if(!(pkt->type & pkt_PRESENCE))
        return mod_PASS;

    ret = storage_get(sess->user->sm->st, "status", jid_user(sess->jid), NULL, &os);
    if (ret == st_SUCCESS)
    {
        if (os_iter_first(os))
        {
            o = os_iter_object(os);
            os_object_get_time(os, o, "last-login", &lastlogin);
            os_object_get_time(os, o, "last-logout", &lastlogout);
        }
        os_free(os);
    }
    else
    {
        lastlogin = (time_t) 0;
        lastlogout = (time_t) 0;
    }

    /* If the presence is for a specific user, ignore it. */
    if (pkt->to == NULL)
        _status_store(sess->user->sm->st, jid_user(sess->jid), pkt, &lastlogin, &lastlogout);

    return mod_PASS;
}

/* presence packets incoming from other servers */
static mod_ret_t _status_pkt_sm(mod_instance_t mi, pkt_t pkt) {
    time_t t;
    module_t mod = mi->mod;
    status_t st = (status_t) mod->private;

    log_debug(ZONE, "\n\n\n=======\npkt from %s, type 0x%X, to: %s, res: %s\n\n\n========", jid_full(pkt->from), pkt->type, jid_full(pkt->to), st->resource);
    /* only check presence/subs to configured resource */
    if(!(pkt->type & pkt_PRESENCE || pkt->type & pkt_S10N) || st->resource == NULL || strcmp(pkt->to->resource, st->resource) != 0)
        return mod_PASS;

    /* handle subscription requests */
    if(pkt->type == pkt_S10N) {
        log_debug(ZONE, "subscription request from %s", jid_full(pkt->from));

        /* accept request */
        pkt_router(pkt_create(st->sm, "presence", "subscribed", jid_user(pkt->from), jid_full(pkt->to)));

        /* send presence */
        pkt_router(pkt_create(st->sm, "presence", NULL, jid_user(pkt->from), jid_full(pkt->to)));

        /* and subscribe back to theirs */
        pkt_router(pkt_tofrom(pkt));

        return mod_HANDLED;
    }

    /* handle unsubscribe requests */
    if(pkt->type == pkt_S10N_UN) {
        log_debug(ZONE, "unsubscribe request from %s", jid_full(pkt->from));

        /* ack the request */
        nad_set_attr(pkt->nad, 1, -1, "type", "unsubscribed", 12);
        pkt_router(pkt_tofrom(pkt));

        return mod_HANDLED;
    }

    /* answer to probes */
    if(pkt->type == pkt_PRESENCE_PROBE) {
        log_debug(ZONE, "presence probe from %s", jid_full(pkt->from));

        /* send presence */
        pkt_router(pkt_create(st->sm, "presence", NULL, jid_user(pkt->from), jid_full(pkt->to)));

        pkt_free(pkt);
        return mod_HANDLED;
    }

    /* store presence information */
    if(pkt->type == pkt_PRESENCE || pkt->type == pkt_PRESENCE_UN) {
        log_debug(ZONE, "storing presence from %s", jid_full(pkt->from));

        t = (time_t) 0;
        
        _status_store(mod->mm->sm->st, jid_user(pkt->from), pkt, &t, &t);

        pkt_free(pkt);
        return mod_HANDLED;
    }

    log_debug(ZONE, "dropping presence from %s", jid_full(pkt->from));

    /* drop the rest */
    pkt_free(pkt);
    return mod_HANDLED;

}

DLLEXPORT int module_init(mod_instance_t mi, char *arg) {
    module_t mod = mi->mod;

    status_t tr;

    if (mod->init) return 0;

    tr = (status_t) malloc(sizeof(struct _status_st));
    memset(tr, 0, sizeof(struct _status_st));

    tr->sm = mod->mm->sm;
    tr->resource = config_get_one(mod->mm->sm->config, "status.resource", 0);

    mod->private = tr;

    mod->user_delete = _status_user_delete;
    mod->sess_start = _status_sess_start;
    mod->sess_end = _status_sess_end;
    mod->in_sess = _status_in_sess;
    mod->pkt_sm = _status_pkt_sm;

    return 0;
}
