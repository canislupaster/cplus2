// Automatically generated header.

#pragma once
#include <stdio.h>
#include <util.h>
#include "types.h"
void print_item(parser_t* parser, FILE* f, item_t* item);
char* item_str(parser_t* parser, item_t* item);
int item_eq(parser_t* parser, item_t* i1, item_t* i2);
int item_child(item_t* item, item_t* child);
void print_item_tree_rec(parser_t* parser, vector_t* items, int depth);
void print_item_tree(parser_t* parser);
typedef struct {
	parser_t* parser;
	vector_t stack; //item_t** within body
	item_t** x_ref;
	item_t* x;
} item_iterator_t;
item_iterator_t item_iterate(parser_t* parser);
void item_restart(item_iterator_t* iter);
item_t* item_parent(item_iterator_t* iter);
int item_next(item_iterator_t* iter);
void item_descend(item_iterator_t* iter);
void item_ascend(item_iterator_t* iter);
void item_remove(item_iterator_t* iter);
int item_until(item_iterator_t* iter, item_ty ty);
item_t* item_get(item_iterator_t* iter, unsigned i);
typedef struct {
	vector_t objs; //pointers to allocated objects (above)
	vector_t names;

	map_t name_label;
	map_t name_item;

	item_iterator_t iter;

	parser_t* parser;
} process_t;
item_t* scope_get(item_t* item);
scope_t* scope_new(process_t* proc);
void scope_exit(process_t* proc, scope_t* scope);
int expr_const(item_iterator_t* iter);
item_t* item_new(process_t* proc, item_ty ty, item_t* inherit, item_t* parent);
item_t* item_push(process_t* proc, item_ty ty, item_t* parent, item_t* inherit);
int scope_exits_early(process_t* proc, item_t* origin);
void tag_items(process_t* proc);
void process_scope(process_t* proc);
void process(process_t* proc);
process_t process_new(parser_t* parser);
