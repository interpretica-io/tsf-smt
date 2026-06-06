/* SPDX-License-Identifier: MIT */
/* Copyright (C) 2026 Interpretica, Unipessoal Lda. All rights reserved. */
/** @file
 * @brief Agent-side SMT solving: the cvc5 backend
 *
 * Drives cvc5 through its C++ API (@c cvc5/cvc5.h and the parser in
 * @c cvc5/cvc5_parser.h) - the library is linked and called in this
 * process, nothing is spawned. The only C++ translation unit in
 * ta_smt; its one entry point, ta_smt_cvc5_solve(), is @c extern @c "C"
 * so the C dispatcher in ta_smt.c calls it directly.
 *
 * cvc5's C++ API has moved between releases (the TermManager arrived in
 * 1.1, the parser namespace settled around 1.2); this is written to
 * the 1.2+ API. A build against an older cvc5 is the place to expect an
 * adjustment - see the agent host requirements in the README.
 */

#include <sstream>
#include <string>

#include <cvc5/cvc5.h>
#include <cvc5/cvc5_parser.h>

extern "C" {
#include "te_defs.h"
#include "te_errno.h"
#include "te_string.h"

#include "ta_smt.h"
}

/* See description in ta_smt.h */
extern "C" te_errno
ta_smt_cvc5_solve(const char *smtlib2, te_bool model, te_bool unsat_core,
                  int timeout_ms, unsigned int random_seed, int *status,
                  te_string *model_out, te_string *core_out,
                  te_string *version, te_string *reason)
{
    try
    {
        cvc5::TermManager tm;
        cvc5::Solver solver(tm);

        if (version != NULL)
        {
            te_string_append(version, "%s",
                cvc5::Configuration::getVersionString().c_str());
        }

        /* Options go in before the problem is parsed; produce-models
         * and produce-unsat-cores must precede the first assertion. */
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
        cvc5::parser::SymbolManager sm(tm);
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
            *status = TA_SMT_SAT;
            if (model && model_out != NULL)
            {
                for (const cvc5::Term &term : sm.getDeclaredTerms())
                {
                    try
                    {
                        std::string name = term.hasSymbol() ?
                                           term.getSymbol() : term.toString();
                        std::string value = solver.getValue(term).toString();

                        te_string_append(model_out, "%s\t%s\n",
                                         name.c_str(), value.c_str());
                    }
                    catch (const std::exception &)
                    {
                        /* A declared symbol with no value (a sort, an
                         * uninterpreted function) is skipped. */
                    }
                }
            }
        }
        else if (result.isUnsat())
        {
            *status = TA_SMT_UNSAT;
            if (unsat_core && core_out != NULL)
            {
                for (const cvc5::Term &term : solver.getUnsatCore())
                    te_string_append(core_out, "%s\n", term.toString().c_str());
            }
        }
        else
        {
            *status = TA_SMT_UNKNOWN;
            if (reason != NULL)
            {
                te_string_append(reason, "%s",
                    result.getUnknownExplanation() == cvc5::UnknownExplanation::UNKNOWN_REASON ?
                    "unknown" : result.toString().c_str());
            }
        }

        return 0;
    }
    catch (const cvc5::parser::ParserException &e)
    {
        if (reason != NULL)
            te_string_append(reason, "%s", e.what());
        return TE_RC(TE_TA_UNIX, TE_ESHCMD);
    }
    catch (const cvc5::CVC5ApiException &e)
    {
        if (reason != NULL)
            te_string_append(reason, "%s", e.what());
        return TE_RC(TE_TA_UNIX, TE_ESHCMD);
    }
    catch (const std::exception &e)
    {
        if (reason != NULL)
            te_string_append(reason, "%s", e.what());
        return TE_RC(TE_TA_UNIX, TE_EFAIL);
    }
}
