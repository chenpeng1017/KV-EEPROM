CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -O2

all: demo test

demo: demo.c eeprom_kv.c eeprom_kv.h
	$(CC) $(CFLAGS) demo.c eeprom_kv.c -o demo

test: test_eeprom_kv.c eeprom_kv.c eeprom_kv.h
	$(CC) $(CFLAGS) test_eeprom_kv.c eeprom_kv.c -o test_eeprom_kv

run-demo: demo
	./demo

run-test: test
	./test_eeprom_kv

clean:
	rm -f demo test_eeprom_kv
