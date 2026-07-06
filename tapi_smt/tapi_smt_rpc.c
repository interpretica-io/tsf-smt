/* SPDX-License-Identifier: MIT */
/* Copyright (C) 2026 Interpretica, Unipessoal Lda. All rights reserved. */
/** @file
 * @brief SMT TAPI: RPC client wrappers
 *
 * The rcf_rpc_call() boilerplate behind tapi_smt. The RPCs return
 * te_errno; an RPC transport failure is mapped to TE_ECORRUPTED.
 */

#define TE_LGR_USER     "TAPI SMT RPC"

#include "te_config.h"

#include <string.h>

#include "te_defs.h"
#include "te_errno.h"
#include "te_string.h"
#include "logger_api.h"
#include "tapi_rpc_internal.h"
#include "tarpc.h"

#include "tapi_smt_rpc.h"

#define CHECK_RPC_ERRNO_UNCHANGED(_func, _var) \
    CHECK_RETVAL_VAR_ERR_COND(_func, _var, false,                    \
                              TE_RC(TE_TAPI, TE_ECORRUPTED), false)

/* Append an RPC string result, when there is one. */
static void
take_string(te_string *dst, const char *src)
{
    if (dst != NULL && src != NULL)
        te_string_append(dst, "%s", src);
}

/* See description in tapi_smt_rpc.h */
te_errno
rpc_smt_solve(rcf_rpc_server *rpcs, int engine, const char *smtlib2,
              te_bool produce_model, te_bool produce_unsat_core,
              int timeout_ms, unsigned int random_seed, int *status,
              te_string *model, te_string *unsat_core, te_string *version,
              te_string *reason)
{
    tarpc_smt_solve_in in;
    tarpc_smt_solve_out out;

    memset(&in, 0, sizeof(in));
    memset(&out, 0, sizeof(out));
    in.engine = engine;
    in.smtlib2 = (char *)(smtlib2 != NULL ? smtlib2 : "");
    in.produce_model = produce_model;
    in.produce_unsat_core = produce_unsat_core;
    in.timeout_ms = timeout_ms;
    in.random_seed = random_seed;

    rcf_rpc_call(rpcs, "smt_solve", &in, &out);
    CHECK_RPC_ERRNO_UNCHANGED(smt_solve, out.retval);
    TAPI_RPC_LOG(rpcs, smt_solve, "engine=%d, model=%d, core=%d", "%r st=%d",
                 engine, produce_model, produce_unsat_core, out.retval,
                 out.status);

    if (out.retval == 0 && status != NULL)
        *status = out.status;
    /* The model/core/version/reason come back whatever the verdict;
     * the caller reads whichever ones it asked for. */
    take_string(model, out.model);
    take_string(unsat_core, out.unsat_core);
    take_string(version, out.engine_version);
    take_string(reason, out.reason);

    RETVAL_TE_ERRNO(smt_solve, out.retval);
}

/* See description in tapi_smt_rpc.h */
te_errno
rpc_smt_available(rcf_rpc_server *rpcs, int engine, te_string *version)
{
    tarpc_smt_available_in in;
    tarpc_smt_available_out out;

    memset(&in, 0, sizeof(in));
    memset(&out, 0, sizeof(out));
    in.engine = engine;

    rcf_rpc_call(rpcs, "smt_available", &in, &out);
    CHECK_RPC_ERRNO_UNCHANGED(smt_available, out.retval);
    TAPI_RPC_LOG(rpcs, smt_available, "engine=%d", "%r", engine, out.retval);

    if (out.retval == 0)
        take_string(version, out.engine_version);

    RETVAL_TE_ERRNO(smt_available, out.retval);
}
