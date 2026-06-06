/* SPDX-License-Identifier: MIT */
/* Copyright (C) 2026 Interpretica, Unipessoal Lda. All rights reserved. */
/** @file
 * @brief Proving things with an SMT solver from a test
 *
 * @defgroup tapi_smt SMT solvers (tapi_smt)
 * @{
 *
 * Deciding a logical formula on a Test Agent with an SMT solver, and
 * reading the verdict back: Z3 and cvc5, through one engine-neutral
 * API. A test hands over a problem in SMT-LIB 2 - declarations and
 * assertions - and gets @c sat, @c unsat or @c unknown, a model when
 * it is @c sat, and an unsat core when it is @c unsat.
 *
 * The solver runs in the agent's RPC server process, called through
 * the solver's own library API (Z3's C API, cvc5's C++ API): the
 * library is linked into the agent, not run as a program. So the
 * problem is decided where the agent is, and nothing of it leaves the
 * agent - there is no provider, no key, no network, unlike tsf-ai.
 *
 * Two ways to use it, the same underneath:
 *
 * - tapi_smt_check() decides a set of assertions as they stand - the
 *   question "is there an assignment that satisfies all of these?";
 * - tapi_smt_prove() asks the other question, the one a test usually
 *   wants: "does this conjecture follow from these assumptions?". It
 *   asserts the assumptions and the *negation* of the conjecture and
 *   expects @c unsat - the standard refutation proof. An @c unsat is
 *   the conjecture proved; a @c sat hands back a counter-model that
 *   shows why it does not hold.
 *
 * @code
 * tapi_smt_result result;
 *
 * if (!tapi_smt_available(rpcs, TAPI_SMT_Z3))
 *     TEST_SKIP("The agent has no Z3");
 *
 * // Does x > 2 follow from x > 3, over the integers? (It does.)
 * CHECK_RC(tapi_smt_prove(rpcs, TAPI_SMT_Z3,
 *                         "(set-logic QF_LIA)\n"
 *                         "(declare-const x Int)\n"
 *                         "(assert (> x 3))\n",
 *                         "(> x 2)", NULL, &result));
 * if (result.status != TAPI_SMT_UNSAT)
 *     TEST_FAIL("Expected the conjecture to be proved");
 * tapi_smt_result_free(&result);
 * @endcode
 */

#ifndef __TAPI_SMT_H__
#define __TAPI_SMT_H__

#include "te_defs.h"
#include "te_errno.h"
#include "te_string.h"
#include "te_vector.h"
#include "rcf_rpc.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Which solver. The values are the RPC's wire values. */
typedef enum tapi_smt_engine {
    TAPI_SMT_Z3 = 0,    /**< Z3, through its C API. */
    TAPI_SMT_CVC5 = 1,  /**< cvc5, through its C++ API. */
} tapi_smt_engine;

/** The verdict on a problem. The values are the RPC's wire values. */
typedef enum tapi_smt_status {
    TAPI_SMT_SAT = 0,       /**< Satisfiable (a model may be read). */
    TAPI_SMT_UNSAT = 1,     /**< Unsatisfiable (an unsat core may be read). */
    TAPI_SMT_UNKNOWN = 2,   /**< Undecided - a timeout, an incomplete theory. */
} tapi_smt_status;

/** One assignment in a model: a declared symbol and its value. */
typedef struct tapi_smt_binding {
    /** The symbol's name, as it was declared. */
    char *name;
    /** Its value, printed the way the engine prints it (e.g. @c "3"). */
    char *value;
} tapi_smt_binding;

/** Options for a solve. Zero/NULL means "the engine's default". */
typedef struct tapi_smt_opts {
    /**
     * SMT-LIB logic to set before the assertions, e.g. @c "QF_LIA",
     * or @c NULL to leave it to a @c (set-logic ...) in the problem.
     * Prepended as @c (set-logic ...) when the problem sets none.
     */
    const char *logic;
    /**
     * Per-solve time limit inside the engine, ms, or @c 0 for none
     * (Z3 @c timeout, cvc5 @c tlimit-per). An engine that hits it
     * answers @c unknown.
     */
    unsigned int engine_timeout_ms;
    /** Read a model back when the answer is @c sat. */
    bool produce_model;
    /**
     * Read an unsat core back when the answer is @c unsat. The problem
     * must name the assertions it wants in the core,
     * @c (assert (! phi :named a)).
     */
    bool produce_unsat_core;
    /** Random seed, for a reproducible search; @c 0 for the default. */
    unsigned int random_seed;
} tapi_smt_opts;

/** Initializer for #tapi_smt_opts: all defaults. */
#define TAPI_SMT_OPTS_INIT { .logic = NULL }

/** The result of a solve. */
typedef struct tapi_smt_result {
    /** The verdict. */
    tapi_smt_status status;
    /** Why, when the engine said (e.g. @c "timeout"); may be @c NULL. */
    char *reason;
    /** Vector of #tapi_smt_binding; the model, empty unless @c sat. */
    te_vec model;
    /** Vector of @c char*; the unsat core, empty unless @c unsat. */
    te_vec unsat_core;
    /** The engine's version string, as it reported it; may be @c NULL. */
    char *engine_version;
} tapi_smt_result;

/**
 * Is the engine usable on the agent?
 *
 * Decides a trivial problem, so it checks the engine is linked into
 * the agent's RPC server and answers. Sends nothing anywhere.
 *
 * @param rpcs          RPC server on the agent.
 * @param engine        The engine.
 *
 * @return @c true when a solve could be attempted.
 */
extern bool tapi_smt_available(rcf_rpc_server *rpcs, tapi_smt_engine engine);

/**
 * Decide a set of assertions.
 *
 * @param[in]  rpcs         RPC server on the agent.
 * @param[in]  engine       The engine.
 * @param[in]  smtlib2      The problem in SMT-LIB 2: declarations and
 *                          assertions. A trailing @c (check-sat) is not
 *                          needed - the check is this call - but is
 *                          harmless.
 * @param[in]  opts         Options, or @c NULL.
 * @param[out] result       The verdict; release with
 *                          tapi_smt_result_free().
 *
 * @return Status code. A decided problem - @c sat, @c unsat or an
 *         engine @c unknown - is a success; @p result carries which.
 * @retval TE_EOPNOTSUPP    The engine is not in the agent.
 * @retval TE_ESHCMD        The engine rejected the problem (a parse
 *                          error, an unknown logic); the detail is in
 *                          @p result->reason and the log.
 */
extern te_errno tapi_smt_check(rcf_rpc_server *rpcs, tapi_smt_engine engine,
                               const char *smtlib2,
                               const tapi_smt_opts *opts,
                               tapi_smt_result *result);

/**
 * Prove a conjecture from a set of assumptions, by refutation.
 *
 * Asserts @p assumptions and @c (assert (not @p conjecture)) and
 * decides them. An @c unsat in @p result is the conjecture proved; a
 * @c sat is a counter-example, and the model in @p result is the
 * assignment that breaks the conjecture.
 *
 * @param[in]  rpcs         RPC server on the agent.
 * @param[in]  engine       The engine.
 * @param[in]  assumptions  SMT-LIB 2 declarations and assumption
 *                          assertions, or @c NULL.
 * @param[in]  conjecture   The conjecture, as one SMT-LIB 2 term (what
 *                          goes inside @c (assert ...)).
 * @param[in]  opts         Options, or @c NULL.
 * @param[out] result       The verdict; release with
 *                          tapi_smt_result_free(). @c unsat means proved.
 *
 * @return Status code, as tapi_smt_check().
 */
extern te_errno tapi_smt_prove(rcf_rpc_server *rpcs, tapi_smt_engine engine,
                               const char *assumptions,
                               const char *conjecture,
                               const tapi_smt_opts *opts,
                               tapi_smt_result *result);

/**
 * A value from a model by the symbol's name.
 *
 * @param result        A result from a @c sat solve.
 * @param name          The symbol's name.
 *
 * @return The value as the engine printed it, or @c NULL when the
 *         model has no such symbol. Owned by @p result.
 */
extern const char *tapi_smt_get(const tapi_smt_result *result,
                                const char *name);

/**
 * Write a result into the log.
 *
 * @param result        Result.
 */
extern void tapi_smt_result_log(const tapi_smt_result *result);

/**
 * Release a result.
 *
 * @param result        Result.
 */
extern void tapi_smt_result_free(tapi_smt_result *result);

/**
 * Spell out an engine.
 *
 * @param engine        The engine.
 *
 * @return A static string, never @c NULL.
 */
extern const char *tapi_smt_engine2str(tapi_smt_engine engine);

/**
 * Spell out a status.
 *
 * @param status        The status.
 *
 * @return A static string, never @c NULL.
 */
extern const char *tapi_smt_status2str(tapi_smt_status status);

#ifdef __cplusplus
} /* extern "C" */
#endif
#endif /* !__TAPI_SMT_H__ */

/**@} <!-- END tapi_smt --> */
