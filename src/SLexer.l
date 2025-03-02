%option noyywrap
%option yylineno
%{
    #include "TextHandler.hpp"

    #define YY_INPUT(buf,result,max_size)  {\
    result = GetNextChar(buf, max_size); \
    if (  result <= 0  ) \
      result = YY_NULL; \
    }

    extern int directives_ended;
    extern int if_processing;
    extern int if_id;
    extern int if_type;

%}


%x STR
%x ML_COMMENT

%%

[/][/].*\n      {
                  // BeginToken(yytext);
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
if              { if_processing = true; BeginToken(yytext); return IF; }
include         { if_type = true; BeginToken(yytext); return INCLUDE; }
define          { if_type = true; BeginToken(yytext); return DEFINE; }
ifdef           { if_type = true; BeginToken(yytext); return IFDEF; }
ifndef          { if_type = true; BeginToken(yytext); return IFNDEF; }
endif           { if_type = true; BeginToken(yytext); return ENDIF; }
else            { BeginToken(yytext); return ELSE; }
for             { BeginToken(yytext); return FOR; }
while           { BeginToken(yytext); return WHILE; }
do              { BeginToken(yytext); return DO; }
return          { if_type = true; BeginToken(yytext); return RETURN; }
print           { BeginToken(yytext); return PRINT; }
int             { if_type = true; BeginToken(yytext); return INT; }
char            { if_type = true; BeginToken(yytext); return CHAR; }
float           { if_type = true; BeginToken(yytext); return FLOAT; }
float2          { if_type = true; BeginToken(yytext); return FLOAT; }
float3          { if_type = true; BeginToken(yytext); return FLOAT; }
float4          { if_type = true; BeginToken(yytext); return FLOAT; }
float8          { if_type = true; BeginToken(yytext); return FLOAT; }
float16         { if_type = true; BeginToken(yytext); return FLOAT; }
float32         { if_type = true; BeginToken(yytext); return FLOAT; }
double          { if_type = true; BeginToken(yytext); return DOUBLE; }
const           { if_type = true; BeginToken(yytext); return CONST; }
unsigned        { if_type = true; BeginToken(yytext); return UNSIGNED; }
long            { if_type = true; BeginToken(yytext); return LONG; }
short           { if_type = true; BeginToken(yytext); return SHORT; }
void            { if_type = true; BeginToken(yytext); return VOID; }
typedef         { if_type = true; BeginToken(yytext); return TYPEDEF; }
union           { if_type = true; BeginToken(yytext); return UNION; }
struct          { if_type = true; BeginToken(yytext); return STRUCT; }
__global        { if_type = true; BeginToken(yytext); return GLOBAL; }
__local         { if_type = true; BeginToken(yytext); return LOCAL; }
__constant      { if_type = true; BeginToken(yytext); return CONSTANT; }
__private       { if_type = true; BeginToken(yytext); return PRIVATE; }
__kernel        { if_type = true; BeginToken(yytext); return KERNEL; }
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

[ ]             {
                    if(if_type == true)
                    {
                        if_type = false;
                        BeginToken(yytext);
                    }
                }

[\r\t\n]          {
                        // if(directives_ended == false)
                        {
                            BeginToken(yytext);
                        }
                  }

([0-9]+[eE][+-]{0,1}[0-9]+)|([0-9]*"."[0-9]*([eE][+-]{0,1}[0-9]+)?)"f"? {
                  BeginToken(yytext); 
                  return FLOAT_NUM;
}


"size_t" {
            if_type = true; 
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
                          if_id = true;
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
