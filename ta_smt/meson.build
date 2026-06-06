# SPDX-License-Identifier: MIT
# Copyright (C) 2026 Interpretica, Unipessoal Lda. All rights reserved.
#
# External TE library (see TE_EXT_REPO): agent-side SMT solving over the
# solvers' own library APIs - Z3's C API (z3.h, -lz3) and cvc5's C++ API
# (cvc5/cvc5.h, -lcvc5). The library is linked and called in-process;
# nothing is spawned. The agent and its RPC server both link it.
#
# One translation unit is C++ (ta_smt_cvc5.cpp, the cvc5 backend), so
# the build of this library needs a C++ compiler. Everything else is C.

headers += files(
    'ta_smt.h',
)

sources += files(
    'ta_smt.c',
    'ta_smt_cvc5.cpp',
)

te_libs += [
    'logger_core',
    'tools',
]

# Z3 ships no pkg-config on many distributions, so fall back to finding
# the library and its header directly. cvc5 ships cvc5.pc from 1.1.
cc = meson.get_compiler('c')
cxx = meson.get_compiler('cpp')

z3_dep = dependency('z3', required: false)
if not z3_dep.found()
    z3_dep = cc.find_library('z3', has_headers: 'z3.h')
endif

cvc5_dep = dependency('cvc5', required: false)
if not cvc5_dep.found()
    cvc5_dep = cxx.find_library('cvc5', has_headers: 'cvc5/cvc5.h')
endif

deps += [
    z3_dep,
    cvc5_dep,
]

# cvc5's SMT-LIB parser (InputParser/SymbolManager) is a separate shared
# library in most builds; where it is folded into libcvc5, this find
# simply contributes nothing.
cvc5_parser_dep = dependency('cvc5parser', required: false)
if not cvc5_parser_dep.found()
    cvc5_parser_dep = cxx.find_library('cvc5parser', required: false)
endif
if cvc5_parser_dep.found()
    deps += [ cvc5_parser_dep ]
endif
