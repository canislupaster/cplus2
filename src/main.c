#include <stdio.h>

#include "parse.h"
#include "syntax.h"

int main(int argc, char** argv) {
	for (int i=1; i<argc; i++) {
		if (argv[i][0]=='-') break;
		parser_t p = parse_file(argv[i]);

		int stop=0;
		vector_iterator err_iter = vector_iterate(&p.errors);
		while (vector_next(&err_iter)) {
			parser_error_t* err = err_iter.x;
			parser_printerr(&p, err);
			if (err->stop) stop=1;
		}

		if (stop) return 0;

		print_item_tree(&p);

		process_t proc = process_new(&p);

		print_item_tree(&p);

	}

	return 0;
}
