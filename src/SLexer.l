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
%x STR_RECOVERY
%x ML_COMMENT

DIGIT                           [0-9]
HEX_DIGIT                       [0-9a-fA-F]
DECIMAL_EXPONENT                [eE][+-]?{DIGIT}+
BINARY_EXPONENT                 [pP][+-]?{DIGIT}+
FLOAT_SUFFIX                    [fFlLhH]
DECIMAL_FRACTION                ({DIGIT}+\.{DIGIT}*|\.{DIGIT}+)
HEXADECIMAL_FRACTION            ({HEX_DIGIT}+\.{HEX_DIGIT}*|\.{HEX_DIGIT}+)
DECIMAL_FLOAT                   ({DECIMAL_FRACTION}({DECIMAL_EXPONENT})?|{DIGIT}+{DECIMAL_EXPONENT}){FLOAT_SUFFIX}?
HEXADECIMAL_FLOAT               0[xX]({HEXADECIMAL_FRACTION}{BINARY_EXPONENT}|{HEX_DIGIT}+{BINARY_EXPONENT}){FLOAT_SUFFIX}?
MALFORMED_DECIMAL_EXPONENT      ({DECIMAL_FRACTION}|{DIGIT}+)[eE][+-]?
MALFORMED_HEXADECIMAL_EXPONENT  0[xX]({HEXADECIMAL_FRACTION}|{HEX_DIGIT}+)[pP][+-]?
MALFORMED_HEXADECIMAL_FRACTION  0[xX]{HEXADECIMAL_FRACTION}
MALFORMED_FLOAT                 ({MALFORMED_DECIMAL_EXPONENT}|{MALFORMED_HEXADECIMAL_EXPONENT}|{MALFORMED_HEXADECIMAL_FRACTION})

%%

<INITIAL>"//"[^\r\n]* {
                    BeginToken(yytext);
                  }
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
<ML_COMMENT><<EOF>> {
                    BeginToken(yytext);
                    BEGIN(INITIAL);
                    return INVALID_COMMENT;
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
float2           { BeginToken(yytext); return FLOAT; }
float3           { BeginToken(yytext); return FLOAT; }
float4           { BeginToken(yytext); return FLOAT; }
float8           { BeginToken(yytext); return FLOAT; }
float16           { BeginToken(yytext); return FLOAT; }
float32           { BeginToken(yytext); return FLOAT; }
double          { BeginToken(yytext); return DOUBLE; }
const           { BeginToken(yytext); return CONST; }
unsigned  { BeginToken(yytext); return UNSIGNED; }
long      { BeginToken(yytext); return LONG; }
short     { BeginToken(yytext); return SHORT; }
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

{HEXADECIMAL_FLOAT}|{DECIMAL_FLOAT} {
                  BeginToken(yytext);
                  return FLOAT_NUM;
                }

{MALFORMED_FLOAT} {
                  BeginToken(yytext);
                  return INVALID_FLOAT;
                }

{DIGIT}         {
                  BeginToken(yytext);
                  yylval = (int)*yytext;
                  return DIGIT;
                }
                
<INITIAL>["]     {
                    BeginToken(yytext);
                    BEGIN(STR);
                  }

<STR>["]         {
                    BeginToken(yytext);
                    BEGIN(INITIAL);
                    return STRING;
                  }

<STR>[^\\\r\n"]+ {
                    BeginToken(yytext);
                  }

<STR>\\\r?\n     {
                    BeginToken(yytext);
                  }

<STR>\\[0-7]{1,3} {
                    BeginToken(yytext);
                  }

<STR>\\x{HEX_DIGIT}+ {
                    BeginToken(yytext);
                  }

<STR>\\u{HEX_DIGIT}{4} {
                    BeginToken(yytext);
                  }

<STR>\\U{HEX_DIGIT}{8} {
                    BeginToken(yytext);
                  }

<STR>\\.         {
                    BeginToken(yytext);
                    switch (yytext[1]) {
                      case '\'': case '"': case '?': case '\\':
                      case 'a': case 'b': case 'f': case 'n':
                      case 'r': case 't': case 'v':
                        break;
                      default:
                        BEGIN(STR_RECOVERY);
                        return INVALID_STRING;
                    }
                  }

<STR>\\          {
                    BeginToken(yytext);
                    BEGIN(STR_RECOVERY);
                    return INVALID_STRING;
                  }

<STR>\r?\n       {
                    BeginToken(yytext);
                    BEGIN(INITIAL);
                    return INVALID_STRING;
                  }

<STR><<EOF>>      {
                    BeginToken(yytext);
                    BEGIN(INITIAL);
                    return INVALID_STRING;
                  }

<STR_RECOVERY>[^"\r\n]+ {
                    BeginToken(yytext);
                  }

<STR_RECOVERY>["] {
                    BeginToken(yytext);
                    BEGIN(INITIAL);
                  }

<STR_RECOVERY>\r?\n {
                    BeginToken(yytext);
                    BEGIN(INITIAL);
                  }

<STR_RECOVERY><<EOF>> {
                    BEGIN(INITIAL);
                    return 0;
                  }

[ \t\r\n]       BeginToken(yytext); // whitespace

"size_t" {
          BeginToken(yytext); 
          return SIZE_T;
}

"-=" {
      BeginToken(yytext); 
      return MIN_IS;
}
"*=" {
      BeginToken(yytext); 
      return MUL_IS;
}

"/=" {
      BeginToken(yytext); 
      return DIV_IS; 
}

"&=" {
      BeginToken(yytext); 
      return AND_IS;
}

"|=" {
      BeginToken(yytext); 
      return OR_IS;
}

"%=" {
      BeginToken(yytext); 
      return MOD_IS;
}

"^=" {
      BeginToken(yytext); 
      return XOR_IS;
}

"~=" {
      BeginToken(yytext); 
      return TILDA_IS;
}

"+="  {
        BeginToken(yytext); 
        return PLUS_IS;
      }

"++"  {
        BeginToken(yytext); 
        return INCR;
      }

"--"  {
        BeginToken(yytext); 
        return DECR;
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

[a-zA-Z_][a-zA-Z0-9_]*"."[a-zA-Z0-9_]+  {
                          BeginToken(yytext); 
                          yylval = (int)*yytext;
                          return STRUCT_ID;
                        }

.               {
                  BeginToken(yytext);
                  return yytext[0];
                }

%%
