# Dozen
Dozen is a high performance HTTP/1.1 server made completely in C. It supports only Linux for now.

## Benefits
- Mostly zero-copy; copying data to memory does not happen much
- Powered by liburing
- Compile-time and SIMD-based routing; routes are generated completely at compile-time, and URLs are hashed and matched via SIMD
- Compile-time responses; code for generating response packets is generated at compile-time
- Succinct and easy; you'll immediately fall in love with Dozen's syntax and ease-of-use

## Example
```c
#include <dozen/dozen.h>

dozen_rs index(dozen_rq *request) {
	dozen_respond (
		"Hello, World!", 200,
		("powered-by-dozen", "yes"),
		("fast-response-times", "also-yes")
	)
}

dozen_rs echo(dozen_rq *request) {
	dozen_respond(dozen_path_arg(request, "message"), 200)
}

int main() {
	return dozen_init("127.0.0.1", 3000);
}

dozen_routes (
	(dozen_get, "/", index),
	(dozen_get, "/echo/:message", echo)
)
```