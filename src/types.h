#pragma once

#include <ctype.h>

#include "util.h"
#include "vector.h"
#include "hashtable.h"

typedef enum {
	//punctuation
	tok_lbrace, tok_rbrace, tok_lbrack, tok_rbrack, tok_lparen, tok_rparen,
	tok_comma, tok_end, tok_enddir, tok_star, tok_ternary, tok_colon, tok_ref, tok_ellipsis, tok_dot,
	//literals
	tok_access, tok_name, tok_compmacro, tok_str, tok_char, tok_num,
	//directives
	tok_include, tok_define, tok_ifdir, tok_ifdef, tok_elifdir, tok_elsedir, tok_endif, tok_dir,
	//types
	tok_typedef, tok_enum, tok_struct, tok_union, tok_static, tok_inline, tok_const,
	//flow
	tok_if, tok_else, tok_elseif, tok_do, tok_while, tok_for, tok_defer, tok_return,
	tok_switch, tok_break, tok_case, tok_default, tok_goto,
	//we dont do any complex static analysis and compile to C, pass all other valid tokens (eg. comparisons) through here
	tok_other,
	//any assignment operation goes through here
	tok_set,
	tok_unaryset,
	tok_eof
} token_ty;

static char* TOKEN_NAMES[tok_eof+1] = {
	"lbrace", "rbrace", "lbrack", "rbrack", "lparen", "rparen",
	"comma", "end", "enddef", "star", "ternary", "colon", "ref", "ellipsis", "dot",
	"access", "name", "builtin", "str", "char", "num",
	"include", "define", "ifdir", "ifdef", "elifdir", "elsedir", "endif", "dir",
	"typedef", "enum", "struct", "union", "static", "inline", "const",
	"if", "else", "elseif", "do", "while", "for", "defer", "return",
	"switch", "case", "default", "goto",
	"other",
	"set",
	"unary set",
	"eof"
};

typedef struct {
	token_ty ty;
	char* t;
	unsigned start;
	unsigned len;

	unsigned strstart, strlen;
} token_t;

//not an ast tree; these items are used to tag groups of tokens/items
typedef enum {
	item_expr,
	item_defer,
	item_ret,
	item_op,
	item_ternary,
	item_dot,
	item_access,
	item_literal_str,
	item_literal_char,
	item_literal_num,
	item_initializer,
	item_cast,
	item_initvar,
	item_initi,
	item_func,
	item_var, //uber, expr/initializer
	item_varset, //ty, var, var, ...
	item_assignment,
	item_while,
	item_dowhile,
	item_for,
	item_if,
	item_elseif,
	item_else,
	item_switch,
	item_case,
	item_break,
	item_typedef,
	item_enum,
	item_enumi,
	item_struct,
	item_union,
	item_field,
	item_type,
	item_typemod, //pointers/const
	item_define,
	item_include,
	item_arg,
	item_args,
	item_name,
	item_uber, //used as lvalues or name after type (eg. in functions)
	item_array,
	item_fnptr,
	item_fncall,
	item_body,
	item_block,
	item_ifdir,
	item_ifdef,
	item_elifdir,
	item_elsedir,
	item_dir, //other directive
	item_macrocall,
	item_macroarg,
	item_macroeof,
	item_goto,
	item_label,
	item_length
} item_ty;

static char* ITEM_NAMES[item_length] = {
	"item_expr", "item_defer", "item_ret", "item_op", "item_ternary", "item_dot", "item_access",
	"item_litstr", "item_litchar", "item_litnum", "item_initializer",
	"item_cast", "item_initvar", "item_initi", "item_func", "item_var", "item_varset",
	"item_assignment", "item_while", "item_dowhile", "item_for",
	"item_if", "item_elseif", "item_else", "item_switch", "item_case", "item_break",
	"item_typedef", "item_enum", "item_enumi", "item_struct", "item_union", "item_field",
	"item_type", "item_typemod", "item_define", "item_include",
	"item_arg", "item_args", "item_name", "item_uber", "item_array", "item_fnptr",
	"item_fncall", "item_body", "item_block", "item_ifdir", "item_ifdef",
	"item_elifdir", "item_elsedir", "item_dir",
	"item_macrocall", "item_macroarg", "item_macroeof", "item_goto", "item_label"
};

typedef struct {
	unsigned start, end;
} span_t;

struct item;

//break/return/goto
typedef struct {
	unsigned defer_i;
	struct item* item;
	struct item* exit_scope;
} exit_t;

typedef struct scope {
	unsigned name_i;
	vector_t deferred;
	vector_t exits;

	char ret; //function scope, handles returns
	char br; //loop/switch, breaks in (sub)scopes go here
} scope_t;

typedef struct {
	char* define_str;
	vector_t args;
} macro_t;

typedef struct {
	char* arg_str;
} arg_t;

typedef struct item {
	item_ty ty;

	vector_t body; //body of items, if ty permits
	unsigned if_stack, if_i;

	char gen;
	union {
		span_t span;
		char* str;
	};

	union {
		scope_t* scope;
		macro_t* macro;
		arg_t* arg;
	};

	struct item* parent;
} item_t;

typedef struct {
	unsigned tok_i, expansion_save, expansions_i;

	unsigned item_i;
	unsigned item_pool_i;
} parser_save_t;

typedef struct {
	char* err;
	span_t span;
	int stop;
} parser_error_t;

//comments are handled out-of-band but directives arent
typedef struct {
	item_t* item;
	parser_save_t save;
} parser_branch_t;

typedef struct {
	unsigned parent, parent_i;

	unsigned tok_i; //start parser->i, used to identify if
	unsigned i; //current/next branch during reparsing or emission
	vector_t branch;
} parser_if_t;

typedef struct {
	unsigned i; //i used during restoration
	char* t;
} parser_expansion_t;

typedef struct {
	char* t; //t is overwritten during expansions, tokens reference that string instead
	unsigned i, len;

	unsigned tok_i;

	//corresponding source "expansion"
	//separated from typical expansions to save time during parser_save
	char* source;
	unsigned source_i;

	//last definition of a macro
	//prior/conditional macros are not referenced
	map_t macros;

	vector_t expansions;
	unsigned expansions_i;
	vector_t expansion_stack; //indices to expansions
	vector_t expansion_save;

	vector_t item_pool; //stores refs to every item for free later

	vector_t tokens; //all tokens
	vector_t items;

	vector_t ifs;
	unsigned current_if;

	vector_cap_t stack; //parse_save_t
	vector_t errors;
	int stop;

	int in_define;
	int in_include;
	int parsed_if; //set after branching to allow syntatic exceptions
} parser_t;
