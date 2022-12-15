help: print_help_messages

# CXX = g++ # gcc version 9.4.0
CXXFLAGS = -g -Wall -Wextra -pthread
SERIAL = h4_problem1_serial
PARALLEL = h4_problem1

s: $(SERIAL).cpp
	@make PROGRAM=$(SERIAL) serial --no-print-directory

p: $(PARALLEL).cpp
	@make parallel PROGRAM=$(PARALLEL) --no-print-directory

test: test.cpp
	@make parallel PROGRAM=test --no-print-directory

serial: $(PROGRAM).cpp
	@$(CXX) $(CXXFLAGS) $(PROGRAM).cpp -o $(PROGRAM)
	@./$(PROGRAM)
	@$(RM) $(PROGRAM)

parallel: ${PROGRAM}.cpp
	@$(CXX) $(CXXFLAGS) $(PROGRAM).cpp -o $(PROGRAM)
	@./$(PROGRAM)
	@$(RM) $(PROGRAM)

print_help_messages:
	@cat makefile_help_messages.txt