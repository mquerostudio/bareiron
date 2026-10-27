// Tiny assertion helpers for the C unit tests
#ifndef H_CHECK
#define H_CHECK

#include <stdio.h>
#include <stdlib.h>

static int check_failures = 0;

#define CHECK(cond) do { if (!(cond)) { \
  printf("  %s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); check_failures ++; } } while (0)

#define RUN(test) do { int before = check_failures; printf("%s\n", #test); test(); \
  if (check_failures != before) printf("  -> FAILED\n"); } while (0)

#define CHECK_DONE() (check_failures ? (printf("%d check(s) failed\n", check_failures), 1) : 0)

#endif
