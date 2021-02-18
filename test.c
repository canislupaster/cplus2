#include <stdio.h>

#define X(y) y

X(void) dothing() {}

int main(int argc, char** argv) {
	defer printf("the exit code is undefined btw");

	switch (1) {
		case 1:
		defer x=++(1/1)+(printf("hello")++);
		return;
		defer printf("hello");
		break;
	}

	dothing();
}

void xd() {}
