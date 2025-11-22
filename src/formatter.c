#include "formatter.h"
#include "ast.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Helper to create indentation string (using tabs!)
static char* make_indent(int level) {
	char *indent = malloc(level + 1);
	for (int i = 0; i < level; i++) {
		indent[i] = '\t';
	}
	indent[level] = '\0';
	return indent;
}

// Helper to append strings with reallocation
static char* str_append(char *dest, const char *src) {
	if (dest == NULL) {
		return strdup(src);
	}
	size_t dest_len = strlen(dest);
	size_t src_len = strlen(src);
	dest = realloc(dest, dest_len + src_len + 1);
	strcpy(dest + dest_len, src);
	return dest;
}

// Format binary operator
static const char* format_binop(BinaryOp op) {
	switch (op) {
		case OP_ADD: return "+";
		case OP_SUB: return "-";
		case OP_MUL: return "*";
		case OP_DIV: return "/";
		case OP_MOD: return "%";
		case OP_EQUAL: return "==";
		case OP_NOT_EQUAL: return "!=";
		case OP_LESS: return "<";
		case OP_LESS_EQUAL: return "<=";
		case OP_GREATER: return ">";
		case OP_GREATER_EQUAL: return ">=";
		case OP_AND: return "&&";
		case OP_OR: return "||";
		case OP_BIT_AND: return "&";
		case OP_BIT_OR: return "|";
		case OP_BIT_XOR: return "^";
		case OP_BIT_LSHIFT: return "<<";
		case OP_BIT_RSHIFT: return ">>";
		default: return "?";
	}
}

// Format unary operator
static const char* format_unaryop(UnaryOp op) {
	switch (op) {
		case UNARY_NOT: return "!";
		case UNARY_NEGATE: return "-";
		case UNARY_BIT_NOT: return "~";
		default: return "?";
	}
}

// Format type annotation
char* format_type(Type *type) {
	if (type == NULL) {
		return strdup("");
	}

	char buffer[256];
	switch (type->kind) {
		case TYPE_I8: return strdup("i8");
		case TYPE_I16: return strdup("i16");
		case TYPE_I32: return strdup("i32");
		case TYPE_I64: return strdup("i64");
		case TYPE_U8: return strdup("u8");
		case TYPE_U16: return strdup("u16");
		case TYPE_U32: return strdup("u32");
		case TYPE_U64: return strdup("u64");
		case TYPE_F32: return strdup("f32");
		case TYPE_F64: return strdup("f64");
		case TYPE_BOOL: return strdup("bool");
		case TYPE_STRING: return strdup("string");
		case TYPE_RUNE: return strdup("rune");
		case TYPE_PTR: return strdup("ptr");
		case TYPE_BUFFER: return strdup("buffer");
		case TYPE_NULL: return strdup("null");
		case TYPE_VOID: return strdup("void");
		case TYPE_GENERIC_OBJECT: return strdup("object");
		case TYPE_ARRAY: {
			char *elem_type = format_type(type->element_type);
			snprintf(buffer, sizeof(buffer), "array<%s>", elem_type);
			free(elem_type);
			return strdup(buffer);
		}
		case TYPE_CUSTOM_OBJECT:
			return strdup(type->type_name ? type->type_name : "object");
		case TYPE_INFER:
			return strdup("");
		default:
			return strdup("unknown");
	}
}

// Forward declaration
char* format_statement(Stmt *stmt, int indent);

// Format expression
char* format_expression(Expr *expr) {
	if (expr == NULL) {
		return strdup("null");
	}

	char buffer[1024];
	char *result = NULL;

	switch (expr->type) {
		case EXPR_NUMBER:
			if (expr->as.number.is_float) {
				snprintf(buffer, sizeof(buffer), "%g", expr->as.number.float_value);
			} else {
				snprintf(buffer, sizeof(buffer), "%lld", (long long)expr->as.number.int_value);
			}
			return strdup(buffer);

		case EXPR_BOOL:
			return strdup(expr->as.boolean ? "true" : "false");

		case EXPR_STRING: {
			// Escape special characters in strings
			const char *s = expr->as.string;
			size_t len = strlen(s);
			char *escaped = malloc(len * 2 + 3); // Worst case: all chars escaped + quotes + null
			char *p = escaped;
			*p++ = '"';
			for (size_t i = 0; i < len; i++) {
				switch (s[i]) {
					case '\n': *p++ = '\\'; *p++ = 'n'; break;
					case '\t': *p++ = '\\'; *p++ = 't'; break;
					case '\r': *p++ = '\\'; *p++ = 'r'; break;
					case '\\': *p++ = '\\'; *p++ = '\\'; break;
					case '"': *p++ = '\\'; *p++ = '"'; break;
					case '\0': *p++ = '\\'; *p++ = '0'; break;
					default: *p++ = s[i]; break;
				}
			}
			*p++ = '"';
			*p = '\0';
			return escaped;
		}

		case EXPR_RUNE: {
			// Format rune literal
			uint32_t codepoint = expr->as.rune;
			if (codepoint == '\n') {
				return strdup("'\\n'");
			} else if (codepoint == '\t') {
				return strdup("'\\t'");
			} else if (codepoint == '\r') {
				return strdup("'\\r'");
			} else if (codepoint == '\\') {
				return strdup("'\\\\'");
			} else if (codepoint == '\'') {
				return strdup("'\\''");
			} else if (codepoint == '\0') {
				return strdup("'\\0'");
			} else if (codepoint >= 32 && codepoint <= 126) {
				snprintf(buffer, sizeof(buffer), "'%c'", (char)codepoint);
				return strdup(buffer);
			} else {
				snprintf(buffer, sizeof(buffer), "'\\u{%X}'", codepoint);
				return strdup(buffer);
			}
		}

		case EXPR_IDENT:
			return strdup(expr->as.ident);

		case EXPR_NULL:
			return strdup("null");

		case EXPR_BINARY: {
			char *left = format_expression(expr->as.binary.left);
			char *right = format_expression(expr->as.binary.right);
			const char *op = format_binop(expr->as.binary.op);
			snprintf(buffer, sizeof(buffer), "%s %s %s", left, op, right);
			free(left);
			free(right);
			return strdup(buffer);
		}

		case EXPR_UNARY: {
			char *operand = format_expression(expr->as.unary.operand);
			const char *op = format_unaryop(expr->as.unary.op);
			snprintf(buffer, sizeof(buffer), "%s%s", op, operand);
			free(operand);
			return strdup(buffer);
		}

		case EXPR_TERNARY: {
			char *cond = format_expression(expr->as.ternary.condition);
			char *true_expr = format_expression(expr->as.ternary.true_expr);
			char *false_expr = format_expression(expr->as.ternary.false_expr);
			result = NULL;
			result = str_append(result, cond);
			result = str_append(result, " ? ");
			result = str_append(result, true_expr);
			result = str_append(result, " : ");
			result = str_append(result, false_expr);
			free(cond);
			free(true_expr);
			free(false_expr);
			return result;
		}

		case EXPR_CALL: {
			char *func = format_expression(expr->as.call.func);
			result = str_append(NULL, func);
			result = str_append(result, "(");
			for (int i = 0; i < expr->as.call.num_args; i++) {
				if (i > 0) result = str_append(result, ", ");
				char *arg = format_expression(expr->as.call.args[i]);
				result = str_append(result, arg);
				free(arg);
			}
			result = str_append(result, ")");
			free(func);
			return result;
		}

		case EXPR_ASSIGN: {
			char *value = format_expression(expr->as.assign.value);
			result = NULL;
			result = str_append(result, expr->as.assign.name);
			result = str_append(result, " = ");
			result = str_append(result, value);
			free(value);
			return result;
		}

		case EXPR_GET_PROPERTY: {
			char *obj = format_expression(expr->as.get_property.object);
			result = NULL;
			result = str_append(result, obj);
			result = str_append(result, ".");
			result = str_append(result, expr->as.get_property.property);
			free(obj);
			return result;
		}

		case EXPR_SET_PROPERTY: {
			char *obj = format_expression(expr->as.set_property.object);
			char *val = format_expression(expr->as.set_property.value);
			result = NULL;
			result = str_append(result, obj);
			result = str_append(result, ".");
			result = str_append(result, expr->as.set_property.property);
			result = str_append(result, " = ");
			result = str_append(result, val);
			free(obj);
			free(val);
			return result;
		}

		case EXPR_INDEX: {
			char *obj = format_expression(expr->as.index.object);
			char *idx = format_expression(expr->as.index.index);
			result = NULL;
			result = str_append(result, obj);
			result = str_append(result, "[");
			result = str_append(result, idx);
			result = str_append(result, "]");
			free(obj);
			free(idx);
			return result;
		}

		case EXPR_INDEX_ASSIGN: {
			char *obj = format_expression(expr->as.index_assign.object);
			char *idx = format_expression(expr->as.index_assign.index);
			char *val = format_expression(expr->as.index_assign.value);
			result = NULL;
			result = str_append(result, obj);
			result = str_append(result, "[");
			result = str_append(result, idx);
			result = str_append(result, "] = ");
			result = str_append(result, val);
			free(obj);
			free(idx);
			free(val);
			return result;
		}

		case EXPR_FUNCTION: {
			result = NULL;
			if (expr->as.function.is_async) {
				result = str_append(result, "async ");
			}
			result = str_append(result, "fn(");
			for (int i = 0; i < expr->as.function.num_params; i++) {
				if (i > 0) result = str_append(result, ", ");
				result = str_append(result, expr->as.function.param_names[i]);
				if (expr->as.function.param_types[i] != NULL) {
					result = str_append(result, ": ");
					char *type_str = format_type(expr->as.function.param_types[i]);
					result = str_append(result, type_str);
					free(type_str);
				}
			}
			result = str_append(result, ")");
			if (expr->as.function.return_type != NULL) {
				result = str_append(result, ": ");
				char *ret_type = format_type(expr->as.function.return_type);
				result = str_append(result, ret_type);
				free(ret_type);
			}
			result = str_append(result, " ");
			char *body = format_statement(expr->as.function.body, 0);
			// Remove trailing newline from function body
			if (body != NULL) {
				size_t len = strlen(body);
				if (len > 0 && body[len-1] == '\n') {
					body[len-1] = '\0';
				}
			}
			result = str_append(result, body);
			free(body);
			return result;
		}

		case EXPR_ARRAY_LITERAL: {
			result = str_append(NULL, "[");
			for (int i = 0; i < expr->as.array_literal.num_elements; i++) {
				if (i > 0) result = str_append(result, ", ");
				char *elem = format_expression(expr->as.array_literal.elements[i]);
				result = str_append(result, elem);
				free(elem);
			}
			result = str_append(result, "]");
			return result;
		}

		case EXPR_OBJECT_LITERAL: {
			result = str_append(NULL, "{ ");
			for (int i = 0; i < expr->as.object_literal.num_fields; i++) {
				if (i > 0) result = str_append(result, ", ");
				result = str_append(result, expr->as.object_literal.field_names[i]);
				result = str_append(result, ": ");
				char *val = format_expression(expr->as.object_literal.field_values[i]);
				result = str_append(result, val);
				free(val);
			}
			result = str_append(result, " }");
			return result;
		}

		case EXPR_PREFIX_INC: {
			char *operand = format_expression(expr->as.prefix_inc.operand);
			result = NULL;
			result = str_append(result, "++");
			result = str_append(result, operand);
			free(operand);
			return result;
		}

		case EXPR_PREFIX_DEC: {
			char *operand = format_expression(expr->as.prefix_dec.operand);
			result = NULL;
			result = str_append(result, "--");
			result = str_append(result, operand);
			free(operand);
			return result;
		}

		case EXPR_POSTFIX_INC: {
			char *operand = format_expression(expr->as.postfix_inc.operand);
			result = NULL;
			result = str_append(result, operand);
			result = str_append(result, "++");
			free(operand);
			return result;
		}

		case EXPR_POSTFIX_DEC: {
			char *operand = format_expression(expr->as.postfix_dec.operand);
			result = NULL;
			result = str_append(result, operand);
			result = str_append(result, "--");
			free(operand);
			return result;
		}

		case EXPR_AWAIT: {
			char *awaited = format_expression(expr->as.await_expr.awaited_expr);
			result = NULL;
			result = str_append(result, "await ");
			result = str_append(result, awaited);
			free(awaited);
			return result;
		}

		default:
			return strdup("<unknown-expr>");
	}
}

// Format statement
char* format_statement(Stmt *stmt, int indent) {
	if (stmt == NULL) {
		return strdup("");
	}

	char *result = NULL;
	char *indent_str = make_indent(indent);

	switch (stmt->type) {
		case STMT_LET: {
			result = str_append(NULL, indent_str);
			result = str_append(result, "let ");
			result = str_append(result, stmt->as.let.name);
			if (stmt->as.let.type_annotation != NULL) {
				result = str_append(result, ": ");
				char *type_str = format_type(stmt->as.let.type_annotation);
				result = str_append(result, type_str);
				free(type_str);
			}
			if (stmt->as.let.value != NULL) {
				result = str_append(result, " = ");
				char *value = format_expression(stmt->as.let.value);
				result = str_append(result, value);
				free(value);
			}
			result = str_append(result, ";\n");
			break;
		}

		case STMT_CONST: {
			result = str_append(NULL, indent_str);
			result = str_append(result, "const ");
			result = str_append(result, stmt->as.const_stmt.name);
			if (stmt->as.const_stmt.type_annotation != NULL) {
				result = str_append(result, ": ");
				char *type_str = format_type(stmt->as.const_stmt.type_annotation);
				result = str_append(result, type_str);
				free(type_str);
			}
			result = str_append(result, " = ");
			char *value = format_expression(stmt->as.const_stmt.value);
			result = str_append(result, value);
			result = str_append(result, ";\n");
			free(value);
			break;
		}

		case STMT_EXPR: {
			result = str_append(NULL, indent_str);
			char *expr = format_expression(stmt->as.expr);
			result = str_append(result, expr);
			result = str_append(result, ";\n");
			free(expr);
			break;
		}

		case STMT_IF: {
			result = str_append(NULL, indent_str);
			result = str_append(result, "if (");
			char *cond = format_expression(stmt->as.if_stmt.condition);
			result = str_append(result, cond);
			result = str_append(result, ") ");
			free(cond);

			char *then_branch = format_statement(stmt->as.if_stmt.then_branch, indent);
			// Remove trailing newline if there's an else clause
			if (stmt->as.if_stmt.else_branch != NULL && then_branch != NULL) {
				size_t len = strlen(then_branch);
				if (len > 0 && then_branch[len-1] == '\n') {
					then_branch[len-1] = '\0';
				}
			}
			result = str_append(result, then_branch);
			free(then_branch);

			if (stmt->as.if_stmt.else_branch != NULL) {
				result = str_append(result, " else ");
				char *else_branch = format_statement(stmt->as.if_stmt.else_branch, indent);
				result = str_append(result, else_branch);
				free(else_branch);
			}
			break;
		}

		case STMT_WHILE: {
			result = str_append(NULL, indent_str);
			result = str_append(result, "while (");
			char *cond = format_expression(stmt->as.while_stmt.condition);
			result = str_append(result, cond);
			result = str_append(result, ") ");
			free(cond);

			char *body = format_statement(stmt->as.while_stmt.body, indent);
			result = str_append(result, body);
			free(body);
			break;
		}

		case STMT_FOR: {
			result = str_append(NULL, indent_str);
			result = str_append(result, "for (");

			if (stmt->as.for_loop.initializer != NULL) {
				char *init = format_statement(stmt->as.for_loop.initializer, 0);
				// Remove trailing ";\n" from init
				size_t len = strlen(init);
				if (len > 2 && init[len-2] == ';' && init[len-1] == '\n') {
					init[len-2] = '\0';
				}
				result = str_append(result, init);
				free(init);
			}
			result = str_append(result, "; ");

			if (stmt->as.for_loop.condition != NULL) {
				char *cond = format_expression(stmt->as.for_loop.condition);
				result = str_append(result, cond);
				free(cond);
			}
			result = str_append(result, "; ");

			if (stmt->as.for_loop.increment != NULL) {
				char *inc = format_expression(stmt->as.for_loop.increment);
				result = str_append(result, inc);
				free(inc);
			}
			result = str_append(result, ") ");

			char *body = format_statement(stmt->as.for_loop.body, indent);
			result = str_append(result, body);
			free(body);
			break;
		}

		case STMT_FOR_IN: {
			result = str_append(NULL, indent_str);
			result = str_append(result, "for (");
			if (stmt->as.for_in.key_var != NULL) {
				result = str_append(result, stmt->as.for_in.key_var);
				result = str_append(result, ", ");
			}
			result = str_append(result, stmt->as.for_in.value_var);
			result = str_append(result, " in ");
			char *iterable = format_expression(stmt->as.for_in.iterable);
			result = str_append(result, iterable);
			result = str_append(result, ") ");
			free(iterable);

			char *body = format_statement(stmt->as.for_in.body, indent);
			result = str_append(result, body);
			free(body);
			break;
		}

		case STMT_BREAK:
			result = str_append(NULL, indent_str);
			result = str_append(result, "break;\n");
			break;

		case STMT_CONTINUE:
			result = str_append(NULL, indent_str);
			result = str_append(result, "continue;\n");
			break;

		case STMT_BLOCK: {
			result = str_append(NULL, "{\n");
			for (int i = 0; i < stmt->as.block.count; i++) {
				char *s = format_statement(stmt->as.block.statements[i], indent + 1);
				result = str_append(result, s);
				free(s);
			}
			result = str_append(result, indent_str);
			result = str_append(result, "}\n");
			break;
		}

		case STMT_RETURN: {
			result = str_append(NULL, indent_str);
			result = str_append(result, "return");
			if (stmt->as.return_stmt.value != NULL) {
				result = str_append(result, " ");
				char *value = format_expression(stmt->as.return_stmt.value);
				result = str_append(result, value);
				free(value);
			}
			result = str_append(result, ";\n");
			break;
		}

		case STMT_DEFINE_OBJECT: {
			result = str_append(NULL, indent_str);
			result = str_append(result, "define ");
			result = str_append(result, stmt->as.define_object.name);
			result = str_append(result, " {\n");

			for (int i = 0; i < stmt->as.define_object.num_fields; i++) {
				char *field_indent = make_indent(indent + 1);
				result = str_append(result, field_indent);
				result = str_append(result, stmt->as.define_object.field_names[i]);

				if (stmt->as.define_object.field_optional[i]) {
					result = str_append(result, "?");
				}

				if (stmt->as.define_object.field_types[i] != NULL) {
					result = str_append(result, ": ");
					char *type_str = format_type(stmt->as.define_object.field_types[i]);
					result = str_append(result, type_str);
					free(type_str);
				}

				if (stmt->as.define_object.field_defaults[i] != NULL) {
					result = str_append(result, ": ");
					char *default_val = format_expression(stmt->as.define_object.field_defaults[i]);
					result = str_append(result, default_val);
					free(default_val);
				}

				result = str_append(result, ",\n");
				free(field_indent);
			}

			result = str_append(result, indent_str);
			result = str_append(result, "}\n");
			break;
		}

		case STMT_TRY: {
			result = str_append(NULL, indent_str);
			result = str_append(result, "try ");
			char *try_block = format_statement(stmt->as.try_stmt.try_block, indent);
			// Remove trailing newline if there's a catch/finally
			if ((stmt->as.try_stmt.catch_block != NULL || stmt->as.try_stmt.finally_block != NULL) && try_block != NULL) {
				size_t len = strlen(try_block);
				if (len > 0 && try_block[len-1] == '\n') {
					try_block[len-1] = '\0';
				}
			}
			result = str_append(result, try_block);
			free(try_block);

			if (stmt->as.try_stmt.catch_block != NULL) {
				result = str_append(result, " catch (");
				result = str_append(result, stmt->as.try_stmt.catch_param);
				result = str_append(result, ") ");
				char *catch_block = format_statement(stmt->as.try_stmt.catch_block, indent);
				// Remove trailing newline if there's a finally
				if (stmt->as.try_stmt.finally_block != NULL && catch_block != NULL) {
					size_t len = strlen(catch_block);
					if (len > 0 && catch_block[len-1] == '\n') {
						catch_block[len-1] = '\0';
					}
				}
				result = str_append(result, catch_block);
				free(catch_block);
			}

			if (stmt->as.try_stmt.finally_block != NULL) {
				result = str_append(result, " finally ");
				char *finally_block = format_statement(stmt->as.try_stmt.finally_block, indent);
				result = str_append(result, finally_block);
				free(finally_block);
			}
			break;
		}

		case STMT_THROW: {
			result = str_append(NULL, indent_str);
			result = str_append(result, "throw ");
			char *value = format_expression(stmt->as.throw_stmt.value);
			result = str_append(result, value);
			result = str_append(result, ";\n");
			free(value);
			break;
		}

		case STMT_SWITCH: {
			result = str_append(NULL, indent_str);
			result = str_append(result, "switch (");
			char *expr = format_expression(stmt->as.switch_stmt.expr);
			result = str_append(result, expr);
			result = str_append(result, ") {\n");
			free(expr);

			for (int i = 0; i < stmt->as.switch_stmt.num_cases; i++) {
				char *case_indent = make_indent(indent + 1);
				result = str_append(result, case_indent);

				if (stmt->as.switch_stmt.case_values[i] == NULL) {
					result = str_append(result, "default:\n");
				} else {
					result = str_append(result, "case ");
					char *case_val = format_expression(stmt->as.switch_stmt.case_values[i]);
					result = str_append(result, case_val);
					result = str_append(result, ":\n");
					free(case_val);
				}

				// Format case body - if it's a block, inline it, otherwise indent it
				if (stmt->as.switch_stmt.case_bodies[i]->type == STMT_BLOCK) {
					// Inline block statements
					Stmt *block = stmt->as.switch_stmt.case_bodies[i];
					for (int j = 0; j < block->as.block.count; j++) {
						char *s = format_statement(block->as.block.statements[j], indent + 2);
						result = str_append(result, s);
						free(s);
					}
				} else {
					char *case_body = format_statement(stmt->as.switch_stmt.case_bodies[i], indent + 2);
					result = str_append(result, case_body);
					free(case_body);
				}
				free(case_indent);
			}

			result = str_append(result, indent_str);
			result = str_append(result, "}\n");
			break;
		}

		case STMT_DEFER: {
			result = str_append(NULL, indent_str);
			result = str_append(result, "defer ");
			char *call = format_expression(stmt->as.defer_stmt.call);
			result = str_append(result, call);
			result = str_append(result, ";\n");
			free(call);
			break;
		}

		case STMT_IMPORT: {
			result = str_append(NULL, indent_str);
			result = str_append(result, "import ");

			if (stmt->as.import_stmt.is_namespace) {
				result = str_append(result, "* as ");
				result = str_append(result, stmt->as.import_stmt.namespace_name);
			} else {
				result = str_append(result, "{ ");
				for (int i = 0; i < stmt->as.import_stmt.num_imports; i++) {
					if (i > 0) result = str_append(result, ", ");
					result = str_append(result, stmt->as.import_stmt.import_names[i]);
					if (stmt->as.import_stmt.import_aliases[i] != NULL) {
						result = str_append(result, " as ");
						result = str_append(result, stmt->as.import_stmt.import_aliases[i]);
					}
				}
				result = str_append(result, " }");
			}

			result = str_append(result, " from \"");
			result = str_append(result, stmt->as.import_stmt.module_path);
			result = str_append(result, "\";\n");
			break;
		}

		case STMT_IMPORT_FFI: {
			result = str_append(NULL, indent_str);
			result = str_append(result, "import_ffi \"");
			result = str_append(result, stmt->as.import_ffi.library_path);
			result = str_append(result, "\";\n");
			break;
		}

		case STMT_EXTERN_FN: {
			result = str_append(NULL, indent_str);
			result = str_append(result, "extern fn ");
			result = str_append(result, stmt->as.extern_fn.function_name);
			result = str_append(result, "(");

			for (int i = 0; i < stmt->as.extern_fn.num_params; i++) {
				if (i > 0) result = str_append(result, ", ");
				char *param_type = format_type(stmt->as.extern_fn.param_types[i]);
				result = str_append(result, param_type);
				free(param_type);
			}

			result = str_append(result, ")");

			if (stmt->as.extern_fn.return_type != NULL) {
				result = str_append(result, ": ");
				char *ret_type = format_type(stmt->as.extern_fn.return_type);
				result = str_append(result, ret_type);
				free(ret_type);
			}

			result = str_append(result, ";\n");
			break;
		}

		default:
			result = str_append(NULL, indent_str);
			result = str_append(result, "<unknown-stmt>\n");
			break;
	}

	free(indent_str);
	return result;
}

// Format a list of statements
char* format_statements(Stmt **statements, int count) {
	char *result = NULL;

	for (int i = 0; i < count; i++) {
		char *stmt_str = format_statement(statements[i], 0);
		result = str_append(result, stmt_str);
		free(stmt_str);
	}

	return result ? result : strdup("");
}

// Check if two files have the same content
int files_are_equal(const char *file1, const char *file2) {
	FILE *f1 = fopen(file1, "r");
	FILE *f2 = fopen(file2, "r");

	if (f1 == NULL || f2 == NULL) {
		if (f1) fclose(f1);
		if (f2) fclose(f2);
		return 0;
	}

	int equal = 1;
	int c1, c2;

	while (equal && (c1 = fgetc(f1)) != EOF) {
		c2 = fgetc(f2);
		if (c1 != c2) {
			equal = 0;
		}
	}

	if (equal && fgetc(f2) != EOF) {
		equal = 0;
	}

	fclose(f1);
	fclose(f2);
	return equal;
}
