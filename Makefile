ifeq ($(OS),Windows_NT)
  EXE := .exe
else
  EXE :=
endif

BIN=Pak_Decrypt${EXE}
SRC=Pak_Decrypt.c
OBJ=${SRC:.c=.o}
DEP=${SRC:.c=.d}

CFLAGS=-std=c99 -pipe -fvisibility=hidden -Wall -Wextra -Werror -DNDEBUG -D_GNU_SOURCE -O2
LDFLAGS=-s

.PHONY: all clean

all: ${BIN}

clean:
	@${RM} ${BIN} ${OBJ} ${DEP}

${BIN}: ${OBJ}
	@echo [L] $@
	@${CC} ${LDFLAGS} -o $@ $^

%.o: %.c
	@echo [C] $<
	@${CC} ${CFLAGS} -MMD -c -o $@ $<

-include ${DEP}
