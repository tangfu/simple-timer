CC:=gcc
FLAGS:= -g3 -Wall -Wextra 
LIBLDFLAGS:= -lpthread -lrt

ALL: 
	@-astyle -n --style=linux --mode=c --pad-oper --pad-paren-in --unpad-paren --break-blocks --delete-empty-lines --min-conditional-indent=0 --max-instatement-indent=80 --indent-col1-comments --indent-switches --lineend=linux *.{c,h} >/dev/null
		@$(CC) -c $(FLAGS) timer.c rbtree.c $(LIBLDFLAGS)
		@$(CC) -c $(FLAGS) minheap_timer.c $(LIBLDFLAGS)
		@ar -rc libtimer.a timer.o minheap_timer.o rbtree.o
#		@$(CC) timer.c -fPIC -shared -o libtimer.so
		@rm *.o
		@make -C example
		@make -C test
#produce document
		@doxygen

release:	
		@$(CC) -c -Wall -Wextra timer.c $(LIBLDFLAGS)
		@ar -rc libtimer.a timer.o
		@rm *.o

clean:
	@if [ -f libtimer.a ];then \
		rm libtimer.a test/test example/example 2>/dev/null; \
		rm -rf doc/html/* 2>/dev/null; \
	fi
