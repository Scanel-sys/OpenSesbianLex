%option noyywrap
%option yylineno
%{
    #include "TextHandler.hpp"

    #define YY_INPUT(buf,result,max_size)  {\
    result = GetNextChar(buf, max_size); \
    if (  result <= 0  ) \
      result = YY_NULL; \
    }
%}


%x STR
%x ML_COMMENT

%%

[/][/].*\n      {
                  BeginToken(yytext);
                } // comment
<INITIAL>"/*"     {
                    BeginToken(yytext); 
                    BEGIN(ML_COMMENT);
                  }
<ML_COMMENT>"*/"  {                
                    BeginToken(yytext); 
                    BEGIN(INITIAL);
                  }

<ML_COMMENT>"\n"  {                
                    BeginToken(yytext); 
                  }
<ML_COMMENT>.     {                
                    BeginToken(yytext); 
                  }
if              { BeginToken(yytext); return IF; }
include         { BeginToken(yytext); return INCLUDE; }
define          { BeginToken(yytext); return DEFINE; }
ifdef           { BeginToken(yytext); return IFDEF; }
ifndef          { BeginToken(yytext); return IFNDEF; }
endif           { BeginToken(yytext); return ENDIF; }
else            { BeginToken(yytext); return ELSE; }
for             { BeginToken(yytext); return FOR; }
while           { BeginToken(yytext); return WHILE; }
do              { BeginToken(yytext); return DO; }
return          { BeginToken(yytext); return RETURN; }
print           { BeginToken(yytext); return PRINT; }
int             { BeginToken(yytext); return INT; }
char            { BeginToken(yytext); return CHAR; }
float           { BeginToken(yytext); return FLOAT; }
double          { BeginToken(yytext); return DOUBLE; }
const           { BeginToken(yytext); return CONST; }
void            { BeginToken(yytext); return VOID; }
typedef         { BeginToken(yytext); return TYPEDEF; }
union           { BeginToken(yytext); return UNION; }
struct          { BeginToken(yytext); return STRUCT; }
__global        { BeginToken(yytext); return GLOBAL; }
__local         { BeginToken(yytext); return LOCAL; }
__constant      { BeginToken(yytext); return CONSTANT; }
__private       { BeginToken(yytext); return PRIVATE; }
__kernel        { BeginToken(yytext); return KERNEL; }
==              { BeginToken(yytext); return EQ; }
"<="            { BeginToken(yytext); return LE; }
>=              { BeginToken(yytext); return GE; }
!=              { BeginToken(yytext); return NE; }
&&              { BeginToken(yytext); return AND; }
[|][|]          { BeginToken(yytext); return OR;  }

[0-9]           { 
                  BeginToken(yytext);
                  yylval = (int)*yytext;
                  return DIGIT;
                }
                
["]             { 
                  BeginToken(yytext);
                  yylval = (int)*""; 
                  BEGIN(STR); 
                }

<STR>["]        { 
                  BeginToken(yytext);
                  BEGIN(INITIAL); 
                  return STRING; 
                  }

<STR>[^\\\n"]+  {
                  BeginToken(yytext);
                  yylval += (int)*yytext;
}

<STR>\\n        {
                  BeginToken(yytext);
                  yylval += '\n';
}
<STR>\\["]      {
                  BeginToken(yytext);
                  yylval += '"';
}

<STR>\\         {
                  BeginToken(yytext);
                }

<STR>\n         {
                  BeginToken(yytext);
                }

[ \t\r\n]       BeginToken(yytext); // whitespace

[eE][+-]{0,1}[0-9]+ {
                  BeginToken(yytext); 
                  return FLOAT_EXP;
}

"."             {
                  BeginToken(yytext);
                  return DOT;
}

[-{};()=<>+*/!,] { 
                    BeginToken(yytext);
                    return *yytext; 
                  }
[a-zA-Z_][a-zA-Z0-9_]*  {
                          BeginToken(yytext); 
                          yylval = (int)*yytext;
                          return ID;
                        }

.               {
                  BeginToken(yytext);
                  return yytext[0];
                }

%%
