/* SPDX-License-Identifier: MIT */
/* Copyright (C) 2026 Interpretica, Unipessoal Lda. All rights reserved. */
/** @file
 * @brief SMT RPC server library
 *
 * The smt_* RPCs (see smt_rpc.x.m4) on top of ta_smt.
 * TARPC_FUNC_STATIC() binds an RPC to the function of the same name,
 * so each RPC has a plain C function first and the wrapper after it.
 */

#define TE_LGR_USER     "RPC SMT"

#include "te_config.h"

#include <string.h>

#include "te_defs.h"
#include "te_errno.h"
#include "te_alloc.h"
#include "te_str.h"
#include "te_string.h"
#include "rpc_server.h"

#include "ta_smt.h"

/* Hand a te_string result over to an RPC string field (never NULL). */
static char *
take(te_string *str)
{
    return str->ptr != NULL ? str->ptr : TE_STRDUP("");
}

static te_errno
smt_solve(int engine, const char *smtlib2, te_bool produce_model,
          te_bool produce_unsat_core, int timeout_ms,
          unsigned int random_seed, int *status, char **model,
          char **unsat_core, char **version, char **reason)
{
    te_string model_s = TE_STRING_INIT;
    te_string core_s = TE_STRING_INIT;
    te_string version_s = TE_STRING_INIT;
    te_string reason_s = TE_STRING_INIT;
    te_errno rc;

    rc = ta_smt_solve(engine, smtlib2, produce_model, produce_unsat_core,
                      timeout_ms, random_seed, status, &model_s, &core_s,
                      &version_s, &reason_s);

    *model = take(&model_s);
    *unsat_core = take(&core_s);
    *version = take(&version_s);
    *reason = take(&reason_s);

    return rc;
}

TARPC_FUNC_STATIC(smt_solve, {},
{
    int status = TA_SMT_UNKNOWN;

    MAKE_CALL(out->retval = func(in->engine, in->smtlib2, in->produce_model,
                                 in->produce_unsat_core, in->timeout_ms,
                                 in->random_seed, &status, &out->model,
                                 &out->unsat_core, &out->engine_version,
                                 &out->reason));
    out->status = status;
    out->common.errno_changed = false;
})

static te_errno
smt_available(int engine, char **version)
{
    te_string version_s = TE_STRING_INIT;
    te_bool ok = ta_smt_available(engine, &version_s);

    *version = take(&version_s);

    return ok ? 0 : TE_RC(TE_TA_UNIX, TE_EOPNOTSUPP);
}

TARPC_FUNC_STATIC(smt_available, {},
{
    MAKE_CALL(out->retval = func(in->engine, &out->engine_version));
    out->common.errno_changed = false;
})
