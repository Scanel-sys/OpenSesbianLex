#include "TextHandler.hpp"

#include <vector>
#include <string>
#include <map>
#include <iostream>
#include <ctime>
#include <algorithm>

#define true 1
#define false 0
#define PREAMBULA_SIZE 7
#define lMaxBuffer      1000
#define MIN_NAME_SIZE   7
#define MAX_NAME_SIZE   1024
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

static char *buffer;
static char *temp_buffer;
static char *obf_buffer;

extern int yylineno;
int err = false;
int directives_ended = false;
int if_processing = false;
int if_id = false;
int if_type = false;

std::string last_token;
std::string act_token;

std::map<std::string, int> ids_dict;
std::map<std::string, std::string> ids_dict_with_new_names;

static int getNextLine(void);


class BracesQueue{

public:
    void push() {braces++;}
    int pop() 
    { 
        if(braces == 1)
        {
            braces--;
            return 0;
        }
        else if(braces > 1)
        {
            braces--;
            return 1;
        }

        return -1;
    }
private:
    size_t braces = 0;
};

class Obfuscator{

public:

    void push_curly_brace(){ braces_queue_.push(); }
    int  pop_curly_brace(){ return braces_queue_.pop(); }

    void print_to_file(FILE * file)
    {
        for(size_t i = 0; i < output_tokens_.size(); i++)
        {
            fprintf(obfuscated_file, "%s", output_tokens_[i].data());
        }
        output_tokens_.clear();
    }

    void push_first_expression_token(char* token)   { first_expression_tokens_.push_back(token); }
    void push_first_expression_token(std::string token)         { first_expression_tokens_.push_back(token); }

    void push_expression_token(char* token)         { expression_tokens_.push_back(token); }
    void push_expression_token(std::string token)   { expression_tokens_.push_back(token); }

    void push_temp_token(char* token)               { temp_tokens_.push_back(token); }
    void push_temp_token(std::string token)         { temp_tokens_.push_back(token); }

    void push_body_token(char* token)               { body_tokens_.push_back(token); }
    void push_body_token(std::string token)         { body_tokens_.push_back(token); }

    void push_output_token(char* token)             { output_tokens_.push_back(token); }
    void push_output_token(std::string token)       { output_tokens_.push_back(token); }
                                                    

    size_t get_first_expression_tokens_size()  { return first_expression_tokens_.size(); }
    size_t get_expression_tokens_size()        { return expression_tokens_.size(); }
    size_t get_temp_tokens_size()              { return temp_tokens_.size(); }
    size_t get_body_tokens_size()              { return body_tokens_.size(); }
    size_t get_output_tokens_size()            { return output_tokens_.size(); }

    void obfuscate_output_tokens()
    {
        for(size_t i = 0; i < get_output_tokens_size(); i++)
        {
            if(output_tokens_[i] == "[" && i % 2 == 0)
                output_tokens_[i].assign("<:");
            
            else if(output_tokens_[i] == "[" && i % 2 == 1)
                output_tokens_[i].assign("??(");

            else if(output_tokens_[i] == "]" && i % 2 == 0)
                output_tokens_[i].assign(":>");
            
            else if(output_tokens_[i] == "]" && i % 2 == 1)
                output_tokens_[i].assign("??)");

            else if(output_tokens_[i] == "{" && i % 2 == 0)
                output_tokens_[i].assign("<%");

            else if(output_tokens_[i] == "{" && i % 2 == 1)
                output_tokens_[i].assign("??<");

            else if(output_tokens_[i] == "}" && i % 2 == 0)
                output_tokens_[i].assign("%>");

            else if(output_tokens_[i] == "}" && i % 2 == 1)
                output_tokens_[i].assign("??>");

            else if(output_tokens_[i] == "#" && i % 2 == 0)
                output_tokens_[i].assign("%:");

            else if(output_tokens_[i] == "#" && i % 2 == 1)
                output_tokens_[i].assign("??=");
            
            else if(output_tokens_[i] == "\\")
                output_tokens_[i].assign("??/");

            else if(output_tokens_[i] == "^")
                output_tokens_[i].assign("??'");

            else if(output_tokens_[i] == "|")
                output_tokens_[i].assign("??!");

            else if(output_tokens_[i] == "~")
                output_tokens_[i].assign("??-");
            
        }
    }

    void push_tokens(std::vector<std::string>& dest, std::vector<std::string>& source, size_t start, size_t cnt)
    {
        for(size_t i = start; i < start + cnt; i++)
        {
            dest.push_back(source[i]);
        }
    }

    void push_tokens(std::vector<std::string>& dest, std::vector<std::string> source, size_t cnt)
    {
        for(size_t i = 0; i < cnt; i++)
        {
            dest.push_back(source[i]);
        }
    }

    void push_tokens(std::vector<std::string>& dest, std::vector<std::string>& source)
    {
        for(size_t i = 0; i < source.size(); i++)
            dest.push_back(source[i]);
    }

    std::vector<std::string>& get_first_expression_tokens() { return first_expression_tokens_; }
    std::vector<std::string>& get_expression_tokens()       { return expression_tokens_; }
    std::vector<std::string>& get_body_tokens()             { return body_tokens_; }
    std::vector<std::string>& get_temp_tokens()             { return temp_tokens_; }
    std::vector<std::string>& get_output_tokens()           { return output_tokens_; }

    void clear_first_expression_tokens()    { first_expression_tokens_.clear(); }
    void clear_expression_tokens()          { expression_tokens_.clear(); }
    void clear_body_tokens()                { body_tokens_.clear(); }
    void clear_temp_tokens()                { temp_tokens_.clear(); }
    void clear_output_tokens()              { output_tokens_.clear(); }

    
    void ProcessToken(char* token)
    {
        bool first_expression_parsing = false;

        if(if_processing == true)
        {
            push_temp_token(token);
            push_expression_token(token);

            if(if_expr_start_pos_ == 0 && token[0] == '(')
            {
                if_expr_start_pos_ = get_temp_tokens_size();
            }

            if(if_expr_end_pos_ == 0 && token[0] == ')')
            {
                if_expr_end_pos_ = get_temp_tokens_size() - 1;
            }
            
            if(if_body_start_pos_ == 0 && token[0] == '{')
            {
                if_body_start_pos_ = get_temp_tokens_size();
            }

            first_expression_parsing = (get_temp_tokens_size() != if_expr_start_pos_ && if_expr_start_pos_ != 0 && if_expr_end_pos_ == 0);

            if(first_expression_parsing == true)
            {
                push_first_expression_token(token);
            }

            if(strcmp(token, "{") == 0)
            {
                push_curly_brace();
            }
            else if(strcmp(token, "}") == 0 && pop_curly_brace() == 0)
            {
                if_processing = false;
            }
        }

        if(get_temp_tokens_size() != 0 && if_processing == false)
        {
            push_tokens(body_tokens_, temp_tokens_, if_body_start_pos_, get_temp_tokens_size() - if_body_start_pos_ - 1);

            push_tokens(output_tokens_, temp_tokens_, if_body_start_pos_);
            make_dead_end();
            push_tokens(output_tokens_, body_tokens_, get_body_tokens_size());
            push_output_token("}");

            clear_temp_tokens();
            clear_expression_tokens();
            clear_body_tokens();

            if_expr_start_pos_ = if_body_start_pos_ = 0;
            if_expr_end_pos_ = 0;
            clear_first_expression_tokens();
            clear_expression_tokens();
        }
        else if(get_temp_tokens_size() == 0)
        {
            push_output_token(token);
        }
    }

    void ObfuscateIds()
    {
        for (auto i : ids_dict)
        {
            std::string new_name;
            std::string old_name = i.first;
            do
            {
                new_name = GetRandomName((MIN_NAME_SIZE + i.first.size()) % MAX_NAME_SIZE);
            } while (ids_dict.find(new_name) != ids_dict.end());

            ids_dict_with_new_names.insert({old_name, new_name});
        }

        for (auto id : ids_dict_with_new_names)
        {
            std::replace(output_tokens_.begin(), output_tokens_.end(), id.first, id.second);
        }
    }

private:

    std::vector<std::string> first_expression_tokens_;
    std::vector<std::string> expression_tokens_;
    std::vector<std::string> temp_tokens_;
    std::vector<std::string> body_tokens_;
    std::vector<std::string> output_tokens_;

    class BracesQueue braces_queue_;

    unsigned int if_body_start_pos_ = 0;
    unsigned int if_expr_start_pos_ = 0;
    unsigned int if_expr_end_pos_   = 0;

    void make_dead_end()
    {
        push_tokens(output_tokens_, temp_tokens_, if_expr_start_pos_);
        push_output_token("!(");
        ObfuscateFirstExpression();
        push_output_token("))");
        push_output_token("{");
        ObfuscateFullBlock();
        push_output_token("}");
    }

    void ObfuscateFirstExpression()
    {
        for(size_t i = 0; i < get_first_expression_tokens_size(); i++)
        {
            push_output_token(first_expression_tokens_[i]);
        }
    }

    void ObfuscateFullBlock()
    {
        for(size_t i = 0; i < get_expression_tokens_size(); i++)
        {
            if(expression_tokens_[i] == "||")
            {
                push_output_token("&&");
            }
            else if(expression_tokens_[i] == "&&")
            {
                push_output_token("||");
            }
            else if(expression_tokens_[i] == "|")
            {
                push_output_token("^");
            }
            else if(expression_tokens_[i] == "&")
            {
                push_output_token("|");
            }
            else if(expression_tokens_[i] == "+")
            {
                push_output_token("*");
            }
            else if(expression_tokens_[i] == "-")
            {
                push_output_token("+");
            }
            else if(expression_tokens_[i] == "%")
            {
                push_output_token("/");
            }
            else if(expression_tokens_[i] == ">=")
            {
                push_output_token("<");
            }
            else if(expression_tokens_[i] == "<=")
            {
                push_output_token(">");
            }
            else if(expression_tokens_[i] == "<")
            {
                push_output_token(">=");
            }
            else if(expression_tokens_[i] == ">")
            {
                push_output_token("=<");
            }
            else if(expression_tokens_[i] == "++")
            {
                push_output_token("--");
            }
            else if(expression_tokens_[i] == "--")
            {
                push_output_token("++");
            }
            else
            {
                push_output_token(expression_tokens_[i]);
            }
        }
    }

    std::string GetRandomName(const int len) 
    {
        static const char alphanum[] =
            "0123456789"
            "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
            "abcdefghijklmnopqrstuvwxyz";

        static const char alphanum_no_ciphs[] =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
            "abcdefghijklmnopqrstuvwxyz";

        std::string tmp_s;
        tmp_s.reserve(len);

        for (int i = 0; i < len; ++i) 
        {
            if(i != 0)
                tmp_s += alphanum[rand() % (sizeof(alphanum) - 1)];
            else
                tmp_s += alphanum_no_ciphs[rand() % (sizeof(alphanum_no_ciphs) - 1)];
        }
        
        return tmp_s;
    }
};


Obfuscator obfuscator;


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



void BeginToken(char *t) 
{
    act_token = t;
    if(if_id == true)
    {
        last_token = t;
        if_id = false;
    }
    else if(act_token != "(" && !last_token.empty())
    {
        ids_dict.insert({last_token, 1});
        last_token.clear();
    }
    else
    {
        last_token.clear();
    }
    obfuscator.ProcessToken(t);

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

    /*================================================================*/
    return 0;
}


int main(int argc, char *argv[])
{
    char *infile_path = argv[1];
    input_file = fopen(infile_path, "r");
    obfuscated_file = fopen("obfuscated_result.cl", "w");

    buffer = (char*)malloc(lMaxBuffer);
    temp_buffer = (char*)malloc(lMaxBuffer);
    obf_buffer = (char*)malloc(lMaxBuffer);
    temp_buffer[0] = obf_buffer[0] = '\0';

    if (  buffer == NULL  ) {
        printf("cannot allocate %d bytes of memory\n", lMaxBuffer);
        fclose(input_file);
        fclose(obfuscated_file);
        return 1;
    }
    
    if (  getNextLine() == 0  )
        yyparse();

    obfuscator.ObfuscateIds();
    obfuscator.print_to_file(obfuscated_file);
    
    for (auto i : ids_dict)
        std::cout << i.first << '\n';

    free(buffer);
    free(obf_buffer);
    free(temp_buffer);
    fclose(input_file);
    fclose(obfuscated_file);

    if(!err)
        printf("PASS\n");

    return err;
}