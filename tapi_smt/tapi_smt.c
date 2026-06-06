/* SPDX-License-Identifier: MIT */
/* Copyright (C) 2026 Interpretica, Unipessoal Lda. All rights reserved. */
/** @file
 * @brief Proving things with an SMT solver from a test
 *
 * The engine-neutral layer over the smt_* RPCs: it builds the problem
 * (prepending a logic or the refutation of a conjecture), asks the
 * agent to decide it, and parses the newline/tab result text into a
 * #tapi_smt_result.
 */

#define TE_LGR_USER     "TAPI SMT"

#include "te_config.h"

#include <string.h>

#include "te_defs.h"
#include "te_errno.h"
#include "te_alloc.h"
#include "te_str.h"
#include "te_string.h"
#include "te_vector.h"
#include "logger_api.h"

#include "tapi_smt.h"
#include "tapi_smt_rpc.h"

/* See description in tapi_smt.h */
const char *
tapi_smt_engine2str(tapi_smt_engine engine)
{
    switch (engine)
    {
        case TAPI_SMT_Z3:
            return "z3";
        case TAPI_SMT_CVC5:
            return "cvc5";
        default:
            return "unknown";
    }
}

/* See description in tapi_smt.h */
const char *
tapi_smt_status2str(tapi_smt_status status)
{
    switch (status)
    {
        case TAPI_SMT_SAT:
            return "sat";
        case TAPI_SMT_UNSAT:
            return "unsat";
        case TAPI_SMT_UNKNOWN:
            return "unknown";
        default:
            return "?";
    }
}

/** Zero a result and mark it undecided. */
static void
smt_result_init(tapi_smt_result *result)
{
    memset(result, 0, sizeof(*result));
    result->status = TAPI_SMT_UNKNOWN;
    result->model = (te_vec)TE_VEC_INIT(tapi_smt_binding);
    result->unsat_core = (te_vec)TE_VEC_INIT(char *);
}

/**
 * Parse the model text - one @c "name\tvalue" line per assignment -
 * into @p result->model.
 */
static void
smt_parse_model(const char *text, tapi_smt_result *result)
{
    const char *line = text;

    while (line != NULL && *line != '\0')
    {
        const char *nl = strchr(line, '\n');
        size_t len = nl != NULL ? (size_t)(nl - line) : strlen(line);
        const char *tab = memchr(line, '\t', len);

        if (tab != NULL)
        {
            tapi_smt_binding b;

            b.name = TE_STRNDUP(line, (size_t)(tab - line));
            b.value = TE_STRNDUP(tab + 1, len - (size_t)(tab - line) - 1);
            TE_VEC_APPEND(&result->model, b);
        }

        line = nl != NULL ? nl + 1 : NULL;
    }
}

/** Parse the unsat-core text - one entry per line - into the core. */
static void
smt_parse_core(const char *text, tapi_smt_result *result)
{
    const char *line = text;

    while (line != NULL && *line != '\0')
    {
        const char *nl = strchr(line, '\n');
        size_t len = nl != NULL ? (size_t)(nl - line) : strlen(line);

        if (len != 0)
        {
            char *entry = TE_STRNDUP(line, len);

            TE_VEC_APPEND(&result->unsat_core, entry);
        }

        line = nl != NULL ? nl + 1 : NULL;
    }
}

/* See description in tapi_smt.h */
bool
tapi_smt_available(rcf_rpc_server *rpcs, tapi_smt_engine engine)
{
    return rpc_smt_available(rpcs, (int)engine, NULL) == 0;
}

/* See description in tapi_smt.h */
te_errno
tapi_smt_check(rcf_rpc_server *rpcs, tapi_smt_engine engine,
               const char *smtlib2, const tapi_smt_opts *opts,
               tapi_smt_result *result)
{
    te_string problem = TE_STRING_INIT;
    te_string model = TE_STRING_INIT;
    te_string core = TE_STRING_INIT;
    te_string version = TE_STRING_INIT;
    te_string reason = TE_STRING_INIT;
    int status = TAPI_SMT_UNKNOWN;
    te_errno rc;

    smt_result_init(result);

    /*
     * A logic from opts is written in only when the problem does not
     * set one itself; the helper needs no notion of a logic of its own.
     */
    if (opts != NULL && opts->logic != NULL &&
        (smtlib2 == NULL || strstr(smtlib2, "(set-logic") == NULL))
    {
        te_string_append(&problem, "(set-logic %s)\n", opts->logic);
    }
    te_string_append(&problem, "%s", smtlib2 != NULL ? smtlib2 : "");

    rc = rpc_smt_solve(rpcs, (int)engine, te_string_value(&problem),
                       opts != NULL && opts->produce_model,
                       opts != NULL && opts->produce_unsat_core,
                       opts != NULL ? (int)opts->engine_timeout_ms : 0,
                       opts != NULL ? opts->random_seed : 0,
                       &status, &model, &core, &version, &reason);

    if (version.len != 0)
        result->engine_version = TE_STRDUP(te_string_value(&version));
    if (reason.len != 0)
        result->reason = TE_STRDUP(te_string_value(&reason));

    if (rc != 0)
    {
        ERROR("%s rejected the problem: %s", tapi_smt_engine2str(engine),
              reason.len != 0 ? te_string_value(&reason) : "(no detail)");
        goto out;
    }

    result->status = (tapi_smt_status)status;
    if (status == TAPI_SMT_SAT)
        smt_parse_model(te_string_value(&model), result);
    else if (status == TAPI_SMT_UNSAT)
        smt_parse_core(te_string_value(&core), result);

out:
    te_string_free(&problem);
    te_string_free(&model);
    te_string_free(&core);
    te_string_free(&version);
    te_string_free(&reason);

    return rc;
}

/* See description in tapi_smt.h */
te_errno
tapi_smt_prove(rcf_rpc_server *rpcs, tapi_smt_engine engine,
               const char *assumptions, const char *conjecture,
               const tapi_smt_opts *opts, tapi_smt_result *result)
{
    te_string problem = TE_STRING_INIT;
    te_errno rc;

    smt_result_init(result);

    if (conjecture == NULL)
    {
        ERROR("tapi_smt_prove: no conjecture");
        return TE_RC(TE_TAPI, TE_EINVAL);
    }

    /*
     * Refutation: assert the hypotheses and the negation of the
     * conjecture. unsat means nothing contradicts the conjecture - it
     * is proved; sat means the model breaks it.
     */
    if (assumptions != NULL)
        te_string_append(&problem, "%s\n", assumptions);
    te_string_append(&problem, "(assert (not %s))\n", conjecture);

    rc = tapi_smt_check(rpcs, engine, te_string_value(&problem), opts,
                        result);

    te_string_free(&problem);

    return rc;
}

/* See description in tapi_smt.h */
const char *
tapi_smt_get(const tapi_smt_result *result, const char *name)
{
    const tapi_smt_binding *b;

    TE_VEC_FOREACH((te_vec *)&result->model, b)
    {
        if (b->name != NULL && strcmp(b->name, name) == 0)
            return b->value;
    }

    return NULL;
}

/* See description in tapi_smt.h */
void
tapi_smt_result_log(const tapi_smt_result *result)
{
    te_string s = TE_STRING_INIT;
    const tapi_smt_binding *b;
    char * const *entry;

    te_string_append(&s, "SMT verdict: %s",
                     tapi_smt_status2str(result->status));
    if (result->reason != NULL)
        te_string_append(&s, " (%s)", result->reason);
    if (result->engine_version != NULL)
        te_string_append(&s, " [%s]", result->engine_version);

    if (te_vec_size(&result->model) != 0)
    {
        te_string_append(&s, "\nmodel:");
        TE_VEC_FOREACH((te_vec *)&result->model, b)
            te_string_append(&s, "\n  %s = %s", b->name, b->value);
    }
    if (te_vec_size(&result->unsat_core) != 0)
    {
        te_string_append(&s, "\nunsat core:");
        TE_VEC_FOREACH((te_vec *)&result->unsat_core, entry)
            te_string_append(&s, " %s", *entry);
    }

    RING("%s", te_string_value(&s));
    te_string_free(&s);
}

/* See description in tapi_smt.h */
void
tapi_smt_result_free(tapi_smt_result *result)
{
    tapi_smt_binding *b;
    char **entry;

    TE_VEC_FOREACH(&result->model, b)
    {
        free(b->name);
        free(b->value);
    }
    te_vec_free(&result->model);

    TE_VEC_FOREACH(&result->unsat_core, entry)
        free(*entry);
    te_vec_free(&result->unsat_core);

    free(result->reason);
    free(result->engine_version);
    memset(result, 0, sizeof(*result));
}
