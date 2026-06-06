# tsf-smt

Proving things with an SMT solver from a Test Agent, packaged as an
external Test Environment (TE) repository (consumed with the
`TE_EXT_REPO` builder directive). It drives the solver over its own
**C/C++ library API — no Python, nothing spawned** — for both decision
procedures and proof by refutation.

Three libraries:

- `ta_smt` — agent side. SMT solving over **Z3's C API** (`z3.h`,
  `-lz3`) and **cvc5's C++ API** (`cvc5/cvc5.h`, `-lcvc5`): the library
  is linked into the agent and called in-process. One entry point,
  `ta_smt_solve()`, dispatches to the Z3 backend (`ta_smt.c`, C) or the
  cvc5 backend (`ta_smt_cvc5.cpp`, C++). The agent and its RPC server
  both link it.
- `rpcs_smt` — the `smt_*` RPCs for the RPC server of the agent, thin
  wrappers over `ta_smt`. The solving happens on the agent, in the RPC
  server process.
- `tapi_smt` — engine side. `tapi_smt.h` gives a test `tapi_smt_check()`
  (decide a set of assertions) and `tapi_smt_prove()` (prove a
  conjecture by refutation), with a model on `sat` and an unsat core on
  `unsat`; `tapi_smt_rpc.h` is the one-per-RPC layer beneath it.

TE has nothing for deciding a logical formula.

## Two engines, through one API

| Engine | `tapi_smt_engine` | Linked as | API used |
|---|---|---|---|
| Z3 | `TAPI_SMT_Z3` | `-lz3` | `Z3_solver_*` (C) |
| cvc5 | `TAPI_SMT_CVC5` | `-lcvc5` | `cvc5::Solver` + parser (C++) |

The two engines read the same SMT-LIB 2 and decide the same logics but
are not the same library and do not report everything alike (the unsat
core is a case in point — see below). A test picks the engine; the rest
of the call is identical.

```c
tapi_smt_result result;

if (!tapi_smt_available(rpcs, TAPI_SMT_Z3))
    TEST_SKIP("The agent has no Z3");

/* Does x > 2 follow from x > 3, over the integers? (It does.) */
CHECK_RC(tapi_smt_prove(rpcs, TAPI_SMT_Z3,
                        "(set-logic QF_LIA)\n"
                        "(declare-const x Int)\n"
                        "(assert (> x 3))\n",
                        "(> x 2)", NULL, &result));
if (result.status != TAPI_SMT_UNSAT)
    TEST_FAIL("Expected the conjecture to be proved");
tapi_smt_result_free(&result);
```

## Two questions: check and prove

- `tapi_smt_check()` decides a set of assertions as they stand — *is
  there an assignment that satisfies all of these?* The answer is `sat`
  (with a model, when asked), `unsat` (with an unsat core, when asked)
  or `unknown`.
- `tapi_smt_prove()` asks the question a test usually wants — *does this
  conjecture follow from these assumptions?* It asserts the assumptions
  and the **negation** of the conjecture and expects `unsat`, the
  standard refutation proof. An `unsat` is the conjecture proved; a
  `sat` is a counter-example, and the model is the assignment that
  breaks it:

```c
/* Prove x > 5 from x > 3 — which is false. result.status is SAT,
 * and the model shows a witness, e.g. x = 4. */
tapi_smt_opts opts = { .produce_model = true };
tapi_smt_prove(rpcs, TAPI_SMT_Z3, "(declare-const x Int)(assert (> x 3))",
               "(> x 5)", &opts, &result);
RING("counter-example: x = %s", tapi_smt_get(&result, "x"));
```

## The solver is a linked library, not a program

`ta_smt` does not run the `z3` or `cvc5` command and read its printout.
It links the solver and calls its API in the agent's RPC server process:
the assertions are parsed by the engine (`Z3_solver_from_string`,
cvc5's `InputParser`), the check is a call (`Z3_solver_check`,
`Solver::checkSat`), and the model and unsat core are read back as the
engine's own objects (`Z3_model_*` / `Solver::getValue`,
`Z3_solver_get_unsat_core` / `Solver::getUnsatCore`) — never scraped
from text. Across the RPC, a model comes back as `name<TAB>value` lines
and a core as one entry per line, which the engine side parses into
`tapi_smt_binding`s and strings.

Nothing of the problem leaves the agent: the solver is local, so a
problem (which may encode something a suite would rather not send
anywhere) stays there. There is no provider, no key, no network —
unlike tsf-ai.

## Models and unsat cores

A `sat` result carries a model when `produce_model` is set — one
`tapi_smt_binding` (a name and its value, printed the way the engine
prints it) per declared symbol; `tapi_smt_get()` looks one up by name.

An `unsat` result carries an unsat core when `produce_unsat_core` is
set. **Name the assertions you want in it** —
`(assert (! phi :named a))` — and the two engines differ in what they
hand back: Z3 returns the **names** (`a`, `b`), cvc5 returns the
**asserted terms** (`(> x 10)`, `(< x 0)`). Both identify the same
unsatisfiable subset; a test that compares exact strings should know
which engine it is talking to. (Z3 also needs
`(set-option :produce-unsat-cores true)` in the script, not just a
solver flag — `ta_smt` writes that in itself.)

## Agent host requirements

- **Z3** with its development headers and shared library — Debian:
  `apt install libz3-dev` (gives `z3.h` and `libz3.so`). The
  `Z3_solver_from_string` / `Z3_solver_get_unsat_core` API used here is
  long-standing (4.x).
- **cvc5** with its development headers and shared libraries — the C++
  API in `cvc5/cvc5.h` (`-lcvc5`) and the SMT-LIB parser in
  `cvc5/cvc5_parser.h`, which is a separate library in most builds
  (`-lcvc5parser`; `ta_smt`'s meson links it when present). Written to
  the **1.2+** API (the `TermManager`, the `cvc5::parser` namespace);
  an older cvc5 is the place to expect an adjustment.
- A **C++ compiler** for the agent platform: the cvc5 backend
  (`ta_smt_cvc5.cpp`) is the one C++ unit in the repository.

## Usage

Declare the repository in an external libraries catalog and pass it to
`dispatcher.sh --external=<catalog.yml>`:

```yaml
repositories:
  - name: tsf_smt
    url: https://github.com/interpretica-io/tsf-smt.git
    ref: <tag>
    libs:
      - ta_smt
      - rpcs_smt
      - tapi_smt
```

In `builder.conf`, bind `tapi_smt` to the engine, list `ta_smt` and
`rpcs_smt` among the RPC server's libraries, and add the RPC
definitions to both platforms:

```
TE_EXT_REPO_USE([tsf_smt], [ta_smt rpcs_smt], [tapi_smt])

# The RPCs, on the engine and on the agent platform (see smt_rpc.x.m4):
TE_LIB_PARMS([rpcxdr], [${TE_HOST}], [],
             [--with-rpcdefs=tarpc_job.x.m4,../ta_smt/smt_rpc.x.m4])
TE_LIB_PARMS([rpcxdr], [${TE_TA_TYPE}], [],
             [--with-rpcdefs=tarpc_job.x.m4,../ta_smt/smt_rpc.x.m4])

# ta_smt and rpcs_smt in the RPC server application:
TE_TA_APP([ta_rpcprovider], ..., [ta_smt rpcs_smt ...], ...)
```

Then add `tapi_smt` to `te_libs` in the suite's `meson.build`. Requires
TE with `TE_EXT_REPO` support and an agent with an **RPC server**
(`ta_rpcprovider`). The RPC program number is **25** (20–24 are taken
by the other tsf agent RPCs); change it in `smt_rpc.x.m4` if it ever
collides.

## What was verified, and what was not

**The C/C++ library code was not compiled here.** This environment had
no TE toolchain and no Z3/cvc5 development headers, so `ta_smt.c`,
`ta_smt_cvc5.cpp`, `rpcs_smt.c`, `tapi_smt*.c` and the TE integration
(the three `meson.build`s, `smt_rpc.x.m4`, `TE_EXT_REPO` wiring) were
written to the tsf-upnp template but **not built or run**. The first
suite to build tsf-smt should expect the ordinary first-build fixes,
and in particular should pin the cvc5 API version (see above). There is
no companion `smt-ts` suite yet; it would be the next step.

**The solving behaviour it encodes was verified against the same
libraries, through their Python bindings** (Z3 4.16.0 and cvc5 1.4.1),
since those bindings call the very same `libz3`/`libcvc5` the C/C++
code links. Each case was driven and its result checked:

- **check / sat** — `x > 3 ∧ x < 5` over `QF_LIA` is `sat` with a model
  `x = 4`, on both engines.
- **prove / unsat** — asserting `x > 3` and `¬(x > 2)` is `unsat` (the
  conjecture `x > 2` proved), on both engines.
- **prove / counter-model** — asserting `x > 3` and `¬(x > 5)` is `sat`
  with the witness `x = 4`, on both engines.
- **unsat core** — two named contradictory assertions come back as a
  core of both: Z3 as the names `a`, `b`, cvc5 as the terms `(> x 10)`,
  `(< x 0)` (the difference documented above). The need for
  `(set-option :produce-unsat-cores true)` in the Z3 script — which
  `ta_smt_z3_solve()` prepends — was found this way.
- **parse error** — malformed SMT-LIB 2 maps to the `parse`/`TE_ESHCMD`
  path on both engines.

What that verification does **not** cover: the Z3 **C** API calls and
the cvc5 **C++** API calls exactly as written (the Python bindings are
the same libraries but not the same call sites), the RPC marshalling,
timeouts, and the random seed.

## Scope

- **The engine must be on the agent.** tsf-smt does not install Z3 or
  cvc5; it drives whichever one a test selects, and
  `tapi_smt_available()` skips cleanly when the engine is not linked in.
  (tsf-package can install one as a test's setup.)
- **An `unknown` is an answer, not an error.** A solver that times out
  or meets an undecidable fragment returns `TAPI_SMT_UNKNOWN`; the call
  still succeeds. A test decides what `unknown` means for it.
