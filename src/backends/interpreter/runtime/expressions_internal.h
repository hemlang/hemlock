#ifndef HEMLOCK_RUNTIME_EXPRESSIONS_INTERNAL_H
#define HEMLOCK_RUNTIME_EXPRESSIONS_INTERNAL_H

#include "internal.h"

// ========== Helper functions (defined in eval_operators.c) ==========
const char* get_value_type_name(Value val);
Value value_add_one(Value val, ExecutionContext *ctx);
Value value_sub_one(Value val, ExecutionContext *ctx);

// ========== Pattern matching (defined in eval_pattern_match.c) ==========
int type_matches_value(Type *type, Value value);
int match_pattern(Pattern *pattern, Value value, Environment *env, ExecutionContext *ctx);
Value eval_match_expr(Expr *expr, Environment *env, ExecutionContext *ctx);

// ========== Call expressions (defined in eval_calls.c) ==========
Value eval_call_expr(Expr *expr, Environment *env, ExecutionContext *ctx);

// ========== Collection expressions (defined in eval_collections.c) ==========
Value eval_array_literal_expr(Expr *expr, Environment *env, ExecutionContext *ctx);
Value eval_object_literal_expr(Expr *expr, Environment *env, ExecutionContext *ctx);
Value eval_index_expr(Expr *expr, Environment *env, ExecutionContext *ctx);
Value eval_index_assign_expr(Expr *expr, Environment *env, ExecutionContext *ctx);
Value eval_get_property_expr(Expr *expr, Environment *env, ExecutionContext *ctx);
Value eval_set_property_expr(Expr *expr, Environment *env, ExecutionContext *ctx);

// ========== Binary expressions (defined in binary_ops.c) ==========
Value eval_binary_expr(Expr *expr, Environment *env, ExecutionContext *ctx);

// ========== Operator expressions (defined in eval_operators.c) ==========
Value eval_unary_expr(Expr *expr, Environment *env, ExecutionContext *ctx);
Value eval_ternary_expr(Expr *expr, Environment *env, ExecutionContext *ctx);
Value eval_assign_expr(Expr *expr, Environment *env, ExecutionContext *ctx);
Value eval_prefix_inc_expr(Expr *expr, Environment *env, ExecutionContext *ctx);
Value eval_prefix_dec_expr(Expr *expr, Environment *env, ExecutionContext *ctx);
Value eval_postfix_inc_expr(Expr *expr, Environment *env, ExecutionContext *ctx);
Value eval_postfix_dec_expr(Expr *expr, Environment *env, ExecutionContext *ctx);
Value eval_null_coalesce_expr(Expr *expr, Environment *env, ExecutionContext *ctx);

#endif // HEMLOCK_RUNTIME_EXPRESSIONS_INTERNAL_H
