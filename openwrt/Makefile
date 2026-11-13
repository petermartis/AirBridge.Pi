CC ?= cc
CFLAGS ?= -O2
CFLAGS += -std=c11 -Wall -Wextra -Werror

all: airbridge-lcd

airbridge-lcd: lcd.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<

test: airbridge-lcd
	python3 test_renderer.py ./airbridge-lcd
	sh -n telemetry.sh
	sh -n airbridge-lcd.init

clean:
	rm -f airbridge-lcd

.PHONY: all test clean
