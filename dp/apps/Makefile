CC = gcc
CFLAGS = -lpthread -lczmq -lzmq
DEPS = job_recv.h zhelpers.h
SRCS = job_recv.c
OBJS = $(SRCS:.c=.o)

%.o: %.c $(DEPS)
	$(CC) -c -o $@ $<

job_recv: $(OBJS)
	$(CC) -o $@ $^ $(CFLAGS)

.PHONY: clean
clean:
	-rm job_recv $(OBJS)