all:	OpenSLex

OpenSLex: clean
	win_bison -d -Wconflicts-sr -Wconflicts-rr SLexParser.ypp
	win_flex --wincompat SLexer.l
	g++ -I"C:\\ProgramData\\chocolatey\\lib\\winflexbison\\tools\\" lex.yy.c TextHandler.cpp SLexParser.tab.cpp -lm -o OpenSLex

clean:
	del SLexParser.tab.* lex.yy.c* OpenSLex.exe
