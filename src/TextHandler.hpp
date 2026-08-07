#pragma once

#define YYLTYPE_IS_DECLARED 1
typedef struct YYLTYPE
{
    int first_line;
    int first_column;
    int last_line;
    int last_column;
} YYLTYPE;

#include "SLexParser.tab.hpp"

int yyparse();
void yyerror(const char* message);

void DumpRow();
int GetNextChar(char* destination, int maxBuffer);
void BeginToken(const char* token);
void PrintError(const char* message);
