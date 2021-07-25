/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2021.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#include "ucg_listener.h"
#include "ucg_group.h"
#include "ucg_plan.h"

#include "ucp/core/ucp_ep.inl"

static uint8_t ucg_listener_am_id = 0;

static void ucg_group_listener_accept_cb(ucp_ep_h ep, void *arg)
{
    ucg_group_h group = (ucg_group_h)arg;
    ucg_group_store_ep(&group->p2p_eps, group->params.member_count++, ep);
}

ucs_status_t ucg_group_listener_create(ucg_group_h group,
                                       ucs_sock_addr_t *bind_address,
                                       ucg_listener_h *listener_p)
{
    ucs_status_t status;
    ucg_listener_h listener;
    ucp_listener_params_t params = {
            .field_mask     = UCP_LISTENER_PARAM_FIELD_SOCK_ADDR |
                              UCP_LISTENER_PARAM_FIELD_ACCEPT_HANDLER,
            .sockaddr       = *bind_address,
            .accept_handler = {
                    .cb     = ucg_group_listener_accept_cb,
                    .arg    = group
            }
    };

    listener = UCS_ALLOC_CHECK(sizeof(*listener), "ucg_listener");
    status   = ucp_listener_create(group->worker, &params, &listener->super);
    if (status != UCS_OK) {
        ucs_free(listener);
        return status;
    }

    listener->group = group;
    *listener_p     = listener;
    return UCS_OK;
}

ucs_status_t ucg_group_listener_connect(ucg_group_h group,
                                        ucs_sock_addr_t *listener_addr)
{
    ucp_ep_h ep;
    ucs_status_t status;
    ucp_ep_params_t params = {
            .field_mask = UCP_EP_PARAM_FIELD_SOCK_ADDR |
                          UCP_EP_PARAM_FIELD_FLAGS,
            .sockaddr   = *listener_addr,
            .flags      = UCP_EP_PARAMS_FLAGS_CLIENT_SERVER
    };

    status = ucp_ep_create(group->worker, &params, &ep);
    if (status != UCS_OK) {
        return status;
    }

    /* Store this endpoint as the root */
    ucg_group_store_ep(&group->p2p_eps, 0, ep);

    /* wait for the group information to arrive (via Active Message) */
    return ucg_collective_acquire_barrier(group);
}

static size_t ucg_group_listener_pack_info(void *dest, void *arg)
{
    memcpy(dest, arg, sizeof(ucg_listener_group_info_t));
    return sizeof(ucg_listener_group_info_t);
}

static ucs_status_t
ucg_group_listener_set_info_cb(void *arg, void *data, size_t length,
                               unsigned flags)
{
    ucg_group_h group;
    ucs_list_link_t *groups_head    = (ucs_list_link_t*)arg;
    ucg_listener_group_info_t *info = (ucg_listener_group_info_t*)data;

    ucs_assert(length == sizeof(*info));

    ucs_list_for_each(group, groups_head, list) {
        if (group->params.id == info->id) {
            group->params.member_index = info->member_index;
            group->params.member_count = info->member_count;
            return ucg_collective_release_barrier(group);
        }
    }

    return UCS_ERR_NO_ELEM;
}

static void ucg_group_listener_trace_info_cb(void *arg,
                                             uct_am_trace_type_t type,
                                             uint8_t id, const void *data,
                                             size_t length, char *buffer,
                                             size_t max)
{
}

ucs_status_t ucg_listener_am_init(uint8_t am_id, ucs_list_link_t *groups_head)
{
    ucg_listener_am_id = am_id;
    return ucg_context_set_am_handler(groups_head, am_id,
                                      ucg_group_listener_set_info_cb,
                                      ucg_group_listener_trace_info_cb);
}

void ucg_group_listener_destroy(ucg_listener_h listener)
{
    ucp_ep_h ucp_ep;
    uct_ep_h uct_ep;
    ucs_status_t status;
    ucp_lane_index_t lane;
    ucg_group_member_index_t idx;

    ucg_group_h group              = listener->group;
    ucg_listener_group_info_t info = {
        .id           = group->params.id,
        .member_count = group->params.member_count
    };

    for (idx = 1; idx < group->params.member_count; idx++) {
        info.member_index = idx;
        ucp_ep            = ucp_plan_get_p2p_ep_by_index(group, idx);

        do {
            lane   = ucp_ep_get_am_lane(ucp_ep);
            uct_ep = ucp_ep_get_am_uct_ep(ucp_ep);
            status = ucg_plan_await_lane_connection(group->worker, ucp_ep,
                                                    lane, uct_ep);
        } while (status == UCS_INPROGRESS);

        (void) uct_ep_am_bcopy(ucp_ep_get_am_uct_ep(ucp_ep), ucg_listener_am_id,
                               ucg_group_listener_pack_info, &info, 0);
    }

    for (idx = 1; idx < group->params.member_count; idx++) {
        ucp_ep = ucp_plan_get_p2p_ep_by_index(group, idx);
        while ((ucp_ep->flags & UCP_EP_FLAG_FLUSH_STATE_VALID) == 0) {
            ucp_worker_progress(group->worker);
        }
    }

    ucp_listener_destroy(listener->super);
}
