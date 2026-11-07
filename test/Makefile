# Desktop harnesses (macOS/Linux, needs libcurl). Not part of the 3DS build.
CFLAGS := -std=c11 -D_DEFAULT_SOURCE -D_DARWIN_C_SOURCE -Wall -Wextra -g -fsanitize=address,undefined
SRC := ../source
CORE := $(SRC)/ttx.c $(SRC)/render.c

all: render_test fetch_test app_test

render_test: render_test.c $(CORE)
	$(CC) $(CFLAGS) -o $@ $^

fetch_test: fetch_test.c $(SRC)/fetcher.c $(SRC)/ttx.c
	$(CC) $(CFLAGS) -o $@ $^ -lcurl

app_test: app_test.c $(SRC)/app.c $(SRC)/fetcher.c $(SRC)/remote.c $(CORE)
	$(CC) $(CFLAGS) -o $@ $^ -lcurl

clean:
	rm -rf render_test fetch_test app_test *.bmp *.dSYM

.PHONY: all clean
