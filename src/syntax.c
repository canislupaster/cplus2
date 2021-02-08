//THERES NO TREE ITS JUST SOME TAGSE ON SOME TOKNEs

#include <stdio.h>
#include <util.h>

#include "types.h"
#include "parse.h"

void print_item(parser_t* parser, FILE* f, item_t* item) {
	if (item->gen) {
		if (!item->str) return;
		fprintf(f, "%s", item->str);
		return;
	}

	token_t* start = vector_get(&parser->tokens, item->span.start);
	token_t* end = vector_get(&parser->tokens, item->span.end);

	fprintf(f, "%.*s", end->start+end->len-start->start, parser->t+start->start);
}

char* item_str(parser_t* parser, item_t* item) {
	token_t* start = vector_get(&parser->tokens, item->span.start);
	token_t* end = vector_get(&parser->tokens, item->span.end);

	return heapcpysubstr(parser->t+start->start, end->start+end->len-start->start);
}

int item_eq(parser_t* parser, item_t* i1, item_t* i2) {
	if (i1->ty!=i2->ty || i1->body.length!=i2->body.length) return 0;

	if (i1->body.length==0) {
		if (i1->ty!=item_name && i1->ty!=item_literal_str) return 1;
		token_t* start1 = vector_get(&parser->tokens, i1->span.start), *end1 = vector_get(&parser->tokens, i1->span.end);
		token_t* start2 = vector_get(&parser->tokens, i2->span.start), *end2 = vector_get(&parser->tokens, i2->span.end);
		if (end1->len+end1->start!=end2->len+end2->start || start1->start!=start2->start) return 0;
		return strncmp(parser->t+start1->start, parser->t+start2->start, end1->len+end1->start-start1->start);
	} else {
		vector_iterator body_iter = vector_iterate(&i1->body);
		while (vector_next(&body_iter)) {
			item_t* i1b=body_iter.x;
			item_t* i2b=vector_get(&i2->body, body_iter.i);
			if (!item_eq(parser, i1b, i2b)) return 0;
		}
	}

	return 1;
}

int item_child(item_t* item, item_t* child) {
	vector_iterator child_iter = vector_iterate(&item->body);
	while (vector_next(&child_iter)) {
		item_t* child_item = child_iter.x;
		if (child_item==child || item_child(child_item, child)) return 1;
	}

	return 0;
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
	print_item_tree_rec(parser, &parser->top, 0);
}

typedef struct {
	parser_t* parser;
	vector_t stack; //item_t** within body
	item_t** x_ref;
	item_t* x;
} item_iterator_t;

item_iterator_t item_iterate(parser_t* parser) {
	return (item_iterator_t){.parser=parser, .stack=vector_new(sizeof(item_t**)), .x_ref=(item_t**)vector_get(&parser->top,0)-1};
}

void item_restart(item_iterator_t* iter) {
	vector_clear(&iter->stack);
	iter->x_ref = (item_t**)vector_get(&iter->parser->top,0)-1;
}

item_t* item_parent(item_iterator_t* iter) {
	if (iter->stack.length==0) return NULL;
	return **(item_t***)vector_get(&iter->stack, iter->stack.length-1);
}

int item_next(item_iterator_t* iter) {
	item_t*** parent_ref = vector_get(&iter->stack, iter->stack.length-1);

	if (parent_ref) {
		item_t* parent = **parent_ref;

		unsigned i = (iter->x_ref+1)-(item_t**)vector_get(&parent->body, 0);
		if (i>=parent->body.length) {
			return 0;
		} else {
			iter->x_ref++;
		}
	} else {
		unsigned i = (iter->x_ref+1)-(item_t**)vector_get(&iter->parser->top, 0);
		if (i>=iter->parser->top.length) return 0;
		else iter->x_ref++;
	}
	
	iter->x = *iter->x_ref;

	return 1;
}

void item_descend(item_iterator_t* iter) {
	vector_pushcpy(&iter->stack, &iter->x_ref); //lmao a triple
	iter->x_ref = (item_t**)vector_get(&iter->x->body, 0) - 1;
}

void item_ascend(item_iterator_t* iter) {
	iter->x_ref=*(item_t***)vector_get(&iter->stack, iter->stack.length-1);
	vector_pop(&iter->stack);
	iter->x = *iter->x_ref;
}

void item_remove(item_iterator_t* iter) {
	item_t* parent = item_parent(iter);
	unsigned i = iter->x_ref-(item_t**)vector_get(&parent->body, 0);
	vector_remove(&parent->body, i);
	iter->x_ref = (item_t**)vector_get(&parent->body, i) - 1;
}

int item_until(item_iterator_t* iter, item_ty ty) {
	while (item_next(iter)) {
		if (iter->x->ty==ty) break;
	}

	return iter->x->ty==ty;
}

item_t* item_get(item_iterator_t* iter, unsigned i) {
	for (unsigned n=0; n<=i; n++) {
		item_next(iter);
		if (iter->x->ty==item_macrocall) n--;
	}

	return iter->x;
}

typedef struct {
	vector_t objs; //pointers to allocated objects (above)
	vector_t names;

	map_t name_label;
	map_t name_item;

	item_iterator_t iter;

	parser_t* parser;
} process_t;

item_t* scope_get(item_t* item) {
	while (1) {
		if (!item) return NULL;
		else if (item->ty==item_block) return item;
		item=item->parent;
	}
}

scope_t* scope_new(process_t* proc) {
	scope_t* sc = heapcpy(sizeof(scope_t), &(scope_t){.name_i=proc->names.length,
			.deferred=vector_new(sizeof(item_t*)), .exits=vector_new(sizeof(exit_t)),
			.ret=0, .br=0});

	vector_pushcpy(&proc->objs, &sc);
	proc->iter.x->scope=sc;
	return sc;
}

void scope_exit(process_t* proc, scope_t* scope) {
	vector_iterator name_iter = vector_iterate(&proc->names);
	name_iter.i=scope->name_i-1;
	while (vector_next(&name_iter)) {
		map_remove(&proc->name_item, name_iter.x);
	}

	vector_truncate(&proc->names, scope->name_i);
}

//dead code for now
int expr_const(item_iterator_t* iter) {
	while (item_next(iter)) {
		switch (iter->x->ty) {
			case item_ternary:
			case item_expr: {
				item_descend(iter);
				if (!expr_const(iter)) {
					item_ascend(iter);
					return 0;
				} else {
					item_ascend(iter);
					break;
				}
			}
			case item_name:
			case item_assignment:
			case item_fncall: return 0;
			default:;
		}
	}

	return 1;
}

item_t* item_new(process_t* proc, item_ty ty, item_t* inherit, item_t* parent) {
	item_t* item = heapcpy(sizeof(item_t), &(item_t){.ty=ty,
			.if_i=inherit ? inherit->if_i : -1,
			.if_stack=inherit ? inherit->if_stack : -1,
			.body=vector_new(sizeof(item_t)), .gen=1, .str=NULL, .parent=parent});

	vector_pushcpy(&proc->parser->item_pool, &item);

	return item;
}

item_t* item_push(process_t* proc, item_ty ty, item_t* parent, item_t* inherit) {
	item_t* item = item_new(proc, ty, inherit, parent);
	vector_pushcpy(&parent->body, &item);
	return item;
}

//pass 1: instantaniate scope & labels
//pass 2: populate scope

int scope_exits_early(process_t* proc, item_t* origin) {
	if (proc->iter.x->ty==item_block) {
		scope_t* sc = proc->iter.x->scope;
		vector_iterator exit_iter = vector_iterate(&sc->exits);
		while (vector_next(&exit_iter)) {
			item_t* exit = exit_iter.x;
			if (exit==origin || item_child(exit, origin)) return 1;
		}
	}

	int exits=0;
	item_descend(&proc->iter);

	while (item_next(&proc->iter)) {
		switch (proc->iter.x->ty) {
			case item_if: {
				item_t* last = vector_get(&proc->iter.x->body, proc->iter.x->body.length-1);
				if (last->ty!=item_else) break;

				exits=scope_exits_early(proc, origin);
				break;
			}
			case item_block: {
				exits=scope_exits_early(proc, origin);
				break;
			}
			default:;
		}

		if (exits) {
			item_ascend(&proc->iter);
			return 1;
		}
	}

	item_ascend(&proc->iter);
	return 0;
}

void tag_items(process_t* proc) {
	while (item_next(&proc->iter))
		switch (proc->iter.x->ty) {
			case item_block: {
				scope_t* sc = scope_new(proc);

				item_t* parent = item_parent(&proc->iter);

				sc->ret = parent->ty==item_func;
				sc->br = parent->ty==item_while || parent->ty==item_for;

				item_descend(&proc->iter);
				tag_items(proc);
				item_ascend(&proc->iter);

				scope_exit(proc, sc);
				break;
			}

			case item_func: {
				scope_t* sc = scope_new(proc);

				item_descend(&proc->iter);
				tag_items(proc);
				item_ascend(&proc->iter);

				scope_exit(proc, sc);
				map_clear(&proc->name_label);

				break;
			}

			case item_label: {
				item_descend(&proc->iter);
				char* k = item_str(proc->parser, item_get(&proc->iter, 0));
				item_ascend(&proc->iter);
				map_insertcpy(&proc->name_label, &k, &proc->iter.x);
				break;
			}

			default: {
				if (proc->iter.x->body.length>0) {
					item_descend(&proc->iter);
					tag_items(proc);
					item_ascend(&proc->iter);
				}
			}
		}
}

void process(process_t* proc);

void process_scope(process_t* proc) {
	scope_t* sc = proc->iter.x->scope;

	item_descend(&proc->iter);
	process(proc);
	item_ascend(&proc->iter);

	int defers=0;
	item_t* scope_item = proc->iter.x;
	while (scope_item) {
		if (scope_item->scope->deferred.length) {
			defers=1; break;
		}

		scope_item=scope_get(scope_item);
	}

	if (!defers) return;

	//if exits are all same, no need for individual treatment
	//simply use gotos and place the exit at the end of the block after deferred labels
	int same=1;

	exit_t* ex=NULL;
	vector_iterator exit_iter = vector_iterate(&sc->exits);
	while (vector_next(&exit_iter)) {
		exit_t* prev_ex = ex;
		ex = exit_iter.x;
		if (!prev_ex) continue;

		if (!item_eq(proc->parser, ex->item, prev_ex->item)
					|| ex->exit_scope!=prev_ex->exit_scope) {
			same=0;
			break;
		}
	}

	if (same) {
		vector_iterator deferred_iter = vector_iterate_end(&sc->deferred);
		while (vector_prev(&deferred_iter)) {
			item_t* defer = *(item_t**)deferred_iter.x;

			char* label_str=NULL;

			exit_iter = vector_iterate(&sc->exits);
			while (vector_next(&exit_iter)) {
				exit_t* ex2 = exit_iter.x;

				if (ex2->defer_i!=deferred_iter.i+1) continue;

				if (!label_str) {
					item_t* label = item_push(proc, item_label, proc->iter.x, proc->iter.x);
					item_t* label_name = item_push(proc, item_name, label, label);

					label_str = heapstr("defer%u", deferred_iter.i);
					while (map_insertcpy_noexist(&proc->name_label, &label_str, &label).exists)
						label_str = straffix(label_str, "_");

					label_name->str=label_str;
				}

				unsigned ex_i = vector_search(&ex2->item->parent->body, &ex2->item);

				item_t* goto_item = item_new(proc, item_goto, ex2->item, ex2->item->parent);
				item_t* goto_label_name = item_push(proc, item_name, goto_item, goto_item);
				goto_label_name->str=label_str;

				vector_setcpy(&ex2->item->parent->body, ex_i, &goto_item);
			}

			vector_pushcpy(&proc->iter.x->body, &defer);
		}

		if (ex) {
			scope_item = proc->iter.x;
			while (scope_item!=ex->exit_scope) {
				deferred_iter = vector_iterate_end(&scope_item->scope->deferred);
				while (vector_prev(&deferred_iter)) {
					item_t* deferred = deferred_iter.x;
					vector_pushcpy(&proc->iter.x->body, &deferred);
				}

				scope_item=scope_get(scope_item);
			}

			vector_pushcpy(&proc->iter.x->body, &ex->item);
		}
	} else {
		if (!scope_exits_early(proc, proc->iter.x)) {
			vector_iterator deferred_iter = vector_iterate_end(&sc->deferred);
			while (vector_prev(&deferred_iter)) {
				item_t* defer = *(item_t**)deferred_iter.x;
				vector_pushcpy(&proc->iter.x->body, &defer);
			}
		}

		exit_iter = vector_iterate(&sc->exits);
		while (vector_next(&exit_iter)) {
			ex = exit_iter.x;
			//yes we have to do this every time considering all the varying modifications we are performing before each exit
			unsigned ex_i = vector_search(&ex->item->parent->body, &ex->item);

			scope_item = proc->iter.x;
			do {
				vector_iterator deferred_iter = vector_iterate_end(&scope_item->scope->deferred);
				while (vector_prev(&deferred_iter)) {
					item_t* deferred = deferred_iter.x;
					vector_insertcpy(&ex->item->parent->body, ex_i, &deferred);
				}

				scope_item=scope_get(scope_item);
			} while (scope_item!=ex->exit_scope);
		}
	}
}

void process(process_t* proc) {
	item_t* current = scope_get(item_parent(&proc->iter));

	while (item_next(&proc->iter)) {
		switch (proc->iter.x->ty) {
			case item_block:
			case item_func: {
				process_scope(proc);
				break;
			}

			case item_defer: {
				item_descend(&proc->iter);
				item_get(&proc->iter, 0);
				vector_pushcpy(&current->scope->deferred, &proc->iter.x);
				item_ascend(&proc->iter);

				item_remove(&proc->iter);

				break;
			}

			case item_ret: {
				item_t* scope_item = current;
				while (!scope_item->scope->ret) scope_item=scope_get(scope_item);

				vector_pushcpy(&current->scope->exits, &(exit_t){.item=proc->iter.x,
						.defer_i=current->scope->deferred.length, .exit_scope=scope_item});
				break;
			}

			case item_break: {
				item_t* scope_item = current;
				while (!scope_item->scope->br) {
					scope_item=scope_get(scope_item);
					if (!scope_item) {
						parser_error(proc->parser, proc->iter.x->span, "nothing to break to", 1);
						break;
					}
				}

				vector_pushcpy(&current->scope->exits, &(exit_t){.item=proc->iter.x,
						.defer_i=current->scope->deferred.length, .exit_scope=scope_item});
				break;
			}

			case item_goto: {
				item_descend(&proc->iter);
				item_t* label = *(item_t**)map_find(&proc->name_label, &(char*){item_str(proc->parser, item_get(&proc->iter, 0))});
				item_ascend(&proc->iter);

				if (!label) {
					parser_error(proc->parser, proc->iter.x->span, "label out of scope", 1);
					break;
				}

				//doesnt actually exit
				item_t* label_scope = scope_get(label);
				if (current==label_scope || item_child(current, label_scope)) break;

				vector_pushcpy(&current->scope->exits, &(exit_t){.item=proc->iter.x,
						.defer_i=current->scope->deferred.length, .exit_scope=label_scope});
				break;
			}

			default:;
		}
	}
}

process_t process_new(parser_t* parser)	{
	process_t proc = {.name_label=map_new(), .parser=parser,
			.iter=item_iterate(parser), .name_item=map_new(), .names=vector_new(sizeof(char*)),
			.objs=vector_new(sizeof(void*))};

	map_configure_string_key(&proc.name_label, sizeof(item_t*));
	map_configure_string_key(&proc.name_item, sizeof(item_t*));

	proc.name_label.free = free_string;
	proc.name_item.free = free_string;

	tag_items(&proc);
	item_restart(&proc.iter);
	process(&proc);

	return proc;
}
