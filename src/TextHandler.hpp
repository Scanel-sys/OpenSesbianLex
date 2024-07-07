#ifndef TEXTHANDLER_H
#define TEXTHANDLER_H

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
#include <string>
#include <cstring>
#include <memory.h>
#include <stdlib.h>
#include <stdarg.h>
#include <float.h>
#include "SLexParser.tab.hpp"

/*
 * lex & parse
 */
extern int yylex(void);
extern int yyparse(void);
extern void yyerror(char*);


extern void DumpRow(void);
extern void PrintSeparateLine(std::string msg);
extern int GetNextChar(char *b, int maxBuffer);
extern void BeginToken(char*);
extern void PrintError(const char *s, ...);

#endif
