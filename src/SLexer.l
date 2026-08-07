%option noyywrap
%option yylineno
%{
    #include "TextHandler.hpp"

    #define YY_INPUT(buf,result,max_size)  {\
    result = GetNextChar(buf, max_size); \
    if (result <= 0) \
      result = YY_NULL; \
    }

    extern int if_processing;
    extern int if_id;
    extern int if_type;
%}

%x STR
%x STR_RECOVERY
%x ML_COMMENT

DIGIT                           [0-9]
HEX_DIGIT                       [0-9a-fA-F]
INTEGER_SUFFIX                  ([uU]([lL]{1,2})?|[lL]{1,2}[uU]?)
DECIMAL_INTEGER                 ([1-9]{DIGIT}*|0){INTEGER_SUFFIX}?
OCTAL_INTEGER                   0[0-7]+{INTEGER_SUFFIX}?
HEXADECIMAL_INTEGER             0[xX]{HEX_DIGIT}+{INTEGER_SUFFIX}?
INTEGER_LITERAL                 ({HEXADECIMAL_INTEGER}|{OCTAL_INTEGER}|{DECIMAL_INTEGER})
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
CHARACTER_LITERAL               \'([^\\'\r\n]|\\([\'"?\\abfnrtv]|[0-7]{1,3}|x{HEX_DIGIT}+|u{HEX_DIGIT}{4}|U{HEX_DIGIT}{8}))+\'
PREPROCESSOR_DIRECTIVE          [ \t]*#[^\r\n]*(\\\r?\n[^\r\n]*)*

%%

<INITIAL>^{PREPROCESSOR_DIRECTIVE} {
                    BeginToken(yytext);
                    return ClassifyPreprocessorDirective(yytext);
                  }

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

if              {
                    if (UseLegacyOpaquePredicatePass())
                    {
                        if_processing = 1;
                    }
                    BeginToken(yytext);
                    return IF;
                  }
else            { BeginToken(yytext); return ELSE; }
for             { BeginToken(yytext); return FOR; }
while           { BeginToken(yytext); return WHILE; }
do              { BeginToken(yytext); return DO; }
switch          { BeginToken(yytext); return SWITCH; }
case            { BeginToken(yytext); return CASE; }
default         { BeginToken(yytext); return DEFAULT; }
return          { if_type = 1; BeginToken(yytext); return RETURN; }
print           { BeginToken(yytext); return PRINT; }
break           { BeginToken(yytext); return BREAK; }
continue        { BeginToken(yytext); return CONTINUE; }
sizeof          { BeginToken(yytext); return SIZEOF; }
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
enum            { if_type = 1; BeginToken(yytext); return ENUM; }
union           { if_type = 1; BeginToken(yytext); return UNION; }
struct          { if_type = 1; BeginToken(yytext); return STRUCT; }
volatile        { if_type = 1; BeginToken(yytext); return VOLATILE; }
restrict        { if_type = 1; BeginToken(yytext); return RESTRICT; }
(read_only|write_only|read_write|__read_only|__write_only|__read_write) {
                    if_type = 1; BeginToken(yytext); return ACCESS_QUAL;
                  }
(image1d_t|image1d_buffer_t|image1d_array_t|image2d_t|image2d_array_t|image3d_t|sampler_t|event_t|clk_event_t|queue_t|reserve_id_t) {
                    if_type = 1; BeginToken(yytext); return BUILTIN_TYPE;
                  }
(__global|global) { if_type = 1; BeginToken(yytext); return GLOBAL; }
(__local|local) { if_type = 1; BeginToken(yytext); return LOCAL; }
(__constant|constant) { if_type = 1; BeginToken(yytext); return CONSTANT; }
(__private|private) { if_type = 1; BeginToken(yytext); return PRIVATE; }
(__kernel|kernel) { if_type = 1; BeginToken(yytext); return KERNEL; }
(__attribute__|__attribute) { BeginToken(yytext); return ATTRIBUTE; }

"<:"            { BeginToken(yytext); return '['; }
":>"            { BeginToken(yytext); return ']'; }
"<%"            { BeginToken(yytext); return '{'; }
"%>"            { BeginToken(yytext); return '}'; }
"%:"            { BeginToken(yytext); return '#'; }
[?][?][(]       { BeginToken(yytext); return '['; }
[?][?][)]       { BeginToken(yytext); return ']'; }
[?][?][<]       { BeginToken(yytext); return '{'; }
[?][?][>]       { BeginToken(yytext); return '}'; }
[?][?][=]       { BeginToken(yytext); return '#'; }
[?][?][/]       { BeginToken(yytext); return '\\'; }
[?][?][']       { BeginToken(yytext); return '^'; }
[?][?][!]       { BeginToken(yytext); return '|'; }
[?][?][-]       { BeginToken(yytext); return '~'; }

"<<="           { BeginToken(yytext); return LEFT_IS; }
">>="           { BeginToken(yytext); return RIGHT_IS; }
"<<"            { BeginToken(yytext); return LEFT_SHIFT; }
">>"            { BeginToken(yytext); return RIGHT_SHIFT; }
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

{INTEGER_LITERAL} {
                  BeginToken(yytext);
                  return INTEGER_LITERAL;
                }

{CHARACTER_LITERAL} {
                  BeginToken(yytext);
                  return CHARACTER_LITERAL;
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
                    if (if_type != 0)
                    {
                        BeginToken(yytext);
                    }
                    if_type = 0;
                  }

\n                {
                    BeginToken(yytext);
                    if_type = 0;
                  }

"-="            { BeginToken(yytext); return MIN_IS; }
"*="            { BeginToken(yytext); return MUL_IS; }
"/="            { BeginToken(yytext); return DIV_IS; }
"&="            { BeginToken(yytext); return AND_IS; }
"|="            { BeginToken(yytext); return OR_IS; }
"%="            { BeginToken(yytext); return MOD_IS; }
"^="            { BeginToken(yytext); return XOR_IS; }
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
