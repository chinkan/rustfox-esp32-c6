# SPDX-License-Identifier: MIT
# Copyright (c) 2026 chinkan.ai

CC ?= gcc
CFLAGS := -std=c11 -Wall -Wextra -Werror -Wpedantic \
	-I../../components/rf_frames/include \
	-I../../components/rf_face/include \
	-I../../components/rf_talk/include

SRCS := ../../components/rf_frames/rf_frames.c \
	../../components/rf_face/rf_face.c \
	../../components/rf_talk/rf_talk.c \
	test_main.c

.PHONY: test clean

test:
	$(CC) $(CFLAGS) $(SRCS) -o /tmp/rustfox-esp32-c6-host-test
	/tmp/rustfox-esp32-c6-host-test

clean:
	rm -f /tmp/rustfox-esp32-c6-host-test
