/* SPDX-License-Identifier: MIT */
/* Copyright (C) 2026 Interpretica, Unipessoal Lda. All rights reserved. */
/** @file
 * @brief Agent-side SMT solving
 *
 * Deciding an SMT-LIB 2 problem with an SMT solver, in the agent's own
 * process, through the solver's C/C++ library API - Z3's C API
 * (@c z3.h, @c -lz3) and cvc5's C++ API (@c cvc5/cvc5.h, @c -lcvc5).
 * Nothing is spawned: the library is linked and called. The agent and
 * its RPC server both link this; the RPCs (see smt_rpc.x.m4) are thin
 * wrappers over these functions.
 *
 * Both engines are driven through one entry point, ta_smt_solve(). The
 * two backends live in their own translation units - Z3 in ta_smt.c
 * (C), cvc5 in ta_smt_cvc5.cpp (C++) - behind a common C seam, so a C++
 * compiler is needed for the cvc5 unit and nothing else is C++.
 *
 * Results that are lists come back as newline-separated text, one
 * record per line, the same shape tsf-upnp and tsf-appium use: the
 * engine side parses them. A model is one @c "name\\tvalue" line per
 * assignment; an unsat core is one name (Z3) or asserted term (cvc5)
 * per line.
 */

#ifndef __TA_SMT_H__
#define __TA_SMT_H__

#include "te_defs.h"
#include "te_errno.h"
#include "te_string.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Which engine to drive. The values are the RPC's wire values. */
enum {
    TA_SMT_ENGINE_Z3 = 0,   /**< Z3, through its C API. */
    TA_SMT_ENGINE_CVC5 = 1, /**< cvc5, through its C++ API. */
};

/** The verdict. The values are the RPC's wire values. */
enum {
    TA_SMT_SAT = 0,         /**< Satisfiable. */
    TA_SMT_UNSAT = 1,       /**< Unsatisfiable. */
    TA_SMT_UNKNOWN = 2,     /**< Undecided. */
};

/**
 * Decide an SMT-LIB 2 problem.
 *
 * The problem - declarations and assertions - is parsed by the engine
 * and checked in this process. A @c (check-sat) in the problem is not
 * needed (the check is this call) and does no harm.
 *
 * @param[in]  engine       @c TA_SMT_ENGINE_*.
 * @param[in]  smtlib2      The problem.
 * @param[in]  model        Read a model back when the answer is @c sat.
 * @param[in]  unsat_core   Read an unsat core back when the answer is
 *                          @c unsat (the problem must name the
 *                          assertions, @c (assert (! phi :named a))).
 * @param[in]  timeout_ms   Per-solve engine time limit, ms, or @c 0.
 * @param[in]  random_seed  Seed for a reproducible search, or @c 0.
 * @param[out] status       The verdict, a @c TA_SMT_* value.
 * @param[out] model_out    When @p model and @c sat: the model, one
 *                          @c "name\\tvalue" line per assignment.
 * @param[out] core_out     When @p unsat_core and @c unsat: the core,
 *                          one entry per line.
 * @param[out] version      The engine's version string.
 * @param[out] reason       When undecided or on an engine error, the
 *                          engine's explanation; empty otherwise.
 *
 * @return Status code.
 * @retval 0                The problem was decided; @p status says how.
 * @retval TE_EINVAL        @p engine is not a known @c TA_SMT_ENGINE_*.
 * @retval TE_ESHCMD        The engine rejected the problem (a parse
 *                          error, an unknown logic); @p reason has the
 *                          detail.
 * @retval TE_EFAIL         The engine failed unexpectedly; see @p reason.
 */
extern te_errno ta_smt_solve(int engine, const char *smtlib2,
                             te_bool model, te_bool unsat_core,
                             int timeout_ms, unsigned int random_seed,
                             int *status, te_string *model_out,
                             te_string *core_out, te_string *version,
                             te_string *reason);

/**
 * Is an engine usable in this agent?
 *
 * @param[in]  engine       @c TA_SMT_ENGINE_*.
 * @param[out] version      The engine's version string, when present.
 *
 * @return @c true when the engine is compiled in and answers a trivial
 *         problem.
 */
extern te_bool ta_smt_available(int engine, te_string *version);

/*
 * The per-engine backends, behind a common C seam. ta_smt_solve()
 * dispatches to one of these; each is defined in its own translation
 * unit (Z3 in ta_smt.c, cvc5 in ta_smt_cvc5.cpp) and both are linked
 * in, so the library always carries both engines.
 */

/** Z3 backend (ta_smt.c, Z3 C API). */
extern te_errno ta_smt_z3_solve(const char *smtlib2, te_bool model,
                                te_bool unsat_core, int timeout_ms,
                                unsigned int random_seed, int *status,
                                te_string *model_out, te_string *core_out,
                                te_string *version, te_string *reason);

/**
 * cvc5 backend (ta_smt_cvc5.cpp, cvc5 C++ API). Plain-C seam: it
 * includes no TE headers (they are not C++-safe), so it takes @c int
 * flags and @c char** out-parameters (heap strings, caller frees) and
 * returns @c 0 ok / @c 1 parse error / @c 2 engine failure. The
 * dispatcher in ta_smt.c adapts it to the te_string/te_errno world.
 */
extern int ta_smt_cvc5_solve_raw(const char *smtlib2, int model,
                                 int unsat_core, int timeout_ms,
                                 unsigned int random_seed, int *status,
                                 char **model_out, char **core_out,
                                 char **version, char **reason);

#ifdef __cplusplus
} /* extern "C" */
#endif
#endif /* !__TA_SMT_H__ */
