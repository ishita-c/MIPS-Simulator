#ifndef MAIN_H
#define MAIN_H

#include <string>
#include <cmath>
#include <unordered_map>

using namespace std;

/* Assignment 3 struct inst_data is defined in main.h instead of main.cpp because we wanted its inner implementation in new.cpp */

struct inst_data
{
	bool type = false; // true for instruction, false for data
	string name;
	int field1;
	int field2;
	int field3;
	string label;
	bool aux;
	int value = 0; // data in memory initialized to 0
	int line_no;
	inst_data(){} // default constructor
	inst_data(bool my_type, string my_name, int my_field1, int my_field2, int my_field3, string my_label, bool my_aux, int my_value, int my_line_no)
	{
		type = my_type; name = my_name; field1 = my_field1; field2 = my_field2; field3 = my_field3; label = my_label; aux = my_aux; value = my_value; line_no = my_line_no;
	}
};

/* Struct definition ends here */

/* Assignment 3 Global Variables (tentative definitions only, declared in new.cpp) */

const int size = pow(2, 18); // 2^18 blocks of 4 byte each
extern inst_data memory[size];
extern int registers[32]; // all registers initialized to 0
extern int cycle;
extern int pc;
extern int index;
extern int instruction_count[10];
extern unordered_map<string, int> label_map;

extern int ROW_ACCESS_DELAY;
extern int COL_ACCESS_DELAY;
extern int ROW_BUFFER[256];
extern int current_row_index; // the index of the row stored in the row buffer currently
extern int num_buffer_updates;

/* Assignment 3 Global Variables end here */

/* Assignment 3 Function Prototypes */

void undefined_error(int, string);
void error(int, string);
void print_cycle_stats();
void print_non_zero_memory();
void print_stats();

int read_int_value(string, int);
void parse_line(istringstream &, string, int);

void copy_from_row_buffer();
void DRAM_read(int, int);
void DRAM_write(int, int);

/* Assignment 3 Function Prototypes end here */

#endif
