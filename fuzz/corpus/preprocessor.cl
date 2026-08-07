#define SELECT(value, ...) ((value) ? (__VA_ARGS__) : 0)
#if defined(SELECT)
int value = SELECT(1, 2);
#endif
