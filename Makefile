TOP_DIR		:= $(PWD)
OPESLEX		:= src
TEST_CL = docs\cl_examples
TEST_C  = docs\c_examples
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

test_cl:
	make -C $(TEST_CL)

true_test_c:
	make true_test -C $(TEST_C)

false_test_c:
	make -i false_test -C $(TEST_C)