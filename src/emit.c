#include <stdio.h>

#include "syntax.h"

int emit_item(FILE* f, item_iterator_t* iter) {
	if (!item_next(iter)) return 0;

	switch (iter->x->ty) {
		case item_func: {
			item_descend(iter);
			emit_item(f, iter); //type
			emit_item(f, iter); //name

			fprintf(f, "(");

			for (int i=0; item_next(iter) && iter->x->ty==item_arg; i++) {
				if (i!=0) fprintf(f, ",");

				item_descend(iter);
				emit_item(f, iter);
				item_ascend(iter);
			}

			fprintf(f, ")");

			emit_item(f, iter); //block/body

			item_ascend(iter);
			break;
		}
		case item_block: {
			fprintf(f, "{");
			item_descend(iter);
			while (emit_item(f, iter));
			item_ascend(iter);
			fprintf(f, "}");
			break;
		}
		case item_type: {
			item_descend(iter);
			emit_item(f, iter); //name
			item_ascend(iter);
			break;
		}
		case item_union:
		case item_struct: {
			item_descend(iter);
			fprintf(f, iter->x->ty == item_struct ? "struct " : "union ");
			emit_item(f, iter); //name
			fprintf(f, "{");

			fprintf(f, "}");
			item_ascend(iter);
		}
	}
}

void emit(parser_t* parser) {
	item_iterator_t iter = item_iterate(parser);
	unsigned tok_loc = -1;
	while (item_next(&iter)) {
		if (iter.x->gen) {
			emit_item(f, &iter);
		} else {

		}
	}
}
