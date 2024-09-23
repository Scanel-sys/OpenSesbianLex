TOP_DIR		:= $(PWD)
OPESLEX		:= src
TARGET = OpenSLex.exe

re_slex: clean build
	
clean:
	make -C $(OPESLEX) clean
	del /f /s /q build 1>nul
	rmdir build

build: TARGET
	mkdir build
	copy $(OPESLEX)\$(TARGET) build\$(TARGET)
	

TARGET:
	make -C $(OPESLEX)