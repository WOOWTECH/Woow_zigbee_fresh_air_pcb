# 主機端單元測試：make -C firmware/test
CORE = ../components/fa_core
CFLAGS = -std=c11 -Wall -Wextra -Werror -O1 -g -I$(CORE)/include -fsanitize=address,undefined
SRC = test_core.c $(wildcard $(CORE)/src/*.c)

test: test_core
	./test_core

test_core: $(SRC) $(wildcard $(CORE)/include/*.h) unity_lite.h
	$(CC) $(CFLAGS) -o $@ $(SRC)

clean:
	rm -f test_core
.PHONY: test clean
