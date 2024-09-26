#define YYLTYPE_IS_DECLARED
typedef struct YYLTYPE
{
  int first_line;
  int first_column;
  int last_line;
  int last_column;
} YYLTYPE;

#include <stdio.h>
#include <math.h>
#include <ctype.h>
#include <string.h>
#include <memory.h>
#include <stdlib.h>
#include <stdarg.h>
#include <float.h>
#include "SLexParser.tab.hpp"

extern int yyparse(void);
extern void yyerror(char*);

void DumpRow(void);
int GetNextChar(char *b, int maxBuffer);
void BeginToken(char*);
void PrintError(const char *s, ...);