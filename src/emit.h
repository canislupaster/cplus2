// Automatically generated header.

#pragma once
#include <stdio.h>
#include "syntax.h"
typedef struct {
	parser_t* parser;
	FILE* f;
	char* fname;
	unsigned line, tok;
	int space, excess_newline, newline; //last character (excess) space/line, do not emit another
	int gen; //last item gen

	//inside macro expansion; do not omit anything
	//in the unfortunate case the end user decides not to align their macros to generated tokens (eg. delimiters)
	//then our emitter will fuck-up due to its elegant design
	int macro;
	item_iterator_t iter;
} emitter_t;
int emit_next_macro(emitter_t* e);
int emit_next(emitter_t* e);
int emit_item_ty(emitter_t* e, item_ty ty);
#define LINE_SPEC e->newline ? "#line %u \"%s\"\n" : "\n#line %u \"%s\"\n"
void flush_whitespace(emitter_t* e, unsigned tok_i);
void emits(emitter_t* e, char* s);
void emit_align_item(emitter_t* e);
void switch_branch(emitter_t* e, parser_if_t* p_if, unsigned from, unsigned to);
int emit_item_next(emitter_t* e);
void emit_sep_items(emitter_t* e, char* sep);
int emit_search_for_macroeof(emitter_t* e);
void emit_item(emitter_t* e);
void emit(char* fname, FILE* f, parser_t* parser);
