#!/bin/sh
# usage: run_binop.sh OP  -> runs int_binop_spec for one operator
R=../..
exec cbmc harnesses/int_binop_spec.c $R/runtime/src/builtins_ops.c $R/runtime/src/value.c \
  $R/src/shared/type_promotion.c stubs/runtime_errors.c \
  -I$R/runtime/include -I$R/src/shared -I$R/include -I$R/runtime/src -Iharnesses \
  -DOP=$1 --function harness --div-by-zero-check --undefined-shift-check \
  --signed-overflow-check --unwind 2 --trace
