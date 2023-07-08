#include <iostream>
#include <fstream>
#include <string>
#include <sstream>
#include <unordered_map>
#include <algorithm>
#include <vector>

#include "main.h"

using namespace std;

/* Struct inst_data definition moved from here to main.h */

/* Global variables moved from here to main.h */

/* DRAM Part implemented in Assignment 3 */

void copy_to_row_buffer(int ind) // copy a row from memory to the row buffer
{
	cycle += ROW_ACCESS_DELAY;
	int addr = 256*ind;
	for(int i=0; i<256; i++){
		ROW_BUFFER[i] = memory[addr].value;
		addr++;
	}
	current_row_index = ind;
	num_buffer_updates++;
}

void copy_from_row_buffer() // copy row from the row buffer to memory
{
	cycle += ROW_ACCESS_DELAY;
	int addr = 256*current_row_index;
	for(int i=0; i<256; i++){
		if(addr >= index){ // if memory[addr] is not an instruction
			memory[addr] = inst_data(false, "", -1, -1, -1, "", false, ROW_BUFFER[i], -1);
		}
		addr++;
	}
}

void write_at_column(int ind, int data) // writes data at address ind in the row buffer
{
	cycle += COL_ACCESS_DELAY;
	ROW_BUFFER[ind] = data;
	num_buffer_updates++;
}

int read_from_column(int ind) // returns data stored at address ind in the row buffer
{
	cycle += COL_ACCESS_DELAY;
	return ROW_BUFFER[ind];
}

void DRAM_read(int addr, int reg_no) // read data from location addr of memory to register reg_no // used for lw
{
	addr = addr/4;
	int row_index = addr/256;
	int col_index = addr%256;
	if(current_row_index == -1){copy_to_row_buffer(row_index);}
	else if(current_row_index == row_index){}
	else{copy_from_row_buffer(); copy_to_row_buffer(row_index);}
	registers[reg_no] = read_from_column(col_index);		
}

void DRAM_write(int addr, int data) // write data to location addr of memory // used for sw
{
	addr = addr/4;
	int row_index = addr/256;
	int col_index = addr%256;
	if(current_row_index == -1){copy_to_row_buffer(row_index);}
	else if(current_row_index == row_index){}
	else{copy_from_row_buffer(); copy_to_row_buffer(row_index);}
	write_at_column(col_index, data);		
}

/* DRAM Part ends here */

struct Exception
{
	string message;
	Exception(const string& str){message = str;}
};

void syntax_error(int line_no, string str)
{
	string message = "Syntax Error: Line "+to_string(line_no)+": "+str;
	cout<<message<<'\n';
	throw Exception(message);
	return;
}

void undefined_error(int line_no, string str)
{
	string message = "Unexpected Error: Line "+to_string(line_no)+": "+str;
	cout<<message<<'\n';
	throw Exception(message);
	return;
}

void error(int line_no, string str) // error while execution
{
	string message = "Execution Error: Line "+to_string(line_no)+": "+str;
	cout<<message<<'\n';
	throw Exception(message);
	return;
}

int read_int_value(string word, int line_no)
{
	if(word[0] == '-'){
		string temp = word.substr(1, temp.length()-1);
		if(temp=="" || !all_of(temp.begin(), temp.end(), ::isdigit)) // this will give error if temp is not integer
		{
			if(line_no==-2){syntax_error(0, "Invalid input access delay");}
			else{syntax_error(line_no, "Invalid instruction format");}
			return -1;	
		}
	}
	else{
		if(word=="" || !all_of(word.begin(), word.end(), ::isdigit)) // this will give error if word is not integer
		{
			if(line_no==-2){syntax_error(0, "Invalid input access delay");}
			else{syntax_error(line_no, "Invalid instruction format");}
			return -1;	
		}
	}
	return stoi(word);
}

int read_register_value(string word, int line_no)
{
	if(word.substr(0,2)=="$t")
	{
		int x = read_int_value(word.substr(2, word.length()-2), line_no); // this will give error if x is not integer
		if(x<0 || x>15)
		{
			syntax_error(line_no, "Invalid register number");
			return -1;
		}
		return x;				
	}
	if(word.substr(0,2)=="$s")
	{
		int x = read_int_value(word.substr(2, word.length()-2), line_no); // this will give error if x is not integer
		if(x<0 || x>14)
		{
			syntax_error(line_no, "Invalid register number");
			return -1;
		}
		return x+16;				
	}
	else if(word=="$zero")
	{
		return 31;				
	}
	else{syntax_error(line_no, "Invalid register name");}
	return -1;
}

string read_label_value(string word, int line_no)
{
	bool b = true;
	if(!word.empty() && !isalpha(word[0])){
		b = false;
	}
	if(word.empty() || !(all_of(word.begin(), word.end(), [](char c){return (isdigit(c) || isalpha(c) || c=='_');}))) { // lambda function to check if all characters are either digits or letters or underscore, all_of is a function from algorithm library
		b = false;
   	}
	if(b==true) return word;
	else{syntax_error(line_no, "Invalid label name");}
	return "";	
}

const string WHITESPACE = " \t"; 
string ltrim(const string& s)
{
    size_t start = s.find_first_not_of(WHITESPACE);
    return (start == string::npos) ? "" : s.substr(start);
} 
string rtrim(const string& s)
{
    size_t end = s.find_last_not_of(WHITESPACE);
    return (end == string::npos) ? "" : s.substr(0, end + 1);
} 
string trim(const string& s)
{
    return rtrim(ltrim(s));
}
void extract_arguments(istringstream &ss, string &word1, string &word2, string &word3, int line_no, int num) // extracts arguments by removing commas, whitespaces and comments and stores them in word1, word2, and word3 // num is the number of parameters
{
	string token;
	if(num==2){
		if(getline(ss, token, ',')){
			word1 = trim(token);	
		}
		else{syntax_error(line_no, "Missing comma"); return;}
		if(ss >> token){
			word2 = trim(token);
			size_t ind = word2.find("#");
			if(ind != string::npos){word2 = word2.substr(0, ind);} // removing comment, if any
			else if(ss >> token && token[0]!='#'){syntax_error(line_no, "Invalid instruction format"); return;}
		}
		else{syntax_error(line_no, "Invalid instruction format"); return;}
	}
	else if(num==3){
		if(getline(ss, token, ',')){
			word1 = trim(token);	
		}
		else{syntax_error(line_no, "Missing comma"); return;}
		if(getline(ss, token, ',')){
			word2 = trim(token);	
		}
		else{syntax_error(line_no, "Missing comma"); return;}
		if(ss >> token){
			word3 = trim(token);
			size_t ind = word3.find("#");
			if(ind != string::npos){word3 = word3.substr(0, ind);} // removing comment, if any
			else if(ss >> token && token[0]!='#'){syntax_error(line_no, "Invalid instruction format"); return;}
		}
		else{syntax_error(line_no, "Invalid instruction format"); return;}
	}
	return;
}

void parse_line(istringstream &ss, string word, int num) // num is line_no // we pass the istringstream object by reference
{
	string temp;
	if(word=="" || word[0]=='#'){
		return;    			
	}
	else if(word=="add"){
		string word1, word2, word3;
		extract_arguments(ss, word1, word2, word3, num, 3);
		int rdest = read_register_value(word1, num);
		int rsrc = read_register_value(word2, num);
		int src;
		bool aux;// whether src contains register or immediate value
		if(word3[0]=='$'){aux = true; src = read_register_value(word3, num);}
		else{aux = false; src = read_int_value(word3, num);}
		memory[index] = inst_data(true, "add", rdest, rsrc, src, "", aux, 0, num);
		index++;			
	}
    	else if(word=="addi"){
    		string word1, word2, word3;
		extract_arguments(ss, word1, word2, word3, num, 3);
		int rdest = read_register_value(word1, num);
		int rsrc = read_register_value(word2, num);
		int src = read_int_value(word3, num);
		memory[index] = inst_data(true, "addi", rdest, rsrc, src, "", false, 0, num);
		index++;   	
    	}
    	else if(word=="sub"){
    		string word1, word2, word3;
		extract_arguments(ss, word1, word2, word3, num, 3);
		int rdest = read_register_value(word1, num);
		int rsrc = read_register_value(word2, num);
		int src;
		bool aux;// whether src contains register or immediate value
		if(word3[0]=='$'){aux = true; src = read_register_value(word3, num);}
		else{aux = false; src = read_int_value(word3, num);}
		memory[index] = inst_data(true, "sub", rdest, rsrc, src, "", aux, 0, num);
		index++;    		
    	}
    	else if(word=="mul"){
    		string word1, word2, word3;
		extract_arguments(ss, word1, word2, word3, num, 3);
		int rdest = read_register_value(word1, num);
		int rsrc = read_register_value(word2, num);
		int src;
		bool aux;// whether src contains register or immediate value
		if(word3[0]=='$'){aux = true; src = read_register_value(word3, num);}
		else{aux = false; src = read_int_value(word3, num);}
		memory[index] = inst_data(true, "mul", rdest, rsrc, src, "", aux, 0, num);
		index++;    		
    	}
    	else if(word=="beq"){
    		string word1, word2, word3;
		extract_arguments(ss, word1, word2, word3, num, 3);
		int rdest = read_register_value(word1, num);
		bool aux;// whether src contains register or immediate value
		int src;
		if(word2[0]=='$'){aux = true; src = read_register_value(word2, num);}
		else{aux = false; src = read_int_value(word2, num);}
		string label = read_label_value(word3, num);		
		memory[index] = inst_data(true, "beq", rdest, src, -1, label, aux, 0, num);
		index++;    		
    	}
    	else if(word=="bne"){
    		string word1, word2, word3;
		extract_arguments(ss, word1, word2, word3, num, 3);
		int rdest = read_register_value(word1, num);
		int src;
		bool aux;// whether src contains register or immediate value
		if(word2[0]=='$'){aux = true; src = read_register_value(word2, num);}
		else{aux = false; src = read_int_value(word2, num);}
		string label = read_label_value(word3, num);		
		memory[index] = inst_data(true, "bne", rdest, src, -1, label, aux, 0, num);
		index++;    		
    	}
    	else if(word=="slt"){
    		string word1, word2, word3;
		extract_arguments(ss, word1, word2, word3, num, 3);
		int rdest = read_register_value(word1, num);
		int rsrc = read_register_value(word2, num);
		int src;
		bool aux;// whether src contains register or immediate value
		if(word3[0]=='$'){aux = true; src = read_register_value(word3, num);}
		else{aux = false; src = read_int_value(word3, num);}
		memory[index] = inst_data(true, "slt", rdest, rsrc, src, "", aux, 0, num);
		index++;    		
    	}
    	else if(word=="j"){
    		string word1;
    		ss >> word1;
    		if(ss >> temp && temp[0]!='#'){syntax_error(num, "Invalid instruction format"); return;}
    		string label = read_label_value(word1, num);
    		memory[index] = inst_data(true, "j", -1, -1, -1, label, false, 0, num);
		index++;    		
    	}
    	else if(word=="lw"){
    		string word1, word2, word3;
    		extract_arguments(ss, word1, word2, word3, num, 2); // extract only two arguments for this instruction
		int rdest = read_register_value(word1, num);
		int mem, offset;
		bool aux; // aux=true would mean indirect addressing mode using parentheses and false would mean integer address in memory is given
		if(word2.back()==')'){
			aux = true;
			size_t ind = word2.find("(");
			if(ind == string::npos){syntax_error(num, "Missing parentheses"); return;}
			else if (ind == 0) {offset = 0;}
			else{offset = read_int_value(word2.substr(0, ind), num);} // size of the string representation of offset would be ind
			mem = read_register_value(word2.substr(ind+1, word2.length()-ind-2), num);			
		}
		else{
			aux = false; offset = 0;
			mem = read_int_value(word2, num);
		}
		memory[index] = inst_data(true, "lw", rdest, mem, offset, "", aux, 0, num);
		index++;    		
    	}
    	else if(word=="sw"){
    		string word1, word2, word3;
    		extract_arguments(ss, word1, word2, word3, num, 2);
		int rsrc = read_register_value(word1, num);
		int mem, offset;
		bool aux; // aux=true would mean indirect addressing mode using parentheses and false would mean integer address in memory is given
		if(word2.back()==')'){
			aux = true;
			size_t ind = word2.find("(");
			if(ind == string::npos){syntax_error(num, "Missing parentheses"); return;}
			else if (ind == 0) {offset = 0;}
			else{offset = read_int_value(word2.substr(0, ind), num);} // size of the string representation of offset would be ind
			mem = read_register_value(word2.substr(ind+1, word2.length()-ind-2), num);			
		}
		else{
			aux = false; offset = 0;
			mem = read_int_value(word2, num);
		}
		memory[index] = inst_data(true, "sw", rsrc, mem, offset, "", aux, 0, num);
		index++;     		
    	}
    	else if(word.find(':') != string::npos){
    		string label;
    		if(word.back()==':'){
	    		ss >> temp;
    			if(ss >> temp && temp[0]!='#'){syntax_error(num, "Invalid instruction format"); return;}
    			label = read_label_value(word.substr(0, word.length()-1), num);
    		}
    		else{
    			size_t ind = word.find(':');
    			if(word[ind+1]!='#'){syntax_error(num, "Invalid instruction format"); return;}
    			label = read_label_value(word.substr(0, ind), num);    			
    		}
    		memory[index] = inst_data(true, "label", -1, -1, -1, label, false, 0, num);
    		label_map.insert(make_pair(label, index));
    		index++;   		
    	}
    	else if(ss >> temp && temp==":"){    		
    		if(ss >> temp && temp[0]!='#'){syntax_error(num, "Invalid instruction format"); return;}
    		string label = read_label_value(word, num);
    		memory[index] = inst_data(true, "label", -1, -1, -1, label, false, 0, num);
    		label_map.insert(make_pair(label, index));
    		index++;   		
    	}
    	else{
    		syntax_error(num, "Invalid instruction");   	
    	}
    	return;
}

void print_cycle_stats() // used to print cycle stats for instructions add, addi, sub, mul, lw, sw, j 
{
	inst_data instruction = memory[pc];
	string reg, addr;
	if(instruction.name=="addi" || instruction.name=="add" || instruction.name=="mul" || instruction.name=="sub" || instruction.name=="slt"){
		if(instruction.field1<16){reg = "$t"+to_string(instruction.field1);}
		else{reg = "$s"+to_string(instruction.field1-16);}
		cout<<"Cycle "<<cycle<<":\t"<<"Instruction \""<<instruction.name<<"\"\t\t"<<reg<<" = "<<registers[instruction.field1]<<'\n';		
	}
	else if(instruction.name=="sw" || instruction.name=="lw"){
		if(instruction.field1<16){reg = "$t"+to_string(instruction.field1);}
		else{reg = "$s"+to_string(instruction.field1-16);}
		if(instruction.aux){
			if(instruction.field2<16){addr = to_string(instruction.field3)+"($t"+to_string(instruction.field2)+")";}
			else{addr = to_string(instruction.field3)+"($s"+to_string(instruction.field2-16)+")";}
		}
		else{addr = to_string(instruction.field2);}
		cout<<"Cycle "<<cycle<<":\t"<<"Instruction \""<<instruction.name<<" "<<reg<<" "<<addr<<"\"\tDRAM request issued\n";
	}
	else if(instruction.name=="j"){
		cout<<"Cycle "<<cycle<<":\t"<<"Instruction \"j\"\t\t"<<"Jump to label "<<instruction.label<<'\n';
	}
	else{
		string str = "Unvalid call to print_cycle_stats: Instruction "+instruction.name;
		undefined_error(instruction.line_no, str);	
	}
	return;	
}

void print_registers()
{
	cout<<"Cycle no.: "<<cycle<<'\n';
	char hex_string[20];
	for(int i=0; i<=15; i++){   		
   		sprintf(hex_string, "%X", registers[i]); // convert number to hexdecimal form   
		cout<<"$t"<<i<<'\t'<<hex_string;
		if(i%4==3){cout<<'\n';}
		else{cout<<'\t';}	
	}
	for(int i=0; i<=14; i++){   		
   		sprintf(hex_string, "%X", registers[i+16]); // convert number to hexdecimal form   
		cout<<"$s"<<i<<'\t'<<hex_string;
		if(i%4==3){cout<<'\n';}
		else{cout<<'\t';}	
	}
	sprintf(hex_string, "%X", registers[31]); // convert number to hexdecimal form
	cout<<"$zero"<<'\t'<<hex_string<<'\n';		
	cout<<"------------------------------------------------------------------------------\n";
	return;	
}

void print_non_zero_memory()
{
	cout<<"------------------------------------------------------------------------------\n";
	cout<<"Execution Completed\n";
	cout<<"------------------------------------------------------------------------------\n";
	cout<<"Memory locations that store non-zero data:\n";
	char hex_string[20];
	for(int i=index; i<size; i++){
		if(memory[i].value!=0){
			sprintf(hex_string, "%X", memory[i].value); // convert number to hexdecimal form
			cout<<4*i<<"-"<<4*i+3<<": "<<hex_string<<'\n';
		}	
	}
	return;
}

void print_stats()
{
	cout<<"------------------------------------------------------------------------------\n";
	cout<<"Statistics:\nTotal no. of Row Buffer Updates: "<<num_buffer_updates<<"\nTotal no. of clock cycles: "<<cycle<<"\nTotal no. of times each instruction executed:\n";
	string names[10] = {"add", "addi", "sub", "mul", "beq", "bne", "slt", "j", "lw", "sw"};
	for(int i=0; i<10; i++){
		cout<<names[i]<<'\t'<<instruction_count[i]<<'\n';
	}
	return;
}

void find_cycles_taken(int &num_cycles, int addr) // determines number of cycles a DRAM operation at address addr will take, result stored in num_cycles
{
	num_cycles = 0;
	int row_index = (addr/4)/256;
	if(current_row_index == -1){num_cycles += ROW_ACCESS_DELAY;}
	else if(current_row_index == row_index){}
	else{num_cycles += 2*ROW_ACCESS_DELAY;}
	num_cycles += COL_ACCESS_DELAY;
}

bool dependent(inst_data instruction, int reg_no, string type) // determines whether instruction is dependent on the current DRAM operation, type is "sw" or "lw"
{
	if(instruction.name=="addi"){
		if(type == "sw"){return false;} // sw is independent of addi among any registers
		return ((instruction.field1 == reg_no) || (instruction.field2 == reg_no));
	}
	else if(instruction.name=="add" || instruction.name=="mul" || instruction.name=="sub" || instruction.name=="slt"){
		if(type == "sw"){return false;}
		if(instruction.aux){return ((instruction.field1 == reg_no) || (instruction.field2 == reg_no) || (instruction.field3 == reg_no));}
		else{return ((instruction.field1 == reg_no) || (instruction.field2 == reg_no));}
	}
	else if(instruction.name=="beq" || instruction.name=="bne"){
		if(type == "sw"){return false;}
		if(instruction.aux){return ((instruction.field1 == reg_no) || (instruction.field2 == reg_no));}
		else{return (instruction.field1 == reg_no);}
	}
	else if(instruction.name=="sw" || instruction.name=="lw"){
		return true; // since DRAM can do only one memory access at a time and a sw or lw is going on
	}
	else if(instruction.name=="j"){
		return false;
	}
	else{
		string str = "Instruction "+instruction.name;
		undefined_error(instruction.line_no, str);
		return false;	
	}	
}

void run_instruction(); // prototyping this functions so that run_further_instructions can call it

void run_further_instructions(int num_cycles, int register_dependency, string type) // this runs instructions parallel to DRAM till its safe
{
	inst_data lookahead_inst = memory[++pc];
	if(type=="lw"){
		for(int i=0; i<num_cycles; i++){
			while(pc<index && lookahead_inst.name == "label"){lookahead_inst = memory[++pc];}
			if(pc<index && ! dependent(lookahead_inst, register_dependency, "lw")){
				run_instruction();
				lookahead_inst = memory[pc];						
			}
			else{
				break; // break as soon as first dependent instruction is encountered
			}				
		}
	}
	else if(type=="sw"){
		for(int i=0; i<num_cycles; i++){
			while(pc<index && lookahead_inst.name == "label"){lookahead_inst = memory[++pc];}
			if(pc<index && ! dependent(lookahead_inst, register_dependency, "sw")){
				run_instruction();
				lookahead_inst = memory[pc];						
			}
			else{
				break; // break as soon as first dependent instruction is encountered
			}				
		}
	}
	else{
		string str = "In run_further_instructions";
		undefined_error(-1, str);		
	}
	return;	
}

void run_instruction()
{
	inst_data current_inst = memory[pc]; // we have already checked pc < index in run() and then called run_instruction()
	if(current_inst.name=="add")
	{
		cycle++;
		if(current_inst.field1==31){error(current_inst.line_no, "Cannot write to $zero register"); return;}
		if(current_inst.aux) {registers[current_inst.field1] = registers[current_inst.field2] + registers[current_inst.field3];}
		else {registers[current_inst.field1] = registers[current_inst.field2] + current_inst.field3;}
		instruction_count[0]++;
		//print_registers();
		string reg;
		if(current_inst.field1<16){reg = "$t"+to_string(current_inst.field1);}
		else{reg = "$s"+to_string(current_inst.field1-16);}
		print_cycle_stats();
		pc++;	
	}
	else if(memory[pc].name=="addi")
	{
		cycle++;
		if(current_inst.field1==31){error(current_inst.line_no, "Cannot write to $zero register"); return;}
		registers[current_inst.field1] = registers[current_inst.field2] + current_inst.field3;
		instruction_count[1]++;
		//print_registers();
		string reg;
		if(current_inst.field1<16){reg = "$t"+to_string(current_inst.field1);}
		else{reg = "$s"+to_string(current_inst.field1-16);}
		print_cycle_stats();
		pc++;		
	}
	else if(memory[pc].name=="sub")
	{
		cycle++;
		if(current_inst.field1==31){error(current_inst.line_no, "Cannot write to $zero register"); return;}
		if(current_inst.aux) {registers[current_inst.field1] = registers[current_inst.field2] - registers[current_inst.field3];}
		else {registers[current_inst.field1] = registers[current_inst.field2] - current_inst.field3;}
		instruction_count[2]++;
		//print_registers();
		string reg;
		if(current_inst.field1<16){reg = "$t"+to_string(current_inst.field1);}
		else{reg = "$s"+to_string(current_inst.field1-16);}
		print_cycle_stats();	
		pc++;	
	}
	else if(memory[pc].name=="mul")
	{
		cycle++;
		if(current_inst.field1==31){error(current_inst.line_no, "Cannot write to $zero register"); return;}
		if(current_inst.aux) {registers[current_inst.field1] = registers[current_inst.field2] * registers[current_inst.field3];}
		else {registers[current_inst.field1] = registers[current_inst.field2] * current_inst.field3;}
		instruction_count[3]++;
		//print_registers();
		string reg;
		if(current_inst.field1<16){reg = "$t"+to_string(current_inst.field1);}
		else{reg = "$s"+to_string(current_inst.field1-16);}
		print_cycle_stats();
		pc++;		
	}
	else if(memory[pc].name=="beq")
	{
		cycle++;
		if(label_map.find(current_inst.label)==label_map.end()){
			error(current_inst.line_no, "Undeclared label name");
			return;
		}
		if(current_inst.aux){			
			if(registers[current_inst.field1] == registers[current_inst.field2]){
				cout<<"Cycle "<<cycle<<":\t"<<"Instruction \"beq\"\t\t"<<"Equal, jump to label "<<current_inst.label<<'\n';
				pc = label_map[current_inst.label];					
			}
			else{
				cout<<"Cycle "<<cycle<<":\t"<<"Instruction \"beq\"\t\t"<<"Not equal\n";
				pc++;
			}
		}
		else{
			if(registers[current_inst.field1] == current_inst.field2){
				cout<<"Cycle "<<cycle<<":\t"<<"Instruction \"beq\"\t\t"<<"Equal, jump to label "<<current_inst.label<<'\n';
				pc = label_map[current_inst.label];					
			}
			else{
				cout<<"Cycle "<<cycle<<":\t"<<"Instruction \"beq\"\t\t"<<"Not equal\n";
				pc++;
			}
		}		
		instruction_count[4]++;
		//print_registers();	
	}
	else if(memory[pc].name=="bne")
	{
		cycle++;
		if(label_map.find(current_inst.label)==label_map.end()){
			error(current_inst.line_no, "Undeclared label name");
			return;
		}
		if(current_inst.aux){			
			if(registers[current_inst.field1] != registers[current_inst.field2]){
				cout<<"Cycle "<<cycle<<":\t"<<"Instruction \"bne\"\t\t"<<"Not equal, jump to label "<<current_inst.label<<'\n';
				pc = label_map[current_inst.label];					
			}
			else{
				cout<<"Cycle "<<cycle<<":\t"<<"Instruction \"bne\"\t\t"<<"Equal\n";
				pc++;
			}
		}
		else{
			if(registers[current_inst.field1] != current_inst.field2){
				cout<<"Cycle "<<cycle<<":\t"<<"Instruction \"bne\"\t\t"<<"Not equal, jump to label "<<current_inst.label<<'\n';
				pc = label_map[current_inst.label];					
			}
			else{
				cout<<"Cycle "<<cycle<<":\t"<<"Instruction \"bne\"\t\t"<<"Equal\n";
				pc++;
			}
		}		
		instruction_count[5]++;
		//print_registers();		
	}
	else if(memory[pc].name=="slt")
	{
		cycle++;
		if(current_inst.field1==31){error(current_inst.line_no, "Cannot write to $zero register"); return;}
		if(current_inst.aux){
			if(registers[current_inst.field2] < registers[current_inst.field3]) {registers[current_inst.field1] = 1;}
			else {registers[current_inst.field1] = 0;}
		}
		else{
			if(registers[current_inst.field2] < current_inst.field3) {registers[current_inst.field1] = 1;}
			else {registers[current_inst.field1] = 0;}
		}	
		instruction_count[6]++;
		//print_registers();
		string reg;
		if(current_inst.field1<16){reg = "$t"+to_string(current_inst.field1);}
		else{reg = "$s"+to_string(current_inst.field1-16);}
		print_cycle_stats();	
		pc++;	
	}
	else if(memory[pc].name=="j")
	{
		cycle++;
		if(label_map.find(current_inst.label)==label_map.end()){
			error(current_inst.line_no, "Undeclared label name");
			return;					
		}
		else{
			print_cycle_stats();
			pc = label_map[current_inst.label];					
		}
		instruction_count[7]++;
		//print_registers();
	}
	else if(memory[pc].name=="lw")
	{
		cycle++;
		int prev_cycle, addr, num_cycles, register_dependency;
		if(current_inst.field1==31){error(current_inst.line_no, "Cannot write to $zero register"); return;}
		if(current_inst.aux){addr = registers[current_inst.field2] + current_inst.field3;}
		else{addr = current_inst.field2;}
		if(addr < index || addr >= size || addr%4 != 0){
			error(current_inst.line_no, "Invalid memory access");
			return;
		}
		print_cycle_stats();
		prev_cycle = cycle+1;
		register_dependency = current_inst.field1;
		find_cycles_taken(num_cycles, addr);
		run_further_instructions(num_cycles, register_dependency, "lw");
		cycle = prev_cycle-1;
		DRAM_read(addr, current_inst.field1);
		instruction_count[8]++;
		//print_registers();
		string reg;
		if(current_inst.field1<16){reg = "$t"+to_string(current_inst.field1);}
		else{reg = "$s"+to_string(current_inst.field1-16);}
		if(prev_cycle==cycle+1){cout<<"Cycle "<<prev_cycle-1<<"-"<<cycle<<":\tDRAM read\t\t\t"<<reg<<" = "<<registers[current_inst.field1]<<'\n';}
		else{cout<<"Cycle "<<prev_cycle<<"-"<<cycle<<":\tDRAM read\t\t\t"<<reg<<" = "<<registers[current_inst.field1]<<'\n';}
	}
	else if(memory[pc].name=="sw")
	{
		cycle++;
		int prev_cycle, addr, num_cycles, register_dependency;
		if(current_inst.aux){addr = registers[current_inst.field2] + current_inst.field3;}
		else{addr = current_inst.field2;}
		if(addr < index || addr >= size || addr%4 != 0){
			error(current_inst.line_no, "Invalid memory access");
			return;
		}
		print_cycle_stats();
		prev_cycle = cycle+1;
		int register_value_old = registers[current_inst.field1]; // we store register value here to later use it for calling DRAM_write
		register_dependency = current_inst.field1;
		find_cycles_taken(num_cycles, addr);
		run_further_instructions(num_cycles, register_dependency, "sw");
		cycle = prev_cycle-1;
		DRAM_write(addr, register_value_old);
		instruction_count[9]++;
		//print_registers();
		if(prev_cycle==cycle+1){cout<<"Cycle "<<prev_cycle-1<<"-"<<cycle<<":\tDRAM write\t\t\tMemory address "<<addr<<"-"<<addr+3<<" = "<<ROW_BUFFER[(addr/4)%256]<<'\n';}
		else{cout<<"Cycle "<<prev_cycle<<"-"<<cycle<<":\tDRAM write\t\t\tMemory address "<<addr<<"-"<<addr+3<<" = "<<ROW_BUFFER[(addr/4)%256]<<'\n';}
	}
	else if(memory[pc].name=="label")
	{
		pc++;
	}
	else
	{
		string str = "Instruction "+current_inst.name;
		undefined_error(current_inst.line_no, str);
	}
	return;	
}

void run()
{
	cycle=0;
	pc=0;
	cout<<"------------------------------------------------------------------------------\n";
	//print_registers();
	for(int i=0; i<10; i++){instruction_count[i] = 0;}
	while(true)
	{
		if(pc >= index) // index is the index just after last instruction in memory
		{
			break;		
		}
		else
		{
			run_instruction();
		}
	}
	int prev_cycle = cycle+1;
	copy_from_row_buffer(); // before execution ends, we store the contents of the ROW_BUFFER back to the memory
	if(prev_cycle==cycle+1){cout<<"Cycle "<<prev_cycle-1<<"-"<<cycle<<":\tFinal Buffer Row Copy to Memory\n";}
	else{cout<<"Cycle "<<prev_cycle<<"-"<<cycle<<":\tFinal Buffer Row WriteBack to Memory\n";}	
	print_non_zero_memory();
	print_stats();
	return;	
}
