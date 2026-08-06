__kernel void string_literals()
{
    printf("simple string");
    printf("quote: \" slash: \\ question: \?");
    printf("controls: \a\b\f\n\r\t\v");
    printf("octal: \101 hex: \x41 unicode: \u03A9");
    printf("continued \
string");

    // A closed single-line comment.
    /* A closed block comment. */
}
