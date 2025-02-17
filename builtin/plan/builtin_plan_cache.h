/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2021-2021.  All rights reserved.
 * Description: UCG builtin plan cache mechanism
 * Author: shizhibao
 * Create: 2021-08-06
 */

#ifndef UCG_BUILTIN_PLAN_CACHE_H
#define UCG_BUILTIN_PLAN_CACHE_H

#include <ucs/sys/compiler.h>
#include <ucg/base/ucg_group.h>

BEGIN_C_DECLS

ucs_status_t ucg_builtin_pcache_init(ucg_group_h group);

void ucg_builtin_pcache_destroy(ucg_group_h group);
 
ucg_plan_t *ucg_builtin_pcache_find(const ucg_group_h group, int algo,
                                    const ucg_collective_params_t *coll_params);

void ucg_builtin_pcache_update(ucg_group_h group, ucg_plan_t *plan, int algo,
                                    const ucg_collective_params_t *coll_params);

END_C_DECLS

#endif /* !UCG_BUILTIN_PLAN_CACHE_H */
