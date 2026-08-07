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
int yylex();
void yyerror(const char* message);

void DumpRow();
int GetNextChar(char* destination, int maxBuffer);
void BeginToken(const char* token);
int ClassifyPreprocessorDirective(const char* directive);
void PrintError(const char* message);
bool UseLegacyOpaquePredicatePass();
void ResetOpenSLexFrontendForFuzzing();
