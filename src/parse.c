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
}

void parser_printerr(parser_t* parser, parser_error_t* perr) {
	int line=1,col=1;
	token_t* start_tok = vector_get(&parser->tokens, perr->span.start);
	for (char* x=parser->t+start_tok->start; x>=parser->t; x--) {
		if (*x=='\n') line++;
		if (line==1) col++;
	}

	fprintf(stderr, "%s at line %i col %i:\n%s\n\n", perr->stop ? "error" : "warning", line, col, perr->err);
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
void parser_remove_unexpected(parser_t* parser)	{
	token_t* t = vector_get(&parser->tokens, parser->tok_i);
	if (!t) return;

	parser->i=t->start;
	vector_truncate(&parser->tokens, parser->tok_i);
}

token_t parser_skip_define(parser_t* parser) {
	parser_remove_unexpected(parser);
	parser_skip_ws(parser);

	token_t tok = {.start=parser->i, .ty=tok_str};
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
				return (token_t){.start=parser->i++, .len=1, .ty=tok_enddir};
			}
		}
	} while (skip_comment(parser) && parser->t[parser->i]);

	token_t tok = {.start=parser->i};

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
			tok.ty=tok_eof; break;
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
	if (parser->tok_i<parser->tokens.length) {
		token_t* t = vector_get(&parser->tokens, parser->tok_i++);
		return *t;
	} else {
		token_t tok = parse_token_fallacious(parser);
		tok.len=parser->i-tok.start;
		vector_pushcpy(&parser->tokens, &tok);
		parser->tok_i++;
		return tok;
	}
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
	return (parser_save_t){.tok_i=parser->tok_i, .item_i=parser->items.length, .item_pool_i=parser->item_pool.length};
}

void parser_start(parser_t* parser) {
	vector_pushcpy(&parser->stack.vec, (parser_save_t[]){parser_save(parser)});
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

void parser_restore(parser_t* parser, parser_save_t* save) {
	parser->tok_i=save->tok_i;
	parser_trunc_items(parser, save->item_pool_i);
	vector_truncate(&parser->items, save->item_i);
	vector_pop(&parser->stack.vec);
}

void parser_cancel(parser_t* parser) {
	parser_save_t* save = vector_get(&parser->stack.vec, parser->stack.vec.length-1);
	parser_restore(parser, save);
}

void parser_finish(parser_t* parser) {
	vector_pop(&parser->stack.vec);
	if (parser->stack.vec.length==0) {
		vector_add(&parser->items, &parser->top);
		vector_clear(&parser->items);
	}
}

item_t* parser_wrap(parser_t* parser, item_ty ty, int top) {
	top = top || parser->stack.vec.length==1;

	parser_save_t* save = vector_get(&parser->stack.vec, parser->stack.vec.length-1);
	//make new save, i guess? happens during syntax errors
	if (!save) save=vector_pushcpy(&parser->stack.vec, (parser_save_t[]){parser_save(parser)});

	item_t* item = heapcpy(sizeof(item_t), &(item_t){.ty=ty, .body=vector_new(sizeof(item_t*)),
			.span={.start=save->tok_i, .end=parser->tok_i==save->tok_i ? save->tok_i : parser->tok_i-1},
			.if_stack=parser->current_if, .gen=0});

	if (parser->current_if!=-1) item->if_i = ((parser_if_t*)vector_get(&parser->ifs, parser->current_if))->i;
	if (parser->items.length>save->item_i) vector_stockcpy(&item->body, parser->items.length-save->item_i, vector_get(&parser->items, save->item_i));

	vector_iterator body_iter = vector_iterate(&item->body);
	while (vector_next(&body_iter)) {
		item_t* child = *(item_t**)body_iter.x;
		child->parent = item;
	}

	vector_truncate(&parser->items, save->item_i);
	vector_pushcpy(top ? &parser->top : &parser->items, &item);
	vector_pushcpy(&parser->item_pool, &item);

	return item;
}

item_t* parser_push(parser_t* parser, item_ty ty, int top) {
	item_t* ret = parser_wrap(parser, ty, top);
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

void parser_add_ifdir(parser_t* parser, item_ty ty, int branch) {
	parser_if_t* p_if = vector_get(&parser->ifs, parser->current_if);

	item_t* item = parser_push(parser, ty, 1);
	unsigned top_i = parser->top.length-1;

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

	vector_pushcpy(&p_if->branch, &(parser_branch_t){.top_i=top_i, .save=parser_save(parser)});
}

int parser_parse_if(parser_t* parser) {
	parser->parsed_if=0;

	parser_start(parser);
	if (parser_expect(parser, tok_ifdef, 0)) {
		parser_expect(parser, tok_name, 1);
		parser_expect(parser, tok_enddir, 1);
		parser_add_ifdir(parser, item_ifdef, 0);
	} else if (parser_expect(parser, tok_ifdir, 0)) {
		parser_skip_define(parser);
		parser_add_ifdir(parser, item_ifdir, 0);
	} else if (parser_expect(parser, tok_elifdir, 0)) {
		parser_skip_define(parser);
		parser_add_ifdir(parser, item_elifdir, 1);
		parser->parsed_if=1;
	} else if (parser_expect(parser, tok_elsedir, 0)) {
		parser_add_ifdir(parser, item_elsedir, 1);
		parser->parsed_if=1;
	} else {
		parser_finish(parser);

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

int parser_expect_pp(parser_t* parser, token_ty ty, int err) {
	//TODO: handle macros and substitutions with another wrapper
	while (1) {
		parser_start(parser);
		if (parser_expect(parser, tok_include, 0)) {
			parser_start(parser);
			parser_expect(parser, tok_str, 1);
			parser_push(parser, item_literal_str, 0);

			parser_expect(parser, tok_enddir, 1);
			parser_push(parser, item_include, 1);
		} else if (parser_expect(parser, tok_define, 0)) {
			parser_expect(parser, tok_name, 1);

			if (parser_expect(parser, tok_lparen, 0)) {
				if (!parser_expect(parser, tok_rparen, 0)) while (1) {
					parser_start(parser);
					parser_expect(parser, tok_name, 1);
					parser_push(parser, item_arg, 0);
					if (!parser_expect(parser, tok_comma, 0)) {
						parser_expect(parser, tok_rparen, 1); break;
					}
				}
			}

			parser_start(parser);
			parser_skip_define(parser);
			parser_push(parser, item_body, 0);

			parser_push(parser, item_define, 1);
		} else if (parser_parse_if(parser)) {
			parser_finish(parser);
		} else if (parser_expect(parser, tok_compmacro, 0)) {
			parser_push(parser, item_macrocall, 0);
		} else {
			parser_finish(parser);
			return parser_expect(parser, ty, err);
		}
	}
}

void parse_expr(parser_t* parser, int allow_comma, int optional);

void parse_args(parser_t* parser) {
	if (parser_expect_pp(parser, tok_rparen, 0)) return;

	while (1) {
		parser_start(parser);
		parse_expr(parser, 0, 0);
		parser_push(parser, item_arg, 0);

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

int parse_name(parser_t* parser, int arr) {
	parser_start(parser);
	while (parser_expect_pp(parser, tok_const, 0)
					|| parser_expect_pp(parser, tok_star, 0)
					|| parser_expect_pp(parser, tok_ref, 0));

	parser_start(parser);
	if (!parser_expect_pp(parser, tok_name, 0)) {
		parser_finish(parser);
		parser_cancel(parser);
		return 0;
	}

	parser_push(parser, item_name, 0);
	parser_finish(parser);

	if (arr) parse_addendums(parser);

	return 1;
}

int parse_ty(parser_t* parser, int named);

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

			parser_expect_pp(parser, tok_set, 1);
			parse_expr(parser, 0, 0);

			parser_expect_pp(parser, tok_rbrack, 1);
			parser_push(parser, item_initi, 0);
		} else {
			parse_expr(parser, 0, 0);
			parser_finish(parser);
		}

		if (!parser_expect(parser, tok_comma, 0)) {
			parser_expect_pp(parser, tok_rbrace, 1); break;
		}
	}

	parser_push(parser, item_initializer, 0);
	return 1;
}

int parse_op(parser_t* parser) {
	parser_start(parser);
	if (parser_expect_pp(parser, tok_other, 0));
	else if (parser_expect_pp(parser, tok_star, 0));
	else {
		parser_finish(parser);
		return 0;
	}

	parser_push(parser, item_op, 0);
	return 1;
}

void parse_expr(parser_t* parser, int allow_comma, int optional) {
	parser_start(parser);

	int ref = parser_expect_pp(parser, tok_ref, 0);
	int cast;

	if (parser_expect_pp(parser, tok_lparen, 0)
			&& parse_ty(parser, 0)
			&& parser_expect_pp(parser, tok_rparen, 0)
			//no operators after cast
			&& !parse_op(parser)) {

		parser_start(parser);

		cast=1;
	} else {
		parser_cancel(parser);
		parser_start(parser);

		cast=0;
	}

	if (parse_initializer(parser)) {
		;
	} else if (cast && ref) {
		parser_error(parser, parser_current(parser), "cannot take reference of casted value", 1);

	//unary ops
	} else if (parse_op(parser)) {
		parse_expr(parser, 0, 0);
	} else if (parser_expect_pp(parser, tok_unaryset, 0)) {
		parser_wrap(parser, item_op, 0);
		parse_expr(parser, 0, 0);
		parser_wrap(parser, item_assignment, 0);

	} else if (parser_expect_pp(parser, tok_lparen, 0)) {
		parse_expr(parser, 1, 0);
		parser_expect_pp(parser, tok_rparen, 1);
		parse_addendums(parser);

	} else if (parse_name(parser, 1)) {
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

		if (optional && !cast) return;
		else parser_error(parser, parser_current(parser), "expected expression", 1);
	}

	if (cast) {
		parser_finish(parser);
		parser_wrap(parser, item_cast, 0);
	}

	//if a branch, restart
	if (parser->parsed_if) {
		parser_push(parser, item_expr, 0);
		return parse_expr(parser, allow_comma, optional);
	}

	if (parser_expect_pp(parser, tok_ternary, 0)) {
		parse_expr(parser, allow_comma, 0);
		parser_expect_pp(parser, tok_colon, 1);
		parse_expr(parser, allow_comma, 0);
		parser_wrap(parser, item_ternary, 0);

	} else if (parser_expect_pp(parser, tok_set, 0)) {
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

	while (parser_expect_pp(parser, tok_const, 0));

	if (is_union || is_struct || is_enum) {
		parser_finish(parser);

		if (parser_expect_pp(parser, tok_name, 0))
			parser_wrap(parser, item_name, 0);

		parser_start(parser);

		if (parser_expect_pp(parser, tok_lbrace, 0)) {
			if (!parser_expect_pp(parser, tok_rbrace, 0)) while (1) {
				if (is_enum) {
					parser_start(parser);
					parser_expect_pp(parser, tok_name, 0);
					parser_push(parser, item_name, 0);

					if (parser_expect_pp(parser, tok_set, 0)) {
						parser_start(parser);
						parse_expr(parser, 0, 0);
						parser_push(parser, item_enumi, 0);
					}

					if (!parser_expect_pp(parser, tok_comma, 0)) {
						parser_expect_pp(parser, tok_rbrace, 1); break;
					}
				} else {
					parse_ty(parser, 1);
					while (parser_expect_pp(parser, tok_comma, 0)) parse_name(parser, 1);
					parser_expect_pp(parser, tok_end, 1);

					if (parser_expect_pp(parser, tok_rbrace, 0)) break;
				}
			}

			parser_push(parser, item_body, 0);
		}

		if (is_union) parser_wrap(parser, item_union, 0);
		else if (is_struct) parser_wrap(parser, item_struct, 0);
		else if (is_enum) parser_wrap(parser, item_enum, 0);
	} else {
		parser_start(parser);

		if (parser_expect_pp(parser, tok_name, 0)) {
			parser_push(parser, item_name, 0);

			//parse unnamed type addendums
			if (!named) {
				while (parser_expect_pp(parser, tok_star, 0) || parser_expect_pp(parser, tok_const, 0));
				if (parser_expect_pp(parser, tok_lbrack, 0)) {
					parser_expect_pp(parser, tok_rbrack, 1);
				}
			}

			parser_wrap(parser, item_type, 0);
		} else {
			parser_finish(parser);
			parser_finish(parser);
			return 0;
		}
	}

	if (parser_expect_pp(parser, tok_lparen, 0)) {
		if (!parser_expect_pp(parser, tok_star, 0)) {
			parser_cancel(parser);
			return 0;
		}

		if (named) if (!parse_name(parser, 1)) {
			parser_cancel(parser);
			return 0;
		}

		parser_expect_pp(parser, tok_rparen, 1);
		parser_expect_pp(parser, tok_lparen, 1);

		parser_start(parser);

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
		if (!parse_name(parser, 1)) {
			parser_cancel(parser);
			return 0;
		}
	}

	parser_finish(parser);
	return 1;
}

int parse_var(parser_t* parser) {
	parser_start(parser);

	if (parse_ty(parser, 1)) while (1) {
		if (parser_expect_pp(parser, tok_set, 0)) {
			if (!parse_initializer(parser))
				parse_expr(parser, 0, 0);
		} else if (parser_expect_pp(parser, tok_end, 0)) {
			parser_push(parser, item_var, 0);
			return 1;
		} else if (parser_expect_pp(parser, tok_comma, 0)) {
			parser_push(parser, item_var, 0);
			parser_start(parser);

			if (!parse_name(parser, 1)) parser_error(parser, parser_current(parser), "variables need names", 1);
		} else {
			parser_cancel(parser);
			return 0;
		}
	} else {
		parser_finish(parser);
		return 0;
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

		parser_expect_pp(parser, tok_lbrace, 1);

		while (1) {
			parser_start(parser);
			if (parser_expect_pp(parser, tok_case, 0)) {
				parse_expr(parser, 1, 0);

				parser_start(parser);
				if (parser_expect_pp(parser, tok_ellipsis, 0)){
					parser_push(parser, item_op, 0);
					parse_expr(parser, 1, 0);
				} else {
					parser_finish(parser);
				}

				parser_expect_pp(parser, tok_colon, 1);
				parser_push(parser, item_case, 0);
			} else if (parser_expect_pp(parser, tok_default, 0)) {
				parser_expect_pp(parser, tok_colon, 1);
				parser_push(parser, item_case, 0);
			} else if (parser_expect_pp(parser, tok_rbrace, 0)) {
				parser_finish(parser);
				break;
			} else {
				parse_stmt(parser);
				parser_finish(parser);
			}
		}

		parser_push(parser, item_switch, 0);
	} else if (parse_block(parser)) {
		parser_finish(parser);
	} else {
		if (parser_expect_pp(parser, tok_name, 0)) {
			parser_wrap(parser, item_name, 0);
			if (parser_expect_pp(parser, tok_colon, 0)) {
				parser_push(parser, item_label, 0);
				return;
			} else {
				parser_cancel(parser);
				parser_start(parser);
			}
		}

		if (!parse_var(parser)) {
			parse_expr(parser, 1, 1);
			parser_expect_pp(parser, tok_end, 1);
		}

		parser_finish(parser);
	}
}

int parse_block(parser_t* parser) {
	parser_start(parser);

	if (!parser_expect_pp(parser, tok_lbrace, 0)) {
		parser_finish(parser);
		return 0;
	}

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
		if (parse_name(parser, 0)) {
			if (parser_expect_pp(parser, tok_lparen, 0)) {
				if (!parser_expect_pp(parser, tok_rparen, 0)) while (1) {
					parser_start(parser);
					parse_ty(parser, 1);
					parser_push(parser, item_arg, 0);

					if (!parser_expect_pp(parser, tok_comma, 0)) {
						parser_expect_pp(parser, tok_rparen, 1); break;
					}
				}

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
	return (parser_t){.i=0, .tok_i=0, .current_if=-1, .in_define=0, .len=strlen(txt), .t=txt,
			.errors=vector_new(sizeof(parser_error_t)), .tokens=vector_new(sizeof(token_t)),
			.stack=vector_alloc(vector_new(sizeof(parser_save_t)), 0), .ifs=vector_new(sizeof(parser_if_t)),
			.items=vector_new(sizeof(item_t*)), .top=vector_new(sizeof(item_t*)),
			.item_pool=vector_new(sizeof(item_t*))};
}

parser_t parse_file(char* filename) {
	parser_t parser = parser_new(read_file(filename));

	while (!parser_expect_pp(&parser, tok_eof, 0)) {
		if (!parse_decl(&parser)) break;
	}

	return parser;
}
