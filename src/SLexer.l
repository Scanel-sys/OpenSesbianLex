%option noyywrap
%option yylineno
%{
    #include "TextHandler.hpp"

    #define YY_INPUT(buf,result,max_size)  {\
    result = GetNextChar(buf, max_size); \
    if (result <= 0) \
      result = YY_NULL; \
    }

    extern int directives_ended;
    extern int if_processing;
    extern int if_id;
    extern int if_type;

    static int preprocessor_line = 0;
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
                    /* Line comments are intentionally omitted from output. */
                  }
<INITIAL>"/*"     {
                    /* A C comment is replaced by whitespace so adjacent
                       tokens cannot merge after obfuscation. */
                    BeginToken(" ");
                    BEGIN(ML_COMMENT);
                  }
<ML_COMMENT>"*/"  {
                    BEGIN(INITIAL);
                  }
<ML_COMMENT>\r?\n   {
                    /* Keep source line boundaries for directives,
                       diagnostics, and following tokens. */
                    BeginToken(yytext);
                  }
<ML_COMMENT>.     {
                  }
<ML_COMMENT><<EOF>> {
                    BeginToken(yytext);
                    BEGIN(INITIAL);
                    return INVALID_COMMENT;
                  }

if              { if_processing = 1; BeginToken(yytext); return IF; }
include         { preprocessor_line = 1; if_type = 1; BeginToken(yytext); return INCLUDE; }
define          { preprocessor_line = 1; if_type = 1; BeginToken(yytext); return DEFINE; }
ifdef           { preprocessor_line = 1; if_type = 1; BeginToken(yytext); return IFDEF; }
ifndef          { preprocessor_line = 1; if_type = 1; BeginToken(yytext); return IFNDEF; }
endif           { preprocessor_line = 1; if_type = 1; BeginToken(yytext); return ENDIF; }
else            { BeginToken(yytext); return ELSE; }
for             { BeginToken(yytext); return FOR; }
while           { BeginToken(yytext); return WHILE; }
do              { BeginToken(yytext); return DO; }
return          { if_type = 1; BeginToken(yytext); return RETURN; }
print           { BeginToken(yytext); return PRINT; }
break           { BeginToken(yytext); return BREAK; }
continue        { BeginToken(yytext); return CONTINUE; }
int             { if_type = 1; BeginToken(yytext); return INT; }
char            { if_type = 1; BeginToken(yytext); return CHAR; }
bool            { if_type = 1; BeginToken(yytext); return INT; }
uchar           { if_type = 1; BeginToken(yytext); return CHAR; }
(ushort|uint|ulong) { if_type = 1; BeginToken(yytext); return INT; }
float           { if_type = 1; BeginToken(yytext); return FLOAT; }
float(2|3|4|8|16) { if_type = 1; BeginToken(yytext); return FLOAT; }
half(2|3|4|8|16)? { if_type = 1; BeginToken(yytext); return FLOAT; }
double          { if_type = 1; BeginToken(yytext); return DOUBLE; }
double(2|3|4|8|16) { if_type = 1; BeginToken(yytext); return DOUBLE; }
char(2|3|4|8|16) { if_type = 1; BeginToken(yytext); return CHAR; }
(uchar|short|ushort|int|uint|long|ulong)(2|3|4|8|16) {
                    if_type = 1; BeginToken(yytext); return INT;
                  }
const           { if_type = 1; BeginToken(yytext); return CONST; }
unsigned        { if_type = 1; BeginToken(yytext); return UNSIGNED; }
size_t          { if_type = 1; BeginToken(yytext); return SIZE_T; }
long            { if_type = 1; BeginToken(yytext); return LONG; }
short           { if_type = 1; BeginToken(yytext); return SHORT; }
void            { if_type = 1; BeginToken(yytext); return VOID; }
typedef         { if_type = 1; BeginToken(yytext); return TYPEDEF; }
union           { if_type = 1; BeginToken(yytext); return UNION; }
struct          { if_type = 1; BeginToken(yytext); return STRUCT; }
__global        { if_type = 1; BeginToken(yytext); return GLOBAL; }
__local         { if_type = 1; BeginToken(yytext); return LOCAL; }
__constant      { if_type = 1; BeginToken(yytext); return CONSTANT; }
__private       { if_type = 1; BeginToken(yytext); return PRIVATE; }
__kernel        { if_type = 1; BeginToken(yytext); return KERNEL; }

==              { BeginToken(yytext); return EQ; }
"<="            { BeginToken(yytext); return LE; }
>=              { BeginToken(yytext); return GE; }
!=              { BeginToken(yytext); return NE; }
&&              { BeginToken(yytext); return AND; }
[|][|]          { BeginToken(yytext); return OR; }

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

<INITIAL>[\"]   {
                    BeginToken(yytext);
                    BEGIN(STR);
                  }

<STR>[\"]       {
                    BeginToken(yytext);
                    BEGIN(INITIAL);
                    return STRING;
                  }

<STR>[^\\\r\n\"]+ {
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

<STR_RECOVERY>[^\"\r\n]+ {
                    BeginToken(yytext);
                  }

<STR_RECOVERY>[\"] {
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

[ \r\t]         {
                    if (if_type != 0 || preprocessor_line != 0)
                    {
                        BeginToken(yytext);
                    }
                    if_type = 0;
                  }

\n                {
                    BeginToken(yytext);
                    if_type = 0;
                    preprocessor_line = 0;
                  }

"-="            { BeginToken(yytext); return MIN_IS; }
"*="            { BeginToken(yytext); return MUL_IS; }
"/="            { BeginToken(yytext); return DIV_IS; }
"&="            { BeginToken(yytext); return AND_IS; }
"|="            { BeginToken(yytext); return OR_IS; }
"%="            { BeginToken(yytext); return MOD_IS; }
"^="            { BeginToken(yytext); return XOR_IS; }
"~="            { BeginToken(yytext); return TILDA_IS; }
"+="            { BeginToken(yytext); return PLUS_IS; }
"++"            { BeginToken(yytext); return INCR; }
"--"            { BeginToken(yytext); return DECR; }
"->"            { BeginToken(yytext); return ARROW; }

[-{};()=<>+*/!,] {
                    BeginToken(yytext);
                    return *yytext;
                  }

[a-zA-Z_][a-zA-Z0-9_]* {
                    if_id = 1;
                    BeginToken(yytext);
                    yylval = (int)*yytext;
                    return ID;
                  }

.                 {
                    BeginToken(yytext);
                    return yytext[0];
                  }

%%
