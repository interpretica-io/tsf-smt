/* SPDX-License-Identifier: MIT */
/* Copyright (C) 2026 Interpretica, Unipessoal Lda. All rights reserved. */
/** @file
 * @brief Agent-side SMT solving: the cvc5 backend
 *
 * Drives cvc5 through its C++ API (@c cvc5/cvc5.h and the parser in
 * @c cvc5/cvc5_parser.h) - the library is linked and called in this
 * process, nothing is spawned. The only C++ translation unit in
 * ta_smt.
 *
 * It deliberately includes NO Test Environment headers: TE's C headers
 * are not C++-safe (they use @c new as an identifier and rely on
 * implicit void* casts), so this unit speaks a plain-C seam - @c char**
 * out-parameters filled with @c strdup'd strings and an @c int return -
 * and the C dispatcher in ta_smt.c converts to/from the TE types.
 *
 * Written against the cvc5 1.1.x C++ API as packaged on Debian/Ubuntu:
 * a default-constructed @c Solver (the @c TermManager and
 * @c Configuration of 1.2+ are not used), and the @c cvc5::parser
 * InputParser/SymbolManager. Model enumeration needs
 * @c SymbolManager::getDeclaredTerms(), which 1.1.x does not provide,
 * so a model is not returned here (sat/unsat/unknown and the unsat core
 * are); the engine-neutral layer and the suite tolerate an empty model
 * from an engine.
 */

#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>

#include <cvc5/cvc5.h>
#include <cvc5/cvc5_parser.h>

/* Status values match ta_smt.h: 0 sat, 1 unsat, 2 unknown. */
#define CVC5_SAT     0
#define CVC5_UNSAT   1
#define CVC5_UNKNOWN 2

/* Return values of the raw seam: 0 ok, 1 parse error, 2 engine failure. */
#define CVC5_OK      0
#define CVC5_EPARSE  1
#define CVC5_EFAIL   2

/** strdup() a std::string into an out-param, if the out-param is set. */
static void
set_out(char **out, const std::string &s)
{
    if (out != NULL)
        *out = strdup(s.c_str());
}

/**
 * Decide one SMT-LIB 2 problem with cvc5. Plain-C seam (see file
 * header); the C dispatcher wraps it. Out strings are heap-allocated
 * with strdup() and owned by the caller.
 */
extern "C" int
ta_smt_cvc5_solve_raw(const char *smtlib2, int model, int unsat_core,
                      int timeout_ms, unsigned int random_seed, int *status,
                      char **model_out, char **core_out,
                      char **version, char **reason)
{
    (void)model;        /* model enumeration is unavailable on cvc5 1.1.x */
    (void)model_out;
    (void)version;      /* cvc5 1.1.x exposes no public version string here */

    try
    {
        cvc5::Solver solver;
        std::string core_s;

        if (model)
            solver.setOption("produce-models", "true");
        if (unsat_core)
            solver.setOption("produce-unsat-cores", "true");
        if (timeout_ms > 0)
            solver.setOption("tlimit-per", std::to_string(timeout_ms));
        if (random_seed != 0)
            solver.setOption("seed", std::to_string(random_seed));

        /* The engine parses the SMT-LIB 2 problem and runs each command
         * against the solver, so the assertions land on its stack. */
        cvc5::parser::SymbolManager sm(&solver);
        cvc5::parser::InputParser parser(&solver, &sm);

        parser.setStringInput(cvc5::modes::InputLanguage::SMT_LIB_2_6,
                              smtlib2 != NULL ? smtlib2 : "", "tsf-smt");
        for (;;)
        {
            cvc5::parser::Command command = parser.nextCommand();
            std::stringstream sink;

            if (command.isNull())
                break;
            command.invoke(&solver, &sm, sink);
        }

        cvc5::Result result = solver.checkSat();

        if (result.isSat())
        {
            *status = CVC5_SAT;
            /* Model omitted: SymbolManager::getDeclaredTerms() is 1.2+. */
        }
        else if (result.isUnsat())
        {
            *status = CVC5_UNSAT;
            if (unsat_core)
            {
                for (const cvc5::Term &term : solver.getUnsatCore())
                    core_s += term.toString() + "\n";
                set_out(core_out, core_s);
            }
        }
        else
        {
            *status = CVC5_UNKNOWN;
            set_out(reason, result.toString());
        }

        return CVC5_OK;
    }
    catch (const cvc5::parser::ParserException &e)
    {
        set_out(reason, e.what());
        return CVC5_EPARSE;
    }
    catch (const cvc5::CVC5ApiException &e)
    {
        set_out(reason, e.what());
        return CVC5_EPARSE;
    }
    catch (const std::exception &e)
    {
        set_out(reason, e.what());
        return CVC5_EFAIL;
    }
}
