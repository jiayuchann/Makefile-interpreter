CC = gcc

CFLAGS += -std=gnu11
CFLAGS += -Wall -Werror

main: fake.c
	${CC} ${CFLAGS} -ggdb3 -pthread -o fake fake.c