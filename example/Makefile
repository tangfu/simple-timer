CC:=gcc
FLAGS:= -g3 -Wall -Wextra -I..
LIBLDFLAGS:= -L..
LIBS:=-ltimer -lpthread -lrt

ALL: 
	@-astyle --style=linux --mode=c --pad-oper --pad-paren-in --unpad-paren --break-blocks --delete-empty-lines --min-conditional-indent=0 --max-instatement-indent=80 --indent-col1-comments --indent-switches --lineend=linux *.c >/dev/null
		@$(CC) -o example $(FLAGS) example.c $(LIBLDFLAGS) $(LIBS)
