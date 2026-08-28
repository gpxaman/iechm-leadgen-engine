# Build with: make
# Then run:   ./iechm_server [port]   or   ./iechm_demo [num_days]
#
# gnu11 (rather than plain c11) so glibc exposes POSIX + Linux extensions
# (readlink, realpath, gmtime_r, pthread, MSG_NOSIGNAL) without having to
# hand-pick individual _POSIX_C_SOURCE/_DEFAULT_SOURCE feature-test macros --
# there's no compiler available in the environment this was written in to
# verify a stricter set wouldn't silently hide something needed.
CC = cc
CFLAGS = -std=gnu11 -Wall -Wextra -O2
LDFLAGS = -lpthread -lm

SRC_DIR = src
COMMON_SRCS = $(filter-out $(SRC_DIR)/main.c $(SRC_DIR)/run_demo.c, $(wildcard $(SRC_DIR)/*.c))
COMMON_OBJS = $(COMMON_SRCS:.c=.o)

.PHONY: all clean

all: iechm_server iechm_demo

iechm_server: $(COMMON_OBJS) $(SRC_DIR)/main.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

iechm_demo: $(COMMON_OBJS) $(SRC_DIR)/run_demo.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(SRC_DIR)/*.o iechm_server iechm_demo
