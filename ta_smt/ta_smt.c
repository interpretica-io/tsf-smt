/* SPDX-License-Identifier: MIT */
/* Copyright (C) 2026 Interpretica, Unipessoal Lda. All rights reserved. */
/** @file
 * @brief Agent-side SMT solving: dispatcher and the Z3 backend
 *
 * ta_smt_solve() picks a backend; the Z3 backend here drives Z3 through
 * its C API (@c z3.h) - the library is linked and called in-process,
 * nothing is spawned. The cvc5 backend is in ta_smt_cvc5.cpp.
 */

#define TE_LGR_USER     "TA SMT"

#include "te_config.h"

#include <string.h>

#include <z3.h>

#include "te_defs.h"
#include "te_errno.h"
#include "te_alloc.h"
#include "te_str.h"
#include "te_string.h"
#include "logger_api.h"

#include "ta_smt.h"

/* See description in ta_smt.h */
te_errno
ta_smt_solve(int engine, const char *smtlib2, te_bool model,
             te_bool unsat_core, int timeout_ms, unsigned int random_seed,
             int *status, te_string *model_out, te_string *core_out,
             te_string *version, te_string *reason)
{
    switch (engine)
    {
        case TA_SMT_ENGINE_Z3:
            return ta_smt_z3_solve(smtlib2, model, unsat_core, timeout_ms,
                                   random_seed, status, model_out, core_out,
                                   version, reason);
        case TA_SMT_ENGINE_CVC5:
        {
            /*
             * The cvc5 backend is C++ and includes no TE headers, so it
             * speaks a plain-C seam: it fills char* out-strings (heap)
             * and returns 0/1/2. Adapt that to te_string/te_errno here.
             */
            char *m = NULL;
            char *c = NULL;
            char *v = NULL;
            char *r = NULL;
            int crc = ta_smt_cvc5_solve_raw(smtlib2, model, unsat_core,
                                            timeout_ms, random_seed, status,
                                            &m, &c, &v, &r);

            if (m != NULL)
            {
                te_string_append(model_out, "%s", m);
                free(m);
            }
            if (c != NULL)
            {
                te_string_append(core_out, "%s", c);
                free(c);
            }
            if (v != NULL)
            {
                te_string_append(version, "%s", v);
                free(v);
            }
            if (r != NULL)
            {
                te_string_append(reason, "%s", r);
                free(r);
            }

            if (crc == 1)
                return TE_RC(TE_TA_UNIX, TE_ESHCMD);
            if (crc != 0)
                return TE_RC(TE_TA_UNIX, TE_EFAIL);
            return 0;
        }
        default:
            ERROR("Unknown SMT engine %d", engine);
            return TE_RC(TE_TA_UNIX, TE_EINVAL);
    }
}

/* See description in ta_smt.h */
te_bool
ta_smt_available(int engine, te_string *version)
{
    te_string model = TE_STRING_INIT;
    te_string core = TE_STRING_INIT;
    te_string reason = TE_STRING_INIT;
    int status = TA_SMT_UNKNOWN;
    te_errno rc;

    /*
     * Deciding (assert true) touches everything a real solve does - the
     * engine is linked, it parses, it checks - and is trivially sat, so
     * it is the cheapest honest probe.
     */
    rc = ta_smt_solve(engine, "(assert true)", false, false, 0, 0, &status,
                      &model, &core, version, &reason);

    te_string_free(&model);
    te_string_free(&core);
    te_string_free(&reason);

    return rc == 0;
}

/*
 * ---- Z3 backend ----------------------------------------------------
 */

/** A no-op error handler so a Z3 error never aborts the agent; the code
 * is read back with Z3_get_error_code() instead. */
static void
z3_quiet_error(Z3_context ctx, Z3_error_code code)
{
    UNUSED(ctx);
    UNUSED(code);
}

/** Append the Z3 version, e.g. "4.13.0.0", to @p version. */
static void
z3_version(te_string *version)
{
    unsigned major = 0;
    unsigned minor = 0;
    unsigned build = 0;
    unsigned revision = 0;

    if (version == NULL)
        return;
    Z3_get_version(&major, &minor, &build, &revision);
    te_string_append(version, "%u.%u.%u.%u", major, minor, build, revision);
}

/** Read the model's constant assignments into "name\tvalue\n" lines. */
static void
z3_read_model(Z3_context ctx, Z3_solver solver, te_string *model_out)
{
    Z3_model model = Z3_solver_get_model(ctx, solver);
    unsigned n;
    unsigned i;

    if (model == NULL)
        return;
    Z3_model_inc_ref(ctx, model);

    n = Z3_model_get_num_consts(ctx, model);
    for (i = 0; i < n; i++)
    {
        Z3_func_decl decl = Z3_model_get_const_decl(ctx, model, i);
        Z3_symbol sym = Z3_get_decl_name(ctx, decl);
        /* The symbol string is valid only until the next Z3 call, so
         * copy it before asking for the value. */
        char *name = TE_STRDUP(Z3_get_symbol_string(ctx, sym));
        Z3_ast value = Z3_model_get_const_interp(ctx, model, decl);
        const char *value_str = value != NULL ?
                                Z3_ast_to_string(ctx, value) : "";

        te_string_append(model_out, "%s\t%s\n", name, value_str);
        free(name);
    }

    Z3_model_dec_ref(ctx, model);
}

/** Read the unsat core's names into one-per-line text. */
static void
z3_read_core(Z3_context ctx, Z3_solver solver, te_string *core_out)
{
    Z3_ast_vector core = Z3_solver_get_unsat_core(ctx, solver);
    unsigned n;
    unsigned i;

    if (core == NULL)
        return;
    Z3_ast_vector_inc_ref(ctx, core);

    n = Z3_ast_vector_size(ctx, core);
    for (i = 0; i < n; i++)
    {
        Z3_ast term = Z3_ast_vector_get(ctx, core, i);

        te_string_append(core_out, "%s\n", Z3_ast_to_string(ctx, term));
    }

    Z3_ast_vector_dec_ref(ctx, core);
}

/* See description in ta_smt.h */
te_errno
ta_smt_z3_solve(const char *smtlib2, te_bool model, te_bool unsat_core,
                int timeout_ms, unsigned int random_seed, int *status,
                te_string *model_out, te_string *core_out,
                te_string *version, te_string *reason)
{
    Z3_config config;
    Z3_context ctx;
    Z3_solver solver;
    Z3_params params;
    te_string problem = TE_STRING_INIT;
    Z3_lbool result;
    te_errno rc = 0;

    z3_version(version);

    config = Z3_mk_config();
    ctx = Z3_mk_context(config);
    Z3_del_config(config);
    Z3_set_error_handler(ctx, z3_quiet_error);

    solver = Z3_mk_solver(ctx);
    Z3_solver_inc_ref(ctx, solver);

    params = Z3_mk_params(ctx);
    Z3_params_inc_ref(ctx, params);
    if (timeout_ms > 0)
    {
        Z3_params_set_uint(ctx, params, Z3_mk_string_symbol(ctx, "timeout"),
                           (unsigned)timeout_ms);
    }
    if (random_seed != 0)
    {
        Z3_params_set_uint(ctx, params,
                           Z3_mk_string_symbol(ctx, "random_seed"),
                           random_seed);
    }
    Z3_solver_set_params(ctx, solver, params);

    /*
     * The unsat core of named assertions is tracked only when the
     * SMT-LIB front-end is told so with a set-option in the text
     * itself; a solver parameter does not reach Z3_solver_from_string's
     * parser. Prepend the options; produce-models too, harmless when a
     * model was not asked for.
     */
    if (unsat_core)
        te_string_append(&problem, "(set-option :produce-unsat-cores true)\n");
    if (model)
        te_string_append(&problem, "(set-option :produce-models true)\n");
    te_string_append(&problem, "%s", smtlib2 != NULL ? smtlib2 : "");

    Z3_solver_from_string(ctx, solver, problem.ptr);
    if (Z3_get_error_code(ctx) != Z3_OK)
    {
        if (reason != NULL)
        {
            te_string_append(reason, "%s",
                Z3_get_error_msg(ctx, Z3_get_error_code(ctx)));
        }
        ERROR("Z3 rejected the problem: %s",
              Z3_get_error_msg(ctx, Z3_get_error_code(ctx)));
        rc = TE_RC(TE_TA_UNIX, TE_ESHCMD);
        goto out;
    }

    result = Z3_solver_check(ctx, solver);
    if (Z3_get_error_code(ctx) != Z3_OK)
    {
        if (reason != NULL)
        {
            te_string_append(reason, "%s",
                Z3_get_error_msg(ctx, Z3_get_error_code(ctx)));
        }
        rc = TE_RC(TE_TA_UNIX, TE_EFAIL);
        goto out;
    }

    switch (result)
    {
        case Z3_L_TRUE:
            *status = TA_SMT_SAT;
            if (model)
                z3_read_model(ctx, solver, model_out);
            break;

        case Z3_L_FALSE:
            *status = TA_SMT_UNSAT;
            if (unsat_core)
                z3_read_core(ctx, solver, core_out);
            break;

        default:
            *status = TA_SMT_UNKNOWN;
            if (reason != NULL)
            {
                te_string_append(reason, "%s",
                    Z3_solver_get_reason_unknown(ctx, solver));
            }
            break;
    }

out:
    Z3_params_dec_ref(ctx, params);
    Z3_solver_dec_ref(ctx, solver);
    Z3_del_context(ctx);
    te_string_free(&problem);

    return rc;
}
