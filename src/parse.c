#include <stdio.h>
#include <ctype.h>

#include "util.h"
#include "vector.h"
#include "hashtable.h"

#include "types.h"

span_t parser_current(parser_t* parser) {
	return (span_t){.start=parser->tokens.length-1, .end=parser->tokens.length-1};
}

void parser_error(parser_t* parser, span_t span, char* err, int stop) {
	vector_pushcpy(&parser->errors, &(parser_error_t){.span=span, .err=err, .stop=stop});
	if (stop) parser->stop=1;
}

void parser_printerr(parser_t* parser, parser_error_t* perr) {
	int line=1,col=1;
	token_t* start_tok = vector_get(&parser->tokens, perr->span.start);
	for (char* x=start_tok->t+start_tok->start; x>=start_tok->t; x--) {
		if (*x=='\n') line++;
		if (line==1) col++;
	}

	fprintf(stderr, "%s at line %i col %i:\n%s\n\n", perr->stop ? "error" : "warning", line, col, perr->err);
}

void print_item(parser_t* parser, FILE* f, item_t* item) {
	if (item->gen) {
		if (!item->str) return;
		fprintf(f, "%s", item->str);
		return;
	}

	if (item->span.end<item->span.start) return;

	token_t* start = vector_get(&parser->tokens, item->span.start);
	token_t* end = vector_get(&parser->tokens, item->span.end);

	fprintf(f, "%.*s", end->start+end->len-start->start, start->t+start->start);
}

char* item_str(parser_t* parser, item_t* item) {
	token_t* start = vector_get(&parser->tokens, item->span.start);
	token_t* end = vector_get(&parser->tokens, item->span.end);

	if (item->span.end<item->span.start) return heapcpy(1, &(char){0});
	return heapcpysubstr(start->t+start->start, end->start+end->len-start->start);
}

void print_item_tree_rec(parser_t* parser, vector_t* items, int depth) {
	vector_iterator item_iter = vector_iterate(items);
	while (vector_next(&item_iter)) {
		item_t* item = *(item_t**)item_iter.x;
		for (int i=0; i<depth; i++) printf("  ");

		printf("%s (", ITEM_NAMES[item->ty]);
		print_item(parser, stdout, item);
		printf(")\n");

		print_item_tree_rec(parser, &item->body, depth+1);
	}
}

void print_item_tree(parser_t* parser) {
	print_item_tree_rec(parser, &parser->items, 0);
}

int parser_ncmp(parser_t* parser, char* x) {
	if (parser->len-parser->i>=strlen(x) && strncmp(parser->t+parser->i, x, strlen(x))==0) {
		parser->i+=strlen(x);
		return 1;
	} else {
		return 0;
	}
}

int skip_comment(parser_t* parser) {
	// skip comments
	if (parser_ncmp(parser, "//")) {
		while (parser->t[parser->i] && parser->t[parser->i] != '\n') parser->i++;
		return 1;
	} else if (parser_ncmp(parser, "/*")) {
		while (parser->t[parser->i] && !parser_ncmp(parser, "*/")) parser->i++;
		return 1;
	} else {
		return 0;
	}
}

void parser_skip_ws(parser_t* parser) {
	while (parser->t[parser->i]
					&& (parser->in_define ? strchr(" \t", parser->t[parser->i]) : strchr("\r\n\t ", parser->t[parser->i]))!=NULL)
		parser->i++;
}

//used when "reparsing" tokens, like below
void parser_reparse(parser_t* parser)	{
	token_t* t = vector_get(&parser->tokens, parser->tok_i);

	if (t) {
		parser->i=t->start;
		vector_truncate(&parser->tokens, parser->tok_i);
		vector_truncate(&parser->expansions, parser->expansions_i);
	}

	parser->tok_i++;
}

token_t parser_skip_define(parser_t* parser) {
	parser_reparse(parser);
	parser_skip_ws(parser);

	token_t tok = {.start=parser->i, .t=parser->t, .ty=tok_str};
	tok.strstart=parser->i;

	while (parser->t[parser->i] != '\n' && parser->t[parser->i] != '\r' && parser->t[parser->i]) {
		if (parser->t[parser->i]=='\\') parser->i++;
		parser->i++;
	}

	parser->in_define=0;
	tok.strlen=parser->i-tok.strstart;
	tok.len=tok.strlen;

	vector_pushcpy(&parser->tokens, &tok);

	return tok;
}

token_t parser_skip_arg(parser_t* parser) {
	parser_reparse(parser);
	parser_skip_ws(parser);

	token_t tok = {.start=parser->i, .t=parser->t, .ty=tok_str};
	tok.strstart=parser->i;

	unsigned parens=0;
	while ((parser->t[parser->i]!=')' && parser->t[parser->i]!=',') || parens!=0) {
		if (parser->t[parser->i]=='(') parens++;
		else if (parser->t[parser->i]==')') parens--;

		parser->i++;
	}

	tok.strlen=parser->i-tok.strstart;
	tok.len=tok.strlen;

	vector_pushcpy(&parser->tokens, &tok);

	return tok;
}

void parse_string(parser_t* parser, token_t* tok) {
	tok->ty=tok_str;
	tok->strstart=parser->i;

	char end=parser->t[tok->start];
	if (parser->t[tok->start]=='<') end='>';

	while (parser->t[parser->i] && parser->t[parser->i]!=end) {
		if (parser->t[parser->i]=='\\') parser->i++;
		parser->i++;
	}

	tok->strlen=parser->i-tok->strstart;
	parser->i++;
}

int parse_num(parser_t* parser, token_t* tok) {
	unsigned start = parser->i;
	while ((parser->t[parser->i]>='0' && parser->t[parser->i]<='9') || parser->t[parser->i]=='.') parser->i++;
	return parser->i>start;
}

int parser_name(parser_t* parser) {
	return parser->t[parser->i]=='_' || isalnum(parser->t[parser->i]);
}

token_t parse_token_fallacious(parser_t* parser) {
	do {
		parser_skip_ws(parser);

		if (parser->in_define) {
			if (parser->t[parser->i]=='\\') parser->i+=2;
			if (parser->t[parser->i]=='\r' || parser->t[parser->i]=='\n') {
				parser->in_define=0;
				parser->in_include=0;
				return (token_t){.start=parser->i++, .t=parser->t, .len=1, .ty=tok_enddir};
			}
		}
	} while (skip_comment(parser) && parser->t[parser->i]);

	token_t tok = {.start=parser->i, .t=parser->t};

	if (parser->in_include && parser->t[tok.start]=='<') {
		parse_string(parser, &tok);
		return tok;
	}

	switch (parser->t[tok.start]) {
		case '{': tok.ty=tok_lbrace; break;
		case '}': tok.ty=tok_rbrace; break;
		case '[': tok.ty=tok_lbrack; break;
		case ']': tok.ty=tok_rbrack; break;
		case '(': tok.ty=tok_lparen; break;
		case ')': tok.ty=tok_rparen; break;
		case ',': tok.ty=tok_comma; break;
		case ';': tok.ty=tok_end; break;

		case '\'': {
			parser->i++;

			tok.strstart=parser->i;

			if (parser->t[parser->i]=='\\') parser->i++;
			parser->i++;

			tok.strlen=parser->i-tok.strstart;

			if (parser->t[parser->i]!='\'') {
				parser_error(parser, parser_current(parser), "character string unterminated", 1);
			}

			parser->i++;
			tok.ty=tok_char;
			break;
		}

		case '"': {
			parser->i++;
			parse_string(parser, &tok);
			break;
		}

		case '#': {
			parser->i++;

			if (parser->in_define) {
				break;
			}

			parser->in_define=1;

			if (parser_ncmp(parser, "include")) {
				tok.ty=tok_include;
				parser->in_include=1;
				break;
			}

			if (parser_ncmp(parser, "ifdef")) tok.ty=tok_ifdef;
			else if (parser_ncmp(parser, "if")) tok.ty=tok_ifdir;
			else if (parser_ncmp(parser, "elif")) tok.ty=tok_elifdir;
			else if (parser_ncmp(parser, "else")) {
				tok.ty=tok_elsedir;
				parser->in_define=0;
			} else if (parser_ncmp(parser, "endif")) {
				tok.ty=tok_endif;
				parser->in_define=0;
			} else {
				if (parser_ncmp(parser, "define")) tok.ty=tok_define;
				else tok.ty=tok_dir;
			}

			break;
		}

		case '?': tok.ty=tok_ternary; break;
		case ':': tok.ty=tok_colon; break;

		case '-': case '+': case '*': case '/': case '%':
		case '&': case '|':
		case '^': case '~': {
			parser->i++;
			if (parser->t[parser->i]=='=') {
				parser->i++;
				tok.ty=tok_set;
			} else if (parser->t[tok.start]=='-' && parser->t[parser->i]=='>') {
				parser->i++;
				tok.ty=tok_access;
			} else if ((parser->t[tok.start]=='+' || parser->t[tok.start]=='-')
									&& parser->t[parser->i]==parser->t[tok.start]) {
				parser->i++;
				tok.ty=tok_unaryset;
			} else if ((parser->t[tok.start]=='&' || parser->t[tok.start]=='|')
									&& parser->t[parser->i]==parser->t[tok.start]) {
				parser->i++;
				tok.ty=tok_other;
			} else {
				if (parser->t[tok.start]=='*') tok.ty=tok_star;
				else if (parser->t[tok.start]=='&') tok.ty=tok_ref;
				else tok.ty=tok_other;
			}

			break;
		}

		case '!': case '<': case '>': case '=': {
			parser->i++;
			if (parser->t[parser->i]=='=') {
				parser->i++;
			} else if (parser->t[tok.start]=='=') {
				tok.ty=tok_set;
				break;
			}

			tok.ty=tok_other;
			break;
		}

		case '.':
		case '0'...'9': {
			if (parser_ncmp(parser, "...")) {
				tok.ty=tok_ellipsis;
				break;
			}

			parser->i++;
			if (!parse_num(parser, &tok) && parser->t[tok.start]=='.') {
				tok.ty=tok_dot;
			} else {
				tok.ty=tok_num;
			}

			break;
		}

		case 0: {
			tok.ty=tok_eof;
			break;
		}

		default: {
			if (parser_ncmp(parser, "if")) tok.ty=tok_if;
			else if (parser_ncmp(parser, "else")) {
				tok.ty=tok_else;

				if (parser_name(parser)) {
					tok.ty=tok_name;
					break;
				}

				parser_skip_ws(parser);
				unsigned back=parser->i;
				if (parser_ncmp(parser, "if")) tok.ty=tok_elseif;

				if (parser_name(parser)) {
					tok.ty=tok_else;
					parser->i=back;
					break;
				}

				else break;
			}
			else if (parser_ncmp(parser, "defer")) tok.ty=tok_defer;
			else if (parser_ncmp(parser, "return")) tok.ty=tok_return;
			else if (parser_ncmp(parser, "do")) tok.ty=tok_do;
			else if (parser_ncmp(parser, "while")) tok.ty=tok_while;
			else if (parser_ncmp(parser, "for")) tok.ty=tok_for;
			else if (parser_ncmp(parser, "goto")) tok.ty=tok_goto;
			else if (parser_ncmp(parser, "switch")) tok.ty=tok_switch;
			else if (parser_ncmp(parser, "break")) tok.ty=tok_break;
			else if (parser_ncmp(parser, "case")) tok.ty=tok_case;
			else if (parser_ncmp(parser, "default")) tok.ty=tok_default;
			else if (parser_ncmp(parser, "typedef")) tok.ty=tok_typedef;
			else if (parser_ncmp(parser, "enum")) tok.ty=tok_enum;
			else if (parser_ncmp(parser, "struct")) tok.ty=tok_struct;
			else if (parser_ncmp(parser, "union")) tok.ty=tok_union;
			else if (parser_ncmp(parser, "static")) tok.ty=tok_static;
			else if (parser_ncmp(parser, "inline")) tok.ty=tok_inline;
			else if (parser_ncmp(parser, "const")) tok.ty=tok_const;
			else if (parser_ncmp(parser, "__")) tok.ty=tok_compmacro;
			else tok.ty=tok_name;

			if (parser_name(parser)) tok.ty=tok_name;

			if (tok.ty==tok_name || tok.ty==tok_compmacro) {
				while (parser->t[parser->i] && parser_name(parser)) parser->i++;
			}
		}
	}

	if (parser->i==tok.start) parser->i++;

	return tok;
}

token_t parse_token(parser_t* parser) {
	token_t t;
	if (parser->tok_i<parser->tokens.length) {
		t = *(token_t*)vector_get(&parser->tokens, parser->tok_i++);
	} else {
		t = parse_token_fallacious(parser);
		t.len=parser->i-t.start;
		vector_pushcpy(&parser->tokens, &t);

		parser->tok_i = parser->tokens.length;
	}

	return t;
}

int parser_peek(parser_t* parser, token_ty ty, unsigned off) {
	token_t tok;
	unsigned old_tok_i = parser->tok_i;
	for (unsigned i=0; i<off; i++) tok = parse_token(parser);
	parser->tok_i=old_tok_i;
	return tok.ty==ty;
}

int parser_expect(parser_t* parser, token_ty ty, int err) {
	token_t tok = parse_token(parser);
	if (tok.ty==ty) {
		return 1;
	} else if (err) {
		parser_error(parser, parser_current(parser), heapstr("expected %s, got %s", TOKEN_NAMES[ty], TOKEN_NAMES[tok.ty]), 1);
	} else {
		parser->tok_i--;
	}

	return 0;
}

parser_save_t parser_save(parser_t* parser) {
	parser_save_t save = {.tok_i=parser->tok_i, .item_i=parser->items.length, .item_pool_i=parser->item_pool.length, .expansions_i=parser->expansions_i};

	if (parser->expansion_stack.length>0) {
		unsigned* i = vector_get(&parser->expansion_stack, parser->expansion_stack.length-1);
		parser_expansion_t* expansion = vector_get(&parser->expansions, *i);
		expansion->i = parser->i;

		vector_t* prev = vector_get(&parser->expansion_save, parser->expansion_save.length-1);
		if (!prev || vector_cmp(prev, &parser->expansion_stack)!=0) {
			vector_t* macrostack_cpy = vector_push(&parser->expansion_save);
			vector_cpy(&parser->expansion_stack, macrostack_cpy);
		}

		save.expansion_save = parser->expansion_save.length-1;
	} else {
		save.expansion_save = -1;
	}

	return save;
}

void parser_start(parser_t* parser) {
	vector_pushcpy(&parser->stack.vec, (parser_save_t[]){parser_save(parser)});
}

int parser_expectstart(parser_t* parser, token_ty ty) {
	if (parser_expect(parser, ty, 0)) {
		vector_pushcpy(&parser->stack.vec, &(parser_save_t){.tok_i=parser->tok_i-1, .item_i=parser->items.length, .item_pool_i=parser->item_pool.length});
		return 1;
	} else {
		return 0;
	}
}

void item_free(item_t* item) {
	if (item->gen && item->str) drop(item->str);

	vector_free(&item->body);
	drop(item);
}

void parser_trunc_items(parser_t* parser, unsigned i) {
	vector_iterator item_iter = vector_iterate(&parser->item_pool);
	item_iter.i=i-1;
	while (vector_next(&item_iter)) {
		item_free(*(item_t**)item_iter.x);
	}

	vector_truncate(&parser->item_pool, i);
}

void parser_macro_pop(parser_t* parser, unsigned len) {
	unsigned* up_i = vector_get(&parser->expansion_stack, parser->expansion_stack.length-len-1);
	parser_expansion_t* up = up_i ? vector_get(&parser->expansions, *up_i) : NULL;

	parser->t = up ? up->t : parser->source;
	parser->i = up ? up->i : parser->source_i;

	vector_removemany(&parser->expansion_stack, parser->expansion_stack.length-len, len);
}

void parser_restore(parser_t* parser, parser_save_t* save) {
	parser->tok_i=save->tok_i;
	parser_trunc_items(parser, save->item_pool_i);
	vector_truncate(&parser->items, save->item_i);
	vector_pop(&parser->stack.vec);

	if (save->expansion_save!=-1) {
		vector_t* vec = vector_get(&parser->expansion_save, save->expansion_save);

		vector_free(&parser->expansion_stack);
		vector_cpy(vec, &parser->expansion_stack);

		parser_expansion_t* expansion = vector_get(&parser->expansions, *(unsigned*)vector_get(vec, vec->length-1));
		parser->t = expansion ? expansion->t : parser->source;
		if (expansion) parser->i = expansion->i;
		else parser->i = parser->source_i;
	} else if (parser->expansion_stack.length) {
		parser_macro_pop(parser, parser->expansion_stack.length);
	}

	parser->expansions_i = save->expansions_i;
}

void parser_cancel(parser_t* parser) {
	parser_save_t* save = vector_get(&parser->stack.vec, parser->stack.vec.length-1);
	parser_restore(parser, save);
}

void parser_finish(parser_t* parser) {
	vector_pop(&parser->stack.vec);
}

item_t* parser_wrap(parser_t* parser, item_ty ty, int oob) {
	parser_save_t* save = vector_get(&parser->stack.vec, parser->stack.vec.length-1);
	//make new save, i guess? happens during syntax errors
	if (!save) save=vector_pushcpy(&parser->stack.vec, (parser_save_t[]){parser_save(parser)});

	item_t* item = heapcpy(sizeof(item_t), &(item_t){
		.ty=ty, .body=vector_new(sizeof(item_t*)),
		.span={.start=save->tok_i, .end=parser->tok_i-1},
		.if_stack=parser->current_if, .gen=0
	});

	if (parser->current_if!=-1) item->if_i = ((parser_if_t*)vector_get(&parser->ifs, parser->current_if))->i;
	if (parser->items.length>save->item_i)
		vector_stockcpy(&item->body, parser->items.length-save->item_i, vector_get(&parser->items, save->item_i));

	vector_iterator body_iter = vector_iterate(&item->body);
	while (vector_next(&body_iter)) {
		item_t* child = *(item_t**)body_iter.x;
		child->parent = item;
	}

	vector_truncate(&parser->items, save->item_i);
	if (!oob) vector_pushcpy(&parser->items, &item);
	vector_pushcpy(&parser->item_pool, &item);

	return item;
}

item_t* parser_push(parser_t* parser, item_ty ty, int oob) {
	item_t* ret = parser_wrap(parser, ty, oob);
	vector_pop(&parser->stack.vec);
	return ret;
}

void parser_skip_branch(parser_t* parser) {
	token_t tok = parse_token(parser);
	int i=1;
	while (tok.ty!=tok_eof && i!=0) {
		if (tok.ty==tok_ifdir || tok.ty==tok_ifdef) i++;
		else if (tok.ty==tok_endif) i--;
	}
}

void parser_push_ifdir(parser_t* parser, item_ty ty, int branch) {
	parser_if_t* p_if = vector_get(&parser->ifs, parser->current_if);

	item_t* item = parser_push(parser, ty, 1);

	if (branch) {
		if (!p_if) {
			parser_error(parser, item->span, "unmatched branch", 1);
			return;
		}

		p_if->i++;
	} else {
		p_if = vector_pushcpy(&parser->ifs, &(parser_if_t){.branch=vector_new(sizeof(parser_branch_t)),
				.i=0, .parent=parser->current_if, .parent_i=p_if ? p_if->i : -1, .tok_i=parser->tok_i});
		parser->current_if=parser->ifs.length-1;
	}

	vector_pushcpy(&p_if->branch, &(parser_branch_t){.item=item, .save=parser_save(parser)});
}

int parser_parse_if(parser_t* parser) {
	parser->parsed_if=0;

	if (parser_expectstart(parser, tok_ifdef)) {
		parser_expect(parser, tok_name, 1);
		parser_expect(parser, tok_enddir, 1);
		parser_push_ifdir(parser, item_ifdef, 0);
	} else if (parser_expectstart(parser, tok_ifdir)) {
		parser_skip_define(parser);
		parser_push_ifdir(parser, item_ifdir, 0);
	} else if (parser_expectstart(parser, tok_elifdir)) {
		parser_skip_define(parser);
		parser_push_ifdir(parser, item_elifdir, 1);
		parser->parsed_if=1;
	} else if (parser_expectstart(parser, tok_elsedir)) {
		parser_push_ifdir(parser, item_elsedir, 1);
		parser->parsed_if=1;
	} else {
		if (parser_expect(parser, tok_endif, 0)) {
			parser_if_t* p_if = vector_get(&parser->ifs, parser->current_if);
			if (!p_if) {
				parser_error(parser, parser_current(parser), "unmatched endif", 1);
				return 0;
			}

			parser->current_if = p_if->parent;
			return 1;
		}

		return 0;
	}

	return 1;
}

int parser_expect_pp(parser_t* parser, token_ty ty, int err);

void parser_handle_macros(parser_t* parser) {
	token_t t = parse_token(parser);
	parser->tok_i--;

	if (t.ty==tok_eof) {
		if (parser->expansion_stack.length>0) {
			parser_start(parser);
			parser_expect(parser, tok_eof, 0);
			parser_push(parser, item_macroeof, 0);

			parser_macro_pop(parser, 1);
		}

		return;
	}

	if (t.ty!=tok_name) return;

	item_t** macro_item = map_find(&parser->macros, &(map_sized_t){.bin=t.t+t.start, .size=t.len});
	if (!macro_item) return;

	parser_start(parser);
	parser_expect(parser, tok_name, 1);
	parser_wrap(parser, item_name, 0);

	char* str;
	if ((*macro_item)->ty==item_macroarg) {
		str = (*macro_item)->arg->arg_str;
	} else {
		macro_t* macro = (*macro_item)->macro;
		str = macro->define_str;

		if (macro->args.length>0) {
			parser_start(parser);
			parser_expect(parser, tok_lparen, 1);

			vector_iterator arg_iter = vector_iterate(&macro->args);
			while (vector_next(&arg_iter)) {
				item_t* arg_name_item = *(item_t**)arg_iter.x;
				token_t* arg_name_tok = vector_get(&parser->tokens, arg_name_item->span.start);

				parser_start(parser);
				parser_skip_arg(parser);
				item_t* arg = parser_push(parser, item_macroarg, 0);

				arg->arg = heapcpy(sizeof(arg_t), &(arg_t){.arg_str=item_str(parser, arg)});
				map_insertcpy(&parser->macros, &(map_sized_t){.bin=arg_name_tok->t+arg_name_tok->start, .size=arg_name_tok->len}, &arg);

				if (arg_iter.i==macro->args.length-1) parser_expect(parser, tok_rparen, 1);
				else parser_expect(parser, tok_comma, 1);
			}

			parser_push(parser, item_args, 0);
		}
	}

	unsigned* up_i = vector_get(&parser->expansion_stack, parser->expansion_stack.length-1);
	parser_expansion_t* up = up_i ? vector_get(&parser->expansions, *up_i) : NULL;

	if (up) up->i = parser->i;
	else parser->source_i = parser->i;

	parser->t = str;
	vector_pushcpy(&parser->expansion_stack, &(unsigned){parser->expansions_i});

	if (parser->expansions_i<parser->expansions.length) {
		parser_expansion_t* expansion = vector_get(&parser->expansions, parser->expansions_i++);
		parser->i = expansion->i;
	} else {
		vector_pushcpy(&parser->expansions, &(parser_expansion_t){.i=0, .t=str});
		parser->i = 0;
		parser->expansions_i++;
	}

	parser_push(parser, item_macrocall, 0);
}

void parser_handle_pp(parser_t* parser) {
	while (1) {
		parser_handle_macros(parser);

		if (parser_expectstart(parser, tok_include)) {
			parser_start(parser);
			parser_handle_macros(parser);
			parser_expect(parser, tok_str, 1);
			parser_push(parser, item_literal_str, 0);

			parser_expect(parser, tok_enddir, 1);
			vector_pushcpy(&parser->items, &(item_t*){parser_push(parser, item_include, 1)});
		} else if (parser_expectstart(parser, tok_define)) {
			parser_start(parser);
			parser_expect(parser, tok_name, 1);
			item_t* name_item = parser_push(parser, item_name, 0);

			macro_t* macro = heap(sizeof(macro_t));
			macro->args = vector_new(sizeof(item_t*));

			parser_start(parser);
			if (parser_expect(parser, tok_lparen, 0)) {
				if (!parser_expect(parser, tok_rparen, 0)) while (1) {
					parser_start(parser);
					parser_expect(parser, tok_name, 1);
					item_t* arg = parser_push(parser, item_name, 0);

					vector_pushcpy(&macro->args, &arg);

					if (!parser_expect(parser, tok_comma, 0)) {
						parser_expect(parser, tok_rparen, 1); break;
					}
				}
			}

			parser_push(parser, item_args, 0);

			parser_start(parser);
			parser_skip_define(parser);
			item_t* body = parser_push(parser, item_body, 0);
			macro->define_str = item_str(parser, body);

			item_t* define = parser_push(parser, item_define, 1);
			vector_pushcpy(&parser->items, &define);

			define->macro = macro;

			token_t* name_tok = vector_get(&parser->tokens, name_item->span.start);
			map_insertcpy(&parser->macros,
								 &(map_sized_t){.bin=name_tok->t+name_tok->start, .size=name_tok->len}, &define);
		} else if (parser_parse_if(parser)) {
			continue;
		} else if (parser_expectstart(parser, tok_dir)) {
			parser_skip_define(parser);
			parser_push(parser, item_dir, 0);
		} else if (parser_expectstart(parser, tok_compmacro)) {
			parser_wrap(parser, item_name, 0);

			parser_start(parser);
			if (parser_expect(parser, tok_lparen, 0)) while (1) {
				parser_start(parser);
				parser_skip_arg(parser);
				parser_push(parser, item_macroarg, 0);

				if (!parser_expect(parser, tok_comma, 0)) {
					parser_expect(parser, tok_rparen, 1); break;
				}
			}

			parser_push(parser, item_args, 0);

			parser_push(parser, item_macrocall, 0);
		} else {
			return;
		}
	}
}

int parser_expect_pp(parser_t* parser, token_ty ty, int err) {
	parser_handle_pp(parser);
	return parser_expect(parser, ty, err);
}

int parser_expectstart_pp(parser_t* parser, token_ty ty) {
	parser_handle_pp(parser);
	return parser_expectstart(parser, ty);
}

int parser_peek_pp(parser_t* parser, token_ty ty, unsigned off) {
	parser_handle_pp(parser);
	return parser_peek(parser, ty, off);
}

void parse_expr(parser_t* parser, int allow_comma, int optional);

void parse_args(parser_t* parser) {
	if (parser_expect_pp(parser, tok_rparen, 0)) return;

	while (1) {
		parse_expr(parser, 0, 0);

		if (!parser_expect_pp(parser, tok_comma, 0)) {
			parser_expect_pp(parser, tok_rparen, 1); break;
		}
	}
}

void parse_addendums(parser_t* parser) {
	while (1) {
		if (parser_expect_pp(parser, tok_lbrack, 0)) {
			parser_start(parser);
			parse_expr(parser, 1, 0);
			parser_push(parser, item_array, 0);

			parser_expect_pp(parser, tok_rbrack, 1);
		} else if (parser_expect_pp(parser, tok_access, 0)) {
			parser_start(parser);
			parser_expect_pp(parser, tok_name, 1);
			parser_push(parser, item_access, 0);
		} else if (parser_expect_pp(parser, tok_dot, 0)) {
			parser_start(parser);
			parser_expect_pp(parser, tok_name, 1);
			parser_push(parser, item_dot, 0);
		} else {
			break;
		}
	}
}

int parse_ty(parser_t* parser, int named);

int parse_aftertype(parser_t* parser, int named) {
	parser_start(parser);

	if (parser_expectstart_pp(parser, tok_const)
			|| parser_expectstart_pp(parser, tok_star)) {
		while (parser_expect_pp(parser, tok_const, 0)
				|| parser_expect_pp(parser, tok_star, 0));
		parser_push(parser, item_typemod, 0);
	}

	if (parser_expectstart_pp(parser, tok_lparen)) {
		if (!parser_expect_pp(parser, tok_star, 0)) {
			parser_finish(parser);
			parser_cancel(parser);
			return 0;
		}

		if (named) if (!parse_aftertype(parser, 1)) {
				parser_finish(parser);
				parser_cancel(parser);
				return 0;
			}

		parser_expect_pp(parser, tok_rparen, 1);
		parser_expect_pp(parser, tok_lparen, 1);

		if (!parser_expect_pp(parser, tok_rparen, 0)) while (1) {
				parser_start(parser);
				parse_ty(parser, 0);
				parser_push(parser, item_arg, 0);

				if (!parser_expect_pp(parser, tok_comma, 0)) {
					parser_expect_pp(parser, tok_rparen, 1); break;
				}
			}

		parser_push(parser, item_fnptr, 0);
	} else if (named) {
		if (!parser_expectstart_pp(parser, tok_name)) {
			parser_cancel(parser);
			return 0;
		}

		parser_push(parser, item_name, 0);
	}

	if (named) {
		parse_addendums(parser);
	} else {
		if (parser_expectstart_pp(parser, tok_lbrack)) {
			parser_expect_pp(parser, tok_rbrack, 1);
			parser_push(parser, item_array, 0);
		}
	}

	parser_push(parser, item_uber, 0);

	return 1;
}

int parse_initializer(parser_t* parser) {
	parser_start(parser);

	if (!parser_expect_pp(parser, tok_lbrace, 0)) {
		parser_finish(parser);
		return 0;
	}

	if (!parser_expect_pp(parser, tok_rbrace, 0)) while (1) {
		parser_start(parser);
		if (parser_expect_pp(parser, tok_dot, 0)) {
			parser_start(parser);
			parser_expect_pp(parser, tok_name, 1);
			parser_push(parser, item_name, 0);

			parser_expect_pp(parser, tok_set, 1);
			parse_expr(parser, 0, 0);

			parser_push(parser, item_initvar, 0);
		} else if (parser_expect_pp(parser, tok_lbrack, 0)) {
			parse_expr(parser, 0, 0);
			parser_expect_pp(parser, tok_rbrack, 1);

			parser_expect_pp(parser, tok_set, 1);
			parse_expr(parser, 0, 0);

			parser_push(parser, item_initi, 0);
		} else {
			parse_expr(parser, 0, 0);
			parser_finish(parser);
		}

		if (!parser_expect_pp(parser, tok_comma, 0)) {
			parser_expect_pp(parser, tok_rbrace, 1); break;
		}
	}

	parser_push(parser, item_initializer, 0);
	return 1;
}

int parse_op(parser_t* parser) {
	if (!parser_expectstart_pp(parser, tok_other) && !parser_expectstart_pp(parser, tok_star)) {
		return 0;
	}

	parser_push(parser, item_op, 0);
	return 1;
}

int parse_expr_left(parser_t* parser, int optional) {
	parser_start(parser);

	int ref = parser_expect_pp(parser, tok_ref, 0);
	if (ref) parser_wrap(parser, item_op, 0);

	int cast;
	parser_start(parser);

	if (parser_expect_pp(parser, tok_lparen, 0)
			&& parse_ty(parser, 0)
			&& parser_expect_pp(parser, tok_rparen, 0)
			//no operators after cast
			&& !parse_op(parser)) {

		parser_finish(parser);
		parser_start(parser);

		cast=1;
	} else {
		parser_cancel(parser);
		cast=0;
	}

	if (parse_initializer(parser)) {
		;
	} else if (cast && ref) {
		parser_error(parser, parser_current(parser), "cannot take reference of casted value", 1);

		//unary ops
	} else if (parse_op(parser)) {
		parse_expr_left(parser, 0);
		parser_push(parser, item_expr, 0);
	} else if (parser_expect_pp(parser, tok_unaryset, 0)) {
		parser_wrap(parser, item_op, 0);
		parse_expr_left(parser, 0);
		parser_push(parser, item_expr, 0);
		parser_wrap(parser, item_assignment, 0);

	} else if (parser_expect_pp(parser, tok_lparen, 0)) {
		parse_expr(parser, 1, 0);
		parser_expect_pp(parser, tok_rparen, 1);
		parse_addendums(parser);

	} else if (parser_expect_pp(parser, tok_name, 0)) {
		parser_wrap(parser, item_name, 0);
		parse_addendums(parser);

		if (parser_expect_pp(parser, tok_lparen, 0)) {
			parse_args(parser);
			parse_addendums(parser);
			parser_wrap(parser, item_fncall, 0);
		}

	} else if (parser_expect_pp(parser, tok_num, 0)) {
		parser_wrap(parser, item_literal_num, 0);
	} else if (parser_expect_pp(parser, tok_str, 0)) {
		parser_wrap(parser, item_literal_str, 0);
	} else if (parser_expect_pp(parser, tok_char, 0)) {
		parser_wrap(parser, item_literal_char, 0);
	} else {
		parser_finish(parser);
		if (cast) parser_finish(parser);

		if (optional && !cast) return 0;
		else parser_error(parser, parser_current(parser), "expected expression", 1);
	}

	if (cast) {
		parser_finish(parser);
		parser_wrap(parser, item_cast, 0);
	}

	return 1;
}

void parse_expr(parser_t* parser, int allow_comma, int optional) {
	if (!parse_expr_left(parser, optional)) return;

	//if a branch, restart
	if (parser->parsed_if) {
		parser_push(parser, item_expr, 0);
		return parse_expr(parser, allow_comma, optional);
	}

	if (parser_expectstart_pp(parser, tok_ternary)) {
		parse_expr(parser, allow_comma, 0);
		parser_expect_pp(parser, tok_colon, 1);
		parse_expr(parser, allow_comma, 0);
		parser_push(parser, item_ternary, 0); //expr (expr, ternary (expr, expr)) :)

	} else if (parser_expectstart_pp(parser, tok_set)) {
		parser_push(parser, item_op, 0);
		parse_expr(parser, allow_comma, 1);
		parser_wrap(parser, item_assignment, 0);

	} else if (parse_op(parser)) {
		parse_expr(parser, allow_comma, 1);

	} else {
		parser_start(parser);
		if (parser_expect_pp(parser, tok_unaryset, 0)) {
			parser_push(parser, item_op, 0);
			parser_wrap(parser, item_assignment, 0);
		} else {
			parser_finish(parser);
		}
	}

	parser_push(parser, item_expr, 0);

	if (allow_comma && parser_expect_pp(parser, tok_comma, 0)) {
		parse_expr(parser, 1, 0);
	}
}

int parse_ty(parser_t* parser, int named)	{
	parser_start(parser);

	int is_union = parser_expect_pp(parser, tok_union, 0),
			is_struct = parser_expect_pp(parser, tok_struct, 0),
			is_enum = parser_expect_pp(parser, tok_enum, 0);

	if (parser_expectstart_pp(parser, tok_const)) {
		parser_push(parser, item_typemod, 0);
	}

	if (is_union || is_struct || is_enum) {
		if (parser_expectstart_pp(parser, tok_name))
			parser_push(parser, item_name, 0);

		parser_start(parser);

		if (parser_expect_pp(parser, tok_lbrace, 0)) {
			if (!parser_expect_pp(parser, tok_rbrace, 0)) while (1) {
				if (is_enum) {
					parser_start(parser);
					parser_expect_pp(parser, tok_name, 1);
					parser_push(parser, item_name, 0);

					if (parser_expect_pp(parser, tok_set, 0)) {
						parser_start(parser);
						parse_expr(parser, 0, 0);
						parser_push(parser, item_enumi, 0);
					}

					//enums allow comma before rbrace, unlike the usual comma-separated lists
					//however, do not allow no comma then rbrace
					if (!parser_expect_pp(parser, tok_comma, 0)) {
						parser_expect_pp(parser, tok_rbrace, 1); break;
					} else if (parser_expect_pp(parser, tok_rbrace, 0)) {
						break;
					}
				} else {
					parser_start(parser);
					parse_ty(parser, 1);
					while (parser_expect_pp(parser, tok_comma, 0)) parse_aftertype(parser, 1);

					parser_expect_pp(parser, tok_end, 1);
					parser_push(parser, item_field, 0);

					if (parser_expect_pp(parser, tok_rbrace, 0)) break;
				}
			}

			parser_push(parser, item_body, 0);
		}

		if (is_union) parser_wrap(parser, item_union, 0);
		else if (is_struct) parser_wrap(parser, item_struct, 0);
		else if (is_enum) parser_wrap(parser, item_enum, 0);
	} else {
		if (parser_expectstart_pp(parser, tok_name)) {
			parser_push(parser, item_name, 0);
		} else {
			parser_cancel(parser);
			return 0;
		}
	}

	if (named) {
		parser_wrap(parser, item_type, 0);
	}

	if (!parse_aftertype(parser, named)) {
		parser_cancel(parser);
		return 0;
	}

	if (!named) {
		parser_push(parser, item_type, 0);
	} else {
		parser_finish(parser);
	}

	return 1;
}

int parse_var(parser_t* parser) {
	parser_start(parser);

	if (!parse_ty(parser, 1)) {
		parser_finish(parser);
		return 0;
	}

	parser_start(parser);

	while (1) {
		if (parser_expect_pp(parser, tok_set, 0)) {
			if (!parse_initializer(parser))
				parse_expr(parser, 0, 0);
		} else if (parser_expect_pp(parser, tok_end, 0)) {
			parser_push(parser, item_var, 0);
			parser_push(parser, item_varset, 0);
			return 1;
		} else if (parser_expect_pp(parser, tok_comma, 0)) {
			parser_push(parser, item_var, 0);
			parser_start(parser);

			if (!parse_aftertype(parser, 1))
				parser_error(parser, parser_current(parser), "variables need names", 1);
		} else {
			parser_finish(parser);
			parser_cancel(parser);
			return 0;
		}
	}
}

int parse_block(parser_t* parser);

void parse_stmt(parser_t* parser) {
	parser_start(parser);

	if (parser_expect_pp(parser, tok_break, 0)) {
		parser_expect_pp(parser, tok_end, 1);
		parser_push(parser, item_break, 0);
	} else if (parser_expect_pp(parser, tok_goto, 0)) {
		parser_start(parser);
		parser_expect_pp(parser, tok_name, 1);
		parser_push(parser, item_name, 0);

		parser_expect_pp(parser, tok_end, 1);
		parser_push(parser, item_goto, 0);
	} else if (parser_expect_pp(parser, tok_defer, 0)) {
		parse_expr(parser, 1, 0);
		parser_expect_pp(parser, tok_end, 1);
		parser_push(parser, item_defer, 0);
	} else if (parser_expect_pp(parser, tok_return, 0)) {
		parse_expr(parser, 1, 1);
		parser_expect_pp(parser, tok_end, 1);
		parser_push(parser, item_ret, 0);
	}  else if (parser_expect_pp(parser, tok_do, 0)) {
		parse_stmt(parser);
		parser_expect_pp(parser, tok_while, 1);

		parser_expect_pp(parser, tok_lparen, 1);
		parse_expr(parser, 1, 0);
		parser_expect_pp(parser, tok_rparen, 1);
		parser_expect_pp(parser, tok_end, 1);

		parser_push(parser, item_dowhile, 0);
	} else if (parser_expect_pp(parser, tok_while, 0)) {
		parser_expect_pp(parser, tok_lparen, 1);
		parse_expr(parser, 1, 0);
		parser_expect_pp(parser, tok_rparen, 1);

		parse_stmt(parser);

		parser_push(parser, item_while, 0);
	} else if (parser_expect_pp(parser, tok_for, 0)) {
		parser_expect_pp(parser, tok_lparen, 1);

		if (!parse_var(parser)) {
			parse_expr(parser, 1, 1);
			parser_expect_pp(parser, tok_end, 1);
		}

		parse_expr(parser, 1, 1);
		parser_expect_pp(parser, tok_end, 1);

		parse_expr(parser, 1, 1);
		parser_expect_pp(parser, tok_rparen, 1);

		parse_stmt(parser);

		parser_push(parser, item_for, 0);
	} else if (parser_expect_pp(parser, tok_if, 0)) {
		parser_expect_pp(parser, tok_lparen, 1);
		parse_expr(parser, 1, 0);
		parser_expect_pp(parser, tok_rparen, 1);

		parse_stmt(parser);

		parser_start(parser);
		while (parser_expect_pp(parser, tok_elseif, 0)) {
			parser_expect_pp(parser, tok_lparen, 1);
			parse_expr(parser, 1, 0);
			parser_expect_pp(parser, tok_rparen, 1);

			parse_stmt(parser);
			parser_push(parser, item_elseif, 0);

			parser_start(parser);
		}

		if (parser_expect_pp(parser, tok_else, 0)) {
			parse_stmt(parser);
			parser_push(parser, item_else, 0);
		} else {
			parser_finish(parser);
		}

		parser_push(parser, item_if, 0);
	} else if (parser_expect_pp(parser, tok_switch, 0)) {
		parser_expect_pp(parser, tok_lparen, 1);
		parse_expr(parser, 1, 0);
		parser_expect_pp(parser, tok_rparen, 1);

		parser_start(parser);
		parser_expect_pp(parser, tok_lbrace, 1);

		if (!parser_expect_pp(parser, tok_rbrace, 0))
			while (1) {
				if (parser_expectstart_pp(parser, tok_case)) {
					parse_expr(parser, 1, 0);

					if (parser_expectstart_pp(parser, tok_ellipsis)){
						parser_push(parser, item_op, 0);
						parse_expr(parser, 1, 0);
						parser_wrap(parser, item_expr, 0);
					}

					parser_expect_pp(parser, tok_colon, 1);
					parser_push(parser, item_case, 0);
				} else if (parser_expectstart_pp(parser, tok_default)) {
					parser_expect_pp(parser, tok_colon, 1);
					parser_push(parser, item_case, 0);
				} else if (parser_expect_pp(parser, tok_rbrace, 0)) {
					break;
				} else {
					parse_stmt(parser);
				}
			}

		parser_push(parser, item_block, 0);
		parser_push(parser, item_switch, 0);
	} else if (parse_block(parser)) {
		parser_finish(parser);
	} else {
		if (parser_peek_pp(parser, tok_name, 1) && parser_peek_pp(parser, tok_colon, 2)) {
			parser_expect_pp(parser, tok_name, 0);
			parser_wrap(parser, item_name, 0);
			parser_expect_pp(parser, tok_colon, 0);
			parser_push(parser, item_label, 0);
			return;
		}

		if (!parse_var(parser)) {
			parse_expr(parser, 1, 1);
			parser_expect_pp(parser, tok_end, 1);
		}

		parser_finish(parser);
	}
}

int parse_block(parser_t* parser) {
	if (!parser_expectstart_pp(parser, tok_lbrace)) return 0;

	if (!parser_expect_pp(parser, tok_rbrace, 0))
		while (!parser_expect_pp(parser, tok_rbrace, 0)) {
			parse_stmt(parser);
		}

	parser_push(parser, item_block, 0);

	return 1;
}


int parse_decl(parser_t* parser) {
	parser_start(parser);
	if (parser_expect_pp(parser, tok_typedef, 0)) {
		parse_ty(parser, 0);

		parser_start(parser);
		parser_expect_pp(parser, tok_name, 1);
		parser_push(parser, item_name, 0);

		parser_expect_pp(parser, tok_end, 1);
		parser_push(parser, item_typedef, 0);
		return 1;
	}

	int _static, _inline;
	if ((_static=parser_expect_pp(parser, tok_static, 0))) parser_expect_pp(parser, tok_inline, 0);
	else if ((_inline=parser_expect_pp(parser, tok_inline, 0))) parser_expect_pp(parser, tok_static, 0);

	if (parse_ty(parser, 0)) {
		if (parser_expectstart_pp(parser, tok_name)) {
			parser_push(parser, item_name, 0);

			if (parser_expect_pp(parser, tok_lparen, 0)) {
				parser_start(parser);

				if (!parser_expect_pp(parser, tok_rparen, 0)) while (1) {
					parser_start(parser);
					parse_ty(parser, 1);
					parser_push(parser, item_arg, 0);

					if (!parser_expect_pp(parser, tok_comma, 0)) {
						parser_expect_pp(parser, tok_rparen, 1); break;
					}
				}

				parser_push(parser, item_args, 0);

				if (!parse_block(parser)) {
					parser_expect_pp(parser, tok_end, 1);
				}

				parser_push(parser, item_func, 0);
				return 1;
			} else {
				parser_cancel(parser);
			}
		} else if (_static || _inline || !parser_expect_pp(parser, tok_end, 0)) {
			parser_cancel(parser);
		} else {
			parser_finish(parser);
			return 1;
		}
	} else {
		parser_cancel(parser);
	}

	if (parse_var(parser)) {
		return 1;
	} else {
		parser_error(parser, parser_current(parser), "expected function, type, or var", 1);
		return 0;
	}
}

parser_t parser_new(char* txt) {
	parser_t p = {
			.tok_i=0, .current_if=-1, .in_define=0, .len=strlen(txt),
			.t=txt, .i=0, .source=txt, .source_i=0,

			.errors=vector_new(sizeof(parser_error_t)), .tokens=vector_new(sizeof(token_t)),
			.stack=vector_alloc(vector_new(sizeof(parser_save_t)), 0), .ifs=vector_new(sizeof(parser_if_t)),
			.items=vector_new(sizeof(item_t*)),
			.item_pool=vector_new(sizeof(item_t*)),
			.macros=map_new(),

			.expansions_i=0,
			.expansions=vector_new(sizeof(parser_expansion_t)),
			.expansion_stack=vector_new(sizeof(unsigned)),
			.expansion_save=vector_new(sizeof(vector_t))
	};

	map_configure_sized_key(&p.macros, sizeof(item_t*));
	return p;
}

parser_t parse_file(char* filename) {
	parser_t parser = parser_new(read_file(filename));

	while (!parser_expect_pp(&parser, tok_eof, 0)) {
		if (!parse_decl(&parser)) break;
	}

	return parser;
}

void parser_free(parser_t* parser) {
	parser_trunc_items(parser, 0);

	vector_free(&parser->items);
	vector_free(&parser->tokens);
	vector_free(&parser->ifs);
	vector_free(&parser->stack.vec);
	vector_free(&parser->errors);
}
