#include <dozen/dozen.h>
#include <stdio.h>
#include <signal.h>
#include <unistd.h>

int main(void) {
	 dozen_init("127.0.0.1", 3000);
}