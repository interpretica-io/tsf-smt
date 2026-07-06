/* SPDX-License-Identifier: MIT */
/* Copyright (C) 2026 Interpretica, Unipessoal Lda. All rights reserved. */
/** @file
 * @brief RPC for SMT solving
 *
 * The RPCs of rpcs_smt, a thin layer over ta_smt, which decides an
 * SMT-LIB 2 problem with Z3 or cvc5 in the RPC server process through
 * the solver's own library API. Add this file to the rpcxdr definitions
 * of the engine platform and of the agent platform:
 *
 *   TE_LIB_PARMS([rpcxdr], [<platform>], [],
 *                [--with-rpcdefs=tarpc_job.x.m4,../ta_smt/smt_rpc.x.m4])
 *
 * The solver has no state between calls: every solve is a whole problem
 * in and a verdict out. Results that are lists come back as
 * newline-separated text, the same shape tsf-upnp and tsf-appium use -
 * the engine side parses them. A model is one "name \t value" line per
 * assignment; an unsat core is one entry per line.
 */

/*
 * smt_solve(): decide one SMT-LIB 2 problem.
 *
 *   engine     0 = Z3, 1 = cvc5
 *   status     0 = sat, 1 = unsat, 2 = unknown (valid when retval == 0)
 *   model      when produce_model and sat: "name \t value" per line
 *   unsat_core when produce_unsat_core and unsat: one entry per line
 *   version    the engine's version string
 *   reason     unknown's explanation, or an engine error's detail
 */
struct tarpc_smt_solve_in {
    struct tarpc_in_arg common;

    tarpc_int       engine;
    string          smtlib2<>;
    tarpc_bool      produce_model;
    tarpc_bool      produce_unsat_core;
    tarpc_int       timeout_ms;        /* engine time limit, ms, or 0 */
    tarpc_uint      random_seed;       /* search seed, or 0 */
};

struct tarpc_smt_solve_out {
    struct tarpc_out_arg common;

    tarpc_int       retval;
    tarpc_int       status;
    string          model<>;
    string          unsat_core<>;
    string          engine_version<>;
    string          reason<>;
};

/*
 * smt_available(): is an engine compiled into this agent and able to
 * decide a trivial problem? retval is 0 when it is.
 */
struct tarpc_smt_available_in {
    struct tarpc_in_arg common;

    tarpc_int       engine;
};

struct tarpc_smt_available_out {
    struct tarpc_out_arg common;

    tarpc_int       retval;
    string          engine_version<>;
};

program smt
{
    version ver0
    {
        RPC_DEF(smt_solve)
        RPC_DEF(smt_available)
    } = 1;
} = 25;
