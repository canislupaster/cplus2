#include <stdio.h>

#include "parse.h"
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

void emit_item(emitter_t* e);
int emit_item_next(emitter_t* e);

int emit_next_macro(emitter_t* e) {
	if (!emit_item_next(e)) return 0;
	emit_item(e);
	return 1;
}

int emit_next(emitter_t* e) {
	do
		if (!emit_next_macro(e)) return 0;
	while (item_special(e->iter.x));

	return 1;
}

int emit_item_ty(emitter_t* e, item_ty ty) {
	item_t* next = item_peek(&e->iter, 1);
	if (!next) return 0;

	if (item_special(next)) {
		emit_next_macro(e);
		return emit_item_ty(e, ty);
	} else if (next->ty==ty) {
		emit_next_macro(e);
		return 1;
	} else {
		return 0;
	}
}

#define LINE_SPEC e->newline ? "#line %u \"%s\"\n" : "\n#line %u \"%s\"\n"

void flush_whitespace(emitter_t* e, unsigned tok_i) {
	if (e->macro) return;

	token_t* etok = vector_get(&e->parser->tokens, e->tok);
	token_t* start = vector_get(&e->parser->tokens, tok_i);
	//pad output
	int new_newline=0;
	for (char* x = e->parser->t+etok->start; x<e->parser->t+start->start; x++) {
		if (*x=='\n') {
			new_newline++;
			e->line++;
		}
	}

	int line_i=0;
	for (char* x = etok->t+etok->start; x<start->t+start->start; x++) {
		if (*x=='\n') {
			line_i++;
			if (new_newline>=3) continue;

			if (!e->excess_newline) {
				fprintf(e->f, "\n");
			} else {
				e->excess_newline--;
			}

			e->newline=1;
		}

		if ((*x=='\t' || (!e->space && *x==' ')) && line_i==new_newline){
			if (new_newline>=3) {
				fprintf(e->f, LINE_SPEC, e->line, e->fname);
				e->newline=1;
				e->excess_newline=0;
				new_newline=0;
			}

			fprintf(e->f, "%c", *x);
		}
	}

	if (new_newline>=3) {
		fprintf(e->f, LINE_SPEC, e->line, e->fname);
		e->newline=1;
		e->excess_newline=0;
	}

	e->space=0;
	e->tok=tok_i;
}

void emits(emitter_t* e, char* s) {
	if (e->macro) return;

	if (s[0]=='\n' && (e->excess_newline || e->newline)) {
		s++;
		if (e->excess_newline) e->excess_newline--;
	}

	unsigned len = strlen(s);
	if (len==0) return;

	if (s[len-1]=='\n') e->excess_newline++;
	else if (s[len-1]==' ') e->space=1;
	else {
		e->newline=0;
		e->space=0;

		if (*s=='(' && len==1) {
			item_t* x = item_peek(&e->iter, 1);
			if (x) flush_whitespace(e, x->span.start);
		}
	}

	fwrite(s, len, 1, e->f);
}

void emit_align_item(emitter_t* e) {
	if (e->iter.x->gen) {
		if (!e->gen) {
			fprintf(e->f, LINE_SPEC, 0, "(generated)");
			e->tok=-1;
			e->gen=1;
		} else {
			e->newline=0;
			e->excess_newline=0;
			e->space=0;
		}

		//discontinuity (by reinserting an item or after generated item)
	} else if (e->tok>e->iter.x->span.start) {
		token_t* start = vector_get(&e->parser->tokens, e->iter.x->span.start);

		e->line=1;

		for (char* x=start->t+start->start; x>=start->t; x--) {
			if (*x=='\n') e->line++;
		}

		fprintf(e->f, LINE_SPEC, e->line, e->fname);
		e->newline=1;
		e->excess_newline=0;
		e->tok=e->iter.x->span.start;
	} else {
		flush_whitespace(e, e->iter.x->span.end<e->iter.x->span.start ? e->iter.x->span.end : e->iter.x->span.start);
	}
}

void switch_branch(emitter_t* e, parser_if_t* p_if, unsigned from, unsigned to) {
	while (from!=to) {
		from++;

		parser_branch_t* b = vector_get(&p_if->branch, from);

		item_set(&e->iter, b->item);
		emit_align_item(e);
		emit_item(e);
		item_ascend(&e->iter);

		p_if->i = to;
	}
}

int emit_item_next(emitter_t* e) {
	if (!item_next(&e->iter)) return 0;

	if (e->macro) return 1;

	if (e->iter.x->if_stack != e->parser->current_if) {
		unsigned common_parent = -1;
		unsigned common_parent_i = e->iter.x->if_i;

		vector_t chain = vector_new(sizeof(unsigned));

		unsigned p_if_i = e->iter.x->if_stack;
		parser_if_t* p_if;
		for (;p_if_i!=-1;p_if_i=p_if->parent) {
			p_if = vector_get(&e->parser->ifs, p_if_i);

			unsigned p_if2_i = e->parser->current_if;
			parser_if_t* p_if2;
			for (;p_if2_i!=-1;p_if2_i=p_if2->parent) {
				p_if2 = vector_get(&e->parser->ifs, p_if2_i);

				if (p_if2_i==p_if_i) {
					common_parent=p_if_i;
				}
			}

			if (common_parent!=-1) break;
			else {
				p_if->i = common_parent_i;
				common_parent_i = p_if->parent_i;
				vector_pushcpy(&chain, &(unsigned){p_if_i});
			}
		}

		p_if_i = e->parser->current_if;
		for (;p_if_i!=common_parent;p_if_i=p_if->parent) {
			p_if = vector_get(&e->parser->ifs, p_if_i);
			fprintf(e->f, e->newline ? "#endif\n" : "\n#endif\n");
			e->excess_newline+=e->newline ? 1 : 2;
		}

		if (common_parent!=-1) {
			p_if = vector_get(&e->parser->ifs, common_parent);
			switch_branch(e, p_if, p_if->i, common_parent_i);
		}

		vector_iterator chain_iter = vector_iterate_end(&chain);
		while (vector_prev(&chain_iter)) {
			p_if_i = *(unsigned*)chain_iter.x;
			p_if = vector_get(&e->parser->ifs, p_if_i);

			switch_branch(e, p_if, -1, p_if->i);
		}

		e->parser->current_if = p_if_i;

		vector_free(&chain);
	} else if (e->parser->current_if!=-1) {
		parser_if_t* p_if = vector_get(&e->parser->ifs, e->parser->current_if);
		switch_branch(e, p_if, p_if->i, e->iter.x->if_i);
	}

	emit_align_item(e);
	return 1;
}

void emit_sep_items(emitter_t* e, char* sep) {
	while (emit_next(e)) {
		if (item_peek(&e->iter, 1))
			emits(e, sep);
	}
}

int emit_search_for_macroeof(emitter_t* e) {
	while (item_next(&e->iter)) {
		if (e->iter.x->ty==item_macroeof) {
			return 1;
		} else if (e->iter.x->body.length>0) {
			item_descend(&e->iter);
			if (emit_search_for_macroeof(e)) return 1;
			item_ascend(&e->iter);
		}
	}

	return 0;
}

void emit_item(emitter_t* e) {
	int descend=0;
	switch (e->iter.x->ty) {
		//directives, literals and names emitted verbatim
		case item_dir:
		case item_ifdir:
		case item_ifdef:
		case item_elsedir:
		case item_elifdir: {
			emits(e, "\n");
			if (!e->macro) print_item(e->parser, e->f, e->iter.x);
			emits(e, "\n");
			break;
		}

		case item_literal_str:
		case item_literal_char:
		case item_literal_num:
		case item_typemod:
		case item_op:
		case item_macroarg:
		case item_name: {
			if (!e->macro) print_item(e->parser, e->f, e->iter.x);
			e->newline=0;
			break;
		}

		case item_macroeof: {
			e->macro=0;
			break;
		}

		case item_macrocall: {
			item_descend(&e->iter);
			descend=1;

			emit_next(e);

			if (emit_item_next(e)) {
				emits(e, "(");
				item_descend(&e->iter);
				emit_sep_items(e, ",");
				item_ascend(&e->iter);
				emits(e, ")");
			}

			item_ascend(&e->iter);

			e->macro=1;
			break;
		}

		case item_break: {
			emits(e, "break;");
			break;
		}

		//save time, these items descend
		default: {
			item_t* x = e->iter.x;
			item_descend(&e->iter);
			descend=1;

			switch (x->ty) {
				case item_define: {
					emits(e, "\n#define ");
					emit_next(e);

					emit_item_next(e);
					item_descend(&e->iter);
					
					if (item_peek(&e->iter, 1)) {
						emits(e, "(");
						emit_sep_items(e, ",");
						emits(e, ")");
					}
					
					item_ascend(&e->iter);

					emit_item_next(e); //body

					if (!e->macro) print_item(e->parser, e->f, e->iter.x);

					emits(e, "\n");
					break;
				}
				case item_include: {
					emits(e, "\n#include ");
					emit_next(e);
					emits(e, "\n");
					break;
				}
				case item_func: {
					emit_next(e); //type
					emits(e, " ");
					emit_next(e); //name

					emits(e, "(");

					emit_item_next(e);
					item_descend(&e->iter); //args
					emit_sep_items(e, ",");
					item_ascend(&e->iter);

					emits(e, ")");

					emit_next(e); //block/body
					break;
				}
				case item_block: {
					emits(e, "{");

					while (emit_next(e)) {
						if (e->iter.x->ty==item_expr) emits(e, ";");
					}

					flush_whitespace(e, x->span.end);
					emits(e, "}");
					break;
				}
				case item_union:
				case item_struct: {
					emit_item_ty(e, item_typemod);
					emits(e, e->iter.x->ty == item_struct ? "struct " : "union ");
					emit_item_ty(e, item_name); //name
					
					emit_item_next(e);
					item_descend(&e->iter); //body
					
					emits(e, "{");

					while (emit_next(e)) emits(e, ";");
					item_ascend(&e->iter);

					flush_whitespace(e, x->span.end);
					emits(e, "}");
					break;
				}
				case item_field: {
					emit_next(e);
					emit_sep_items(e, ",");

					break;
				}
				case item_enum: {
					emit_item_ty(e, item_typemod);

					emits(e, "enum ");
					emit_item_ty(e, item_name);

					emit_item_next(e);
					item_descend(&e->iter);
					emits(e, "{");

					while (emit_next(e)) {
						emit_item_ty(e, item_enumi);
						if (item_peek(&e->iter, 1)) emits(e, ",");
					}

					item_ascend(&e->iter);

					flush_whitespace(e, x->span.end);
					emits(e, "}");
					break;
				}
				case item_enumi: {
					emits(e, " = ");
					emit_next(e);
					break;
				}
				case item_type: {
					emit_next(e);
					emit_next(e);
					if (!x->parent) emits(e, ";");
					break;
				}
				case item_typedef: {
					emits(e, "typedef ");
					emit_next(e);
					emits(e, " ");
					emit_next(e);
					emits(e, ";");
					break;
				}
				case item_varset: {
					emit_next(e); //type

					emit_item_next(e);
					emit_sep_items(e, ",");
					emits(e, ";");
					break;
				}
				case item_var: {
					emit_next(e); //uber
					if (emit_item_next(e)) {
						emits(e, " = ");
						emit_item(e);
					}
					break;
				}
				case item_initializer: {
					emits(e, "{");
					while (emit_next(e));
					flush_whitespace(e, x->span.end);
					emits(e, "}");
					break;
				}
				case item_initvar: {
					emits(e, ".");
					emit_next(e);
					emits(e, " = ");
					emit_next(e);
					break;
				}
				case item_initi: {
					emits(e, "[");
					emit_next(e);
					emits(e, "] = ");
					emit_next(e);
					break;
				}
				case item_ret: {
					emits(e, "return ");
					emit_next(e);
					emits(e, ";");
					break;
				}
				case item_goto: {
					emits(e, "goto ");
					emit_next(e);
					emits(e, ";");
					break;
				}
				case item_fncall: {
					emit_next(e); //name
					emits(e, "(");
					emit_sep_items(e, ",");
					emits(e, ")");

					break;
				}
				case item_fnptr: { //same thing except unnamed
					emits(e, "(*");
					emit_next(e);
					emits(e, ")");

					emits(e, "(");
					emit_sep_items(e, ",");
					emits(e, ")");
					break;
				}
				case item_label: {
					emit_next(e);
					emits(e, ":");
					break;
				}
				
				case item_expr: {
					while (emit_item_next(e) && e->iter.x->ty==item_op) emit_item(e);

					//lhs
					if (e->iter.x->ty==item_expr) emits(e, "(");
					emit_item(e);
					if (e->iter.x->ty==item_expr) emits(e, ")");

					emit_next(e);
					//rhs
					if (e->iter.x->ty!=item_ternary) emit_next(e);

					break;
				}
				case item_ternary: {
					emits(e, " ? ");
					emit_next(e);
					emits(e, " : ");
					emit_next(e);
					break;
				}
				case item_cast: {
					emits(e, "(");
					emit_next(e);
					emits(e, ")");
					emit_next(e);
				}

				case item_dot: {
					emits(e, ".");
					emit_next(e);
					break;
				}
				case item_access: {
					emits(e, "->");
					emit_next(e);
					break;
				}
				case item_array: {
					emits(e, "[");
					emit_next(e); //expr
					emits(e, "]");
					break;
				}
				case item_if: {
					emits(e, "if");
					emits(e, "(");
					emit_next(e);
					emits(e, ")");
					emit_next(e);
					while (emit_next(e));
					break;
				}
				case item_else: {
					emits(e, "else");
					emit_next(e);
					break;
				}
				case item_elseif: {
					emits(e, "else if");
					emits(e, "(");
					emit_next(e);
					emits(e, ")");
					emit_next(e);
					break;
				}
				case item_switch: {
					emits(e, "switch");
					emits(e, "(");
					emit_next(e);
					emits(e, ")");

					emit_next(e);
					break;
				}
				case item_case: {
					if (item_peek(&e->iter, 1)) {
						emits(e, "case ");
						emit_next(e);
						emits(e, ":");
					} else {
						emits(e, "default:");
					}

					break;
				}
				case item_while: {
					emits(e, "while");
					emits(e, "(");
					emit_next(e);
					emits(e, ")");
					emit_next(e);
					break;
				}
				case item_dowhile: {
					emits(e, "do ");
					emit_next(e);
					emits(e, " while ");
					emits(e, "(");
					emit_next(e);
					emits(e, ");");
					break;
				}
				case item_for: {
					emits(e, "for");
					emits(e, "(");
					emit_next(e); //var/expr
					emits(e, ";");
					emit_next(e); //expr
					emits(e, ";");
					emit_next(e); //expr
					emits(e, ")");
					emit_next(e);
					break;
				}

				default: while (emit_next(e));
			}

			item_ascend(&e->iter);
		}
	}

	if (e->macro && !descend) {
		item_descend(&e->iter);
		if (emit_search_for_macroeof(e))
			e->macro=0;
		item_ascend(&e->iter);
	}
}

void emit(char* fname, FILE* f, parser_t* parser) {
	parser->current_if = -1;

	emitter_t e = {.iter=item_iterate(parser), .f=f, .parser=parser, .line=-1, .tok=-1, .gen=0, .space=1, .excess_newline=0, .newline=1};
	e.fname = strreplace(fname, "\"", "\\\"");
	while (emit_next(&e));

	drop(e.fname);
	item_iterator_free(&e.iter);
}
