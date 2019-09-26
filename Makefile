ifeq ($(OS),Windows_NT)
  EXE := .exe
else
  EXE :=
endif

BIN1=Pak_Decrypt
BIN2=Lxr_Decrypt
SRC1=Pak_Decrypt.c util.c
SRC2=Lxr_Decrypt.c util.c puff.c
OBJ1=${SRC1:.c=.o}
DEP1=${SRC1:.c=.d}
OBJ2=${SRC2:.c=.o}
DEP2=${SRC2:.c=.d}

BIN=${BIN1}${EXE} ${BIN2}${EXE}
OBJ=${OBJ1} ${OBJ2}

CFLAGS=-std=c99 -pipe -fvisibility=hidden -Wall -Wextra -Werror -DNDEBUG -D_GNU_SOURCE -O2
LDFLAGS=-s

.PHONY: all clean

all: ${BIN}

clean:
	@${RM} ${BIN} ${OBJ} ${DEP}

${BIN1}${EXE}: ${OBJ1}
	@echo [L] $@
	@${CC} ${LDFLAGS} -o $@ $^

${BIN2}${EXE}: ${OBJ2}
	@echo [L] $@
	@${CC} ${LDFLAGS} -o $@ $^

%.o: %.c
	@echo [C] $<
	@${CC} ${CFLAGS} -MMD -c -o $@ $<

-include ${DEP}
