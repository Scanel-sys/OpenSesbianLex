#include "TextHandler.hpp"

#define true 1
#define false 0
#define PREAMBULA_SIZE 7
/*
 * global variable
 */
int debug = 0;

/*
    local vars
*/
static FILE *input_file;
static FILE *obfuscated_file;
static int eof = 0;
static int nRow = 0;
static int nBuffer = 0;
static int lBuffer = 0;
static int nTokenStart = 0;
static int nTokenLength = 0;
static int nTokenNextStart = 0;
static int lMaxBuffer = 1000;
static char *buffer;
static char *obf_buffer;
extern int yylineno;
int err = false;
int directives_ended = false;

static int getNextLine(void);

/*--------------------------------------------------------------------
 * dumpChar
 * 
 * printable version of a char
 *------------------------------------------------------------------*/
static
char dumpChar(char c) {
    if (  isprint(c)  )
        return c;
    return '@';
}

/*--------------------------------------------------------------------
 * dumpString
 * 
 * printable version of a string upto 100 character
 *------------------------------------------------------------------*/
static
char *dumpString(char *s) {
    static char buf[101];
    int i;
    int n = strlen(s);

    if (  n > 100  )
        n = 100;

    for (i=0; i<n; i++)
        buf[i] = dumpChar(s[i]);
    buf[i] = 0;
    return buf;
}

void PrintError(const char *errorstring, ...) {
    err = true;
    static char errmsg[10000];
    va_list args;

    int start = nTokenStart;
    int end=start + nTokenLength - 1;
    int i;

  /*================================================================*/
  /* a bit more complicate version ---------------------------------*/
/* */
    if (  eof  ) {
        fprintf(stdout, "...... !");
        for (i=0; i<lBuffer; i++)
        fprintf(stdout, ".");
        fprintf(stdout, "^-EOF\n");
    }
    else {
        fprintf(stdout, "...... !");
        if(start != 1)
        {
            for (i=0; i<start; i++)
            fprintf(stdout, ".");
        }
        for (i=start; i<=end; i++)
        fprintf(stdout, "^");
        
        fprintf(stdout, "\n");
    }
/* */

    /*================================================================*/
    /* print it using variable arguments -----------------------------*/
    va_start(args, errorstring);
    vsprintf(errmsg, errorstring, args);
    va_end(args);

    fprintf(stdout, "Error: %s at line %d\n", errmsg, yylineno);
    
    for (i = 1; i < 71; i++)
        fprintf(stdout, " "); 
    fprintf(stdout, "\n"); 
}

/*--------------------------------------------------------------------
 * DumpRow
 * 
 * dumps the contents of the current row
 *------------------------------------------------------------------*/
void DumpRow(void) 
{
    if (err)
        fprintf(stderr, "\nError(s) occured while parsing:\n\n");
    
    fprintf(stdout, "%6d |%.*s", nRow, lBuffer, buffer);
}

void ProcessObfuscation(char* token)
{
    snprintf(obf_buffer, lMaxBuffer, "%s", token);

    fprintf(obfuscated_file, "%s", obf_buffer);
}

void BeginToken(char *t) 
{
    ProcessObfuscation(t);
    printf_s("%s\n", t);

    /*================================================================*/
    /* remember last read token --------------------------------------*/
    nTokenStart = nTokenNextStart;
    nTokenLength = strlen(t);
    nTokenNextStart = nBuffer; // + 1;


    /*================================================================*/
    /* location for bison --------------------------------------------*/
    yylloc.first_line = nRow;
    yylloc.first_column = nTokenStart;
    yylloc.last_line = nRow;
    yylloc.last_column = nTokenStart + nTokenLength - 1;

    if (  debug  ) {
        printf("Token '%s' at %d:%d next at %d\n", dumpString(t),
                            yylloc.first_column,
                            yylloc.last_column, nTokenNextStart);
    }
}

void AppendObfBuffer(char* code_string)
{
    strcat(obf_buffer, code_string);
}

/*--------------------------------------------------------------------
* GetNextChar
* 
* reads a character from input for flex
*------------------------------------------------------------------*/
int GetNextChar(char *b, int maxBuffer) 
{
    int frc;

    /*================================================================*/
    /*----------------------------------------------------------------*/
    if (  eof  )
        return 0;

    /*================================================================*/
    /* read next line if at the end of the current -------------------*/
    while (  nBuffer >= lBuffer  ) {
        frc = getNextLine();
        if (  frc != 0  )
        return 0;
    }

    /*================================================================*/
    /* ok, return character ------------------------------------------*/
    b[0] = buffer[nBuffer];
    nBuffer += 1;

    if (  debug  )
        printf("GetNextChar() => '%c'0x%02x at %d\n",
                            dumpChar(b[0]), b[0], nBuffer);
    return b[0]==0?0:1;
}

/*--------------------------------------------------------------------
 * getNextLine
 * 
 * reads a line into the buffer
 *------------------------------------------------------------------*/
static
int getNextLine(void) {
    int i;
    char *p;
    
    /*================================================================*/
    /*----------------------------------------------------------------*/
    nBuffer = 0;
    nTokenStart = -1;
    nTokenNextStart = 1;
    eof = false;

    /*================================================================*/
    /* read a line ---------------------------------------------------*/
    p = fgets(buffer, lMaxBuffer, input_file);
    if (  p == NULL  ) {
        if (  ferror(input_file)  )
            return -1;
        eof = true;
        return 1;
    }

    nRow += 1;
    lBuffer = strlen(buffer);
    // ProcessObfuscation();                   // print all file lines

    /*================================================================*/
    return 0;
}


int main(int argc, char *argv[])
{
    char *infile_path = argv[1];
    input_file = fopen(infile_path, "r");
    obfuscated_file = fopen("obfuscated_result.cl", "w");

    buffer = (char*)malloc(lMaxBuffer);
    obf_buffer = (char*)malloc(lMaxBuffer);
    obf_buffer[0] = '\0';

    if (  buffer == NULL  ) {
        printf("cannot allocate %d bytes of memory\n", lMaxBuffer);
        fclose(input_file);
        fclose(obfuscated_file);
        return 1;
    }
    
    if (  getNextLine() == 0  )
        yyparse();
    
    free(buffer);
    free(obf_buffer);
    fclose(input_file);
    fclose(obfuscated_file);

    if(!err)
        printf("PASS\n");

    return err;
}