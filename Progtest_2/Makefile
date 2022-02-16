CXX=g++
CXXFLAGS=-std=c++11 -Wall -pedantic -g
LD=g++
LDFLAGS=-g -Wall -pedantic
LIBS=-lpthread


run: all
	./test1 && ./test2 && ./test3 && ./test4 && ./test5

runtest1: test1
	./test1 > test1.out

runtest2: test2
	./test2 > test2.out

runtest3: test3
	./test3 > test3.out

runtest4: test4
	./test4 > test4.out

runtest5: test5
	./test5 > test5.out


all: test1 test2 test3 test4 test5

test1: solution.o ccpu.o test_op.o test1.o
	$(LD) $(LDFLAGS) $^ -o $@ $(LIBS)

test2: solution.o ccpu.o test_op.o test2.o
	$(LD) $(LDFLAGS) $^ -o $@ $(LIBS)

test3: solution.o ccpu.o test_op.o test3.o
	$(LD) $(LDFLAGS) $^ -o $@ $(LIBS)

test4: solution.o ccpu.o test_op.o test4.o
	$(LD) $(LDFLAGS) $^ -o $@ $(LIBS)

test5: solution.o ccpu.o test_op.o test5.o
	$(LD) $(LDFLAGS) $^ -o $@ $(LIBS)
	
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<
	
clean:
	rm -f *.o test[1-5]

clear: clean
	rm -f core *.bak *~ *.o

ccpu.o: ccpu.cpp common.h
solution.o: solution.cpp common.h
test1.o: test1.cpp common.h test_op.h
test2.o: test2.cpp common.h test_op.h
test3.o: test3.cpp common.h test_op.h
test4.o: test4.cpp common.h test_op.h
test5.o: test5.cpp common.h test_op.h
test_op.o: test_op.cpp common.h test_op.h
