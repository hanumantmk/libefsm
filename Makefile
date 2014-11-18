efsm_test : Makefile tests/efsm_test.c src/efsm.h src/efsm_internal.h src/libefsm.c src/utlist.h src/utstring.h
	gcc $(CFLAGS) -Isrc tests/efsm_test.c src/libefsm.c -o efsm_test
