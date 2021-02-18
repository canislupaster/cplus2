// Automatically generated header.

#pragma once
#include <stdio.h>
#include <ctype.h>
#include "util.h"
#include "vector.h"
#include "hashtable.h"
#include "types.h"
span_t parser_current(parser_t* parser);
void parser_error(parser_t* parser, span_t span, char* err, int stop);
void parser_printerr(parser_t* parser, parser_error_t* perr);
void print_item(parser_t* parser, FILE* f, item_t* item);
char* item_str(parser_t* parser, item_t* item);
void print_item_tree_rec(parser_t* parser, vector_t* items, int depth);
void print_item_tree(parser_t* parser);
int parser_ncmp(parser_t* parser, char* x);
int skip_comment(parser_t* parser);
void parser_skip_ws(parser_t* parser);
void parser_reparse(parser_t* parser);
token_t parser_skip_define(parser_t* parser);
token_t parser_skip_arg(parser_t* parser);
void parse_string(parser_t* parser, token_t* tok);
int parse_num(parser_t* parser, token_t* tok);
int parser_name(parser_t* parser);
token_t parse_token_fallacious(parser_t* parser);
token_t parse_token(parser_t* parser);
int parser_peek(parser_t* parser, token_ty ty, unsigned off);
int parser_expect(parser_t* parser, token_ty ty, int err);
parser_save_t parser_save(parser_t* parser);
void parser_start(parser_t* parser);
int parser_expectstart(parser_t* parser, token_ty ty);
void item_free(item_t* item);
void parser_trunc_items(parser_t* parser, unsigned i);
void parser_macro_pop(parser_t* parser, unsigned len);
void parser_restore(parser_t* parser, parser_save_t* save);
void parser_cancel(parser_t* parser);
void parser_finish(parser_t* parser);
item_t* parser_wrap(parser_t* parser, item_ty ty, int oob);
item_t* parser_push(parser_t* parser, item_ty ty, int oob);
void parser_skip_branch(parser_t* parser);
void parser_push_ifdir(parser_t* parser, item_ty ty, int branch);
int parser_parse_if(parser_t* parser);
void parser_handle_macros(parser_t* parser);
void parser_handle_pp(parser_t* parser);
int parser_expect_pp(parser_t* parser, token_ty ty, int err);
int parser_expectstart_pp(parser_t* parser, token_ty ty);
int parser_peek_pp(parser_t* parser, token_ty ty, unsigned off);
void parse_args(parser_t* parser);
void parse_addendums(parser_t* parser);
int parse_aftertype(parser_t* parser, int named);
int parse_initializer(parser_t* parser);
int parse_op(parser_t* parser);
int parse_expr_left(parser_t* parser, int optional);
void parse_expr(parser_t* parser, int allow_comma, int optional);
int parse_ty(parser_t* parser, int named);
int parse_var(parser_t* parser);
void parse_stmt(parser_t* parser);
int parse_block(parser_t* parser);
int parse_decl(parser_t* parser);
parser_t parser_new(char* txt);
parser_t parse_file(char* filename);
void parser_free(parser_t* parser);
