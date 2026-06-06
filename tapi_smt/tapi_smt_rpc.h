/* SPDX-License-Identifier: MIT */
/* Copyright (C) 2026 Interpretica, Unipessoal Lda. All rights reserved. */
/** @file
 * @brief SMT TAPI: RPC client wrappers
 *
 * Client wrappers of the smt_* RPCs, see smt_rpc.x.m4. Tests use
 * tapi_smt.h; these are the calls behind it, one per RPC.
 */

#ifndef __TAPI_SMT_RPC_H__
#define __TAPI_SMT_RPC_H__

#include "te_defs.h"
#include "te_errno.h"
#include "te_string.h"
#include "rcf_rpc.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Decide one SMT-LIB 2 problem on the agent.
 *
 * @param[in]  rpcs                 RPC server on the agent.
 * @param[in]  engine               @c 0 Z3, @c 1 cvc5.
 * @param[in]  smtlib2              The problem.
 * @param[in]  produce_model        Read a model when @c sat.
 * @param[in]  produce_unsat_core   Read an unsat core when @c unsat.
 * @param[in]  timeout_ms           Engine time limit, ms, or @c 0.
 * @param[in]  random_seed          Search seed, or @c 0.
 * @param[out] status               @c 0 sat, @c 1 unsat, @c 2 unknown.
 * @param[out] model                Model text, or @c NULL to ignore.
 * @param[out] unsat_core           Core text, or @c NULL to ignore.
 * @param[out] version              Engine version, or @c NULL.
 * @param[out] reason               Unknown/error detail, or @c NULL.
 *
 * @return Status code (the engine's, or TE_ECORRUPTED on RPC failure).
 */
extern te_errno rpc_smt_solve(rcf_rpc_server *rpcs, int engine,
                              const char *smtlib2, te_bool produce_model,
                              te_bool produce_unsat_core, int timeout_ms,
                              unsigned int random_seed, int *status,
                              te_string *model, te_string *unsat_core,
                              te_string *version, te_string *reason);

/**
 * Is an engine compiled into the agent and able to answer?
 *
 * @param[in]  rpcs     RPC server on the agent.
 * @param[in]  engine   @c 0 Z3, @c 1 cvc5.
 * @param[out] version  Engine version, or @c NULL.
 *
 * @return @c 0 when the engine is usable, TE_EOPNOTSUPP when not.
 */
extern te_errno rpc_smt_available(rcf_rpc_server *rpcs, int engine,
                                  te_string *version);

#ifdef __cplusplus
} /* extern "C" */
#endif
#endif /* !__TAPI_SMT_RPC_H__ */
