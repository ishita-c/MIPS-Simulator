#include <iostream>
#include <sstream>
#include <fstream>
#include <string>
#include <cmath>
#include <unordered_map>
#include <algorithm>
#include <vector>
#include <cstring> // contains memcpy

using namespace std;

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

struct core
{
	int registers[32];
	inst_data instruction_memory[512]; //2^9
	int PC=0;
	int index=0;
	int offset;
	int instruction_count[10]={0,0,0,0,0,0,0,0,0,0};
	unordered_map<string, int> label_map;
	bool is_being_written[32];
	bool is_being_written_buffer[32]; // MRM modifies is_being_written_buffer which gets copied into is_being_written at the start of each cycle
	bool is_being_stored[32];
	bool store_valid[32];
	int store_address[32];
	core(){}
};

struct DRAM_inst
{
	bool type; // true for lw, false for sw
	int addr;
	int row;
	int data;
	int register_no;
	int inst_no;
	int core_no;
	DRAM_inst(){}
	DRAM_inst(bool my_type, int my_addr, int my_row, int my_data, int my_register_no, int my_inst_no, int my_core_no)
	{
		type = my_type; row = my_row; addr = my_addr; data = my_data; register_no = my_register_no; inst_no = my_inst_no; core_no = my_core_no;
	}
};

const int size = pow(2, 18); // 2^18 blocks of 4 byte each
int DRAM[size];

int cycle;
int cycles_used; // we don't use all cycles, we might only print All instructions initiated, we don't want to count the cycle in this case

int ROW_ACCESS_DELAY;
int COL_ACCESS_DELAY;
int ROW_BUFFER[256];
int current_row_index = -1; // the index of the row stored in the row buffer currently
int prev_row_index = -1;
int num_buffer_updates = 0;

vector<DRAM_inst> DRAM_wait_queue;
vector<DRAM_inst> DRAM_wait_queue2;
int DRAM_inst_number = 0; // this is used to assign order to DRAM_inst objects stored in DRAM_wait_queue
size_t queue_size = 32;

int MRM_decision_cycles = 0;
int MRM_delete_cycles = 0;
bool is_lw_writing = false;
bool is_busy_wait_buffer = false;
bool is_deciding_MRM = false;
bool is_deleting_MRM = false;
bool lw_overwrite_detected = false;
DRAM_inst lw_to_delete;

int DRAM_status = 0; // 0 for free, 1 for buffer row writeback, 2 for buffer row activation, 3 for column access
DRAM_inst current_DRAM_inst; // current DRAM_inst being run
bool is_valid_current_DRAM_inst = false; // true while current_DRAM_inst is being run, false after it
int DRAM_cycles_left;

vector<int> completed_core_num_cycles;
int num_completed_instructions = 0;
int num_deletes = 0; // number of deletes of overwitten lw

int N;
int M;

core core_array[32];
bool has_Error[32];
bool is_initiated[32];

void syntax_error(int line_no, string str, int i)
{
	string message;
	if (i==-1) message = "Syntax Error: "+str;
	else message = "\tSyntax Error: File t"+to_string(i+1)+".txt: Line "+to_string(line_no)+": "+str;
	cout<<message<<'\n';
	has_Error[i] = true;
	return;
}

void undefined_error(int line_no, string str, int i)
{
	string message = "\tUnexpected Error: File t"+to_string(i+1)+".txt: Line "+to_string(line_no)+": "+str;
	cout<<message<<'\n';
	has_Error[i] = true;
	return;
}

void error(int line_no, string str, int i) // error while execution
{
	string message = "\tExecution Error: File t"+to_string(i+1)+".txt: Line "+to_string(line_no)+": "+str;
	cout<<message<<'\n';
	has_Error[i] = true;
	return;
}

int read_int_value(string word, int line_no, int i)
{
	if(word[0] == '-'){
		string temp = word.substr(1, temp.length()-1);
		if(temp=="" || !all_of(temp.begin(), temp.end(), ::isdigit)) // this will give error if temp is not integer
		{
			if(line_no==-2){syntax_error(0, "Invalid integer value", i);}
			else{syntax_error(line_no, "Invalid instruction format", i);}
			return -1;
		}
	}
	else{
		if(word=="" || !all_of(word.begin(), word.end(), ::isdigit)) // this will give error if word is not integer
		{
			if(line_no==-2){syntax_error(0, "Invalid integer value", i);}
			else{syntax_error(line_no, "Invalid instruction format", i);}
			return -1;
		}
	}
	return stoi(word);
}

int read_register_value(string word, int line_no, int i)
{
	if(word=="$zero")
	{
		return 0;
	}
	else if(word=="$at" || word=="$gp" || word=="$k0" || word=="$k1")
	{
		string str = "Cannot read or write to reserved register "+word;
		syntax_error(line_no, str, i);
		return -1;
	}
	else if(word=="$fp" || word=="$ra")
	{
		string str = "Functionality for register "+word+" not provided, use another register";
		syntax_error(line_no, str, i);
		return -1;
	}
	else if(word=="$sp")
	{
		return 29;
	}
	else if(word.substr(0,2)=="$t")
	{
		int x = read_int_value(word.substr(2, word.length()-2), line_no, i); // this will give error if x is not integer
		if(x<0 || x>9)
		{
			syntax_error(line_no, "Invalid register number", i);
			return -1;
		}
		else if(x<8){ return (x+8);}
		else{ return (x+16);}
	}
	else if(word.substr(0,2)=="$s")
	{
		int x = read_int_value(word.substr(2, word.length()-2), line_no, i); // this will give error if x is not integer
		if(x<0 || x>7)
		{
			syntax_error(line_no, "Invalid register number", i);
			return -1;
		}
		return (x+16);
	}
	else if(word.substr(0,2)=="$v")
	{
		int x = read_int_value(word.substr(2, word.length()-2), line_no, i); // this will give error if x is not integer
		if(x<0 || x>1)
		{
			syntax_error(line_no, "Invalid register number", i);
			return -1;
		}
		return (x+2);
	}
	else if(word.substr(0,2)=="$a")
	{
		int x = read_int_value(word.substr(2, word.length()-2), line_no, i); // this will give error if x is not integer
		if(x<0 || x>3)
		{
			syntax_error(line_no, "Invalid register number", i);
			return -1;
		}
		return (x+4);
	}

	else{
		syntax_error(line_no, "Invalid register name", i);
		return -1;
	}
}

string read_label_value(string word, int line_no, int i)
{
	bool b = true;
	if(!word.empty() && !isalpha(word[0])){
		b = false;
	}
	if(word.empty() || !(all_of(word.begin(), word.end(), [](char c){return (isdigit(c) || isalpha(c) || c=='_');}))) { // lambda function to check if all characters are either digits or letters or underscore, all_of is a function from algorithm library
		b = false;
   	}
	if(b==true) return word;
	else{syntax_error(line_no, "Invalid label name", i);}
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
void extract_arguments(istringstream &ss, string &word1, string &word2, string &word3, int line_no, int num, int i) // extracts arguments by removing commas, whitespaces and comments and stores them in word1, word2, and word3 // num is the number of parameters
{
	string token;
	if(num==2){
		if(getline(ss, token, ',')){
			word1 = trim(token);
		}
		else{syntax_error(line_no, "Missing comma", i); return;}
		if(ss >> token){
			word2 = trim(token);
			size_t ind = word2.find("#");
			if(ind != string::npos){word2 = word2.substr(0, ind);} // removing comment, if any
			else if(ss >> token && token[0]!='#'){syntax_error(line_no, "Invalid instruction format", i); return;}
		}
		else{syntax_error(line_no, "Invalid instruction format", i); return;}
	}
	else if(num==3){
		if(getline(ss, token, ',')){
			word1 = trim(token);
		}
		else{syntax_error(line_no, "Missing comma", i); return;}
		if(getline(ss, token, ',')){
			word2 = trim(token);
		}
		else{syntax_error(line_no, "Missing comma", i); return;}
		if(ss >> token){
			word3 = trim(token);
			size_t ind = word3.find("#");
			if(ind != string::npos){word3 = word3.substr(0, ind);} // removing comment, if any
			else if(ss >> token && token[0]!='#'){syntax_error(line_no, "Invalid instruction format", i); return;}
		}
		else{syntax_error(line_no, "Invalid instruction format", i); return;}
	}
	return;
}

bool dependent_queue(inst_data instruction, int i)
{
	if(instruction.name=="addi"){
		if(core_array[i].is_being_written[instruction.field2]) return true;
		if(!core_array[i].is_being_written[instruction.field1]) return false;
		// here there is a possibility of removing the previous overwritten lw instruction from the wait_queue (only if it is not the instruction being run right now)
		if((is_valid_current_DRAM_inst) && current_DRAM_inst.core_no == i && current_DRAM_inst.register_no == instruction.field1) return true; // the overwritten lw is being run and cannot be deleted now
		// now we know we can delete the previous overwritten lw
		lw_overwrite_detected = true;
		lw_to_delete = DRAM_inst(true, -1, -1, 0, instruction.field1, -1, i);
		return false;
	}
	else if(instruction.name=="add" || instruction.name=="mul" || instruction.name=="sub" || instruction.name=="slt"){
		if(instruction.aux)
		{
			if(core_array[i].is_being_written[instruction.field2] || core_array[i].is_being_written[instruction.field3]) return true;
			if(!core_array[i].is_being_written[instruction.field1]) return false;
			// here there is a possibility of removing the previous overwritten lw instruction from the wait_queue (only if it is not the instruction being run right now)
			if((is_valid_current_DRAM_inst) && current_DRAM_inst.core_no == i && current_DRAM_inst.register_no == instruction.field1) return true; // the overwritten lw is being run and cannot be deleted now
			// now we know we can delete the previous overwritten lw
			lw_overwrite_detected = true;
			lw_to_delete = DRAM_inst(true, -1, -1, 0, instruction.field1, -1, i);
			return false;
		}
		else
		{
			if(core_array[i].is_being_written[instruction.field2]) return true;
			if(!core_array[i].is_being_written[instruction.field1]) return false;
			// here there is a possibility of removing the previous overwritten lw instruction from the wait_queue (only if it is not the instruction being run right now)
			if((is_valid_current_DRAM_inst) && current_DRAM_inst.core_no == i && current_DRAM_inst.register_no == instruction.field1) return true; // the overwritten lw is being run and cannot be deleted now
			// now we know we can delete the previous overwritten lw
			lw_overwrite_detected = true;
			lw_to_delete = DRAM_inst(true, -1, -1, 0, instruction.field1, -1, i);
			return false;
		}
	}
	else if(instruction.name=="beq" || instruction.name=="bne"){
		if(instruction.aux)
		{
			if(core_array[i].is_being_written[instruction.field1] || core_array[i].is_being_written[instruction.field2])
			return true;
			else return false;
		}
		else
		{
			if(core_array[i].is_being_written[instruction.field1])
			return true;
			else return false;
		}
	}
	else if(instruction.name=="sw"){ // "sw" instruction is dependent if it wants to read from a dependent register or its indirect addressing is based on a dependent register (dependent means that register is already being written to)
		if(instruction.aux){
			if(core_array[i].is_being_written[instruction.field1] || core_array[i].is_being_written[instruction.field2])
			return true;
			else return false;
		}
		else{
			if(core_array[i].is_being_written[instruction.field1])
			return true;
			else return false;
		}
	}
	else if(instruction.name=="lw"){ // "lw" instruction is dependent if it wants to write to a dependent register or its indirect addressing is based on a dependent register (dependent means that register is already being written to)
		if(instruction.aux){
			if(core_array[i].is_being_written[instruction.field2]) return true;
			if(!core_array[i].is_being_written[instruction.field1]) return false;
			// here there is a possibility of removing the previous overwritten lw instruction from the wait_queue (only if it is not the instruction being run right now)
			if((is_valid_current_DRAM_inst) && current_DRAM_inst.core_no == i && current_DRAM_inst.register_no == instruction.field1) return true; // the overwritten lw is being run and cannot be deleted now
			if(is_busy_wait_buffer || is_deciding_MRM || is_deleting_MRM) return true; // we don't want to modify lw_overwrite_detected and lw_to_delete if this instruction is not going to be run in this cycle so we don't want to delete previous lw
			// now we know we can delete the previous overwritten lw
			lw_overwrite_detected = true;
			lw_to_delete = DRAM_inst(true, -1, -1, 0, instruction.field1, -1, i);
			return false;
		}
		else{
			if(!core_array[i].is_being_written[instruction.field1]) return false;
			// here there is a possibility of removing the previous overwritten lw instruction from the wait_queue (only if it is not the instruction being run right now)
			if((is_valid_current_DRAM_inst) && current_DRAM_inst.core_no == i && current_DRAM_inst.register_no == instruction.field1) return true; // the overwritten lw is being run and cannot be deleted now
			if(is_busy_wait_buffer || is_deciding_MRM || is_deleting_MRM) return true; // we don't want to modify lw_overwrite_detected and lw_to_delete if this instruction is not going to be run in this cycle so we don't want to delete previous lw
			// now we know we can delete the previous overwritten lw
			lw_overwrite_detected = true;
			lw_to_delete = DRAM_inst(true, -1, -1, 0, instruction.field1, -1, i);
			return false;
		}
	}
	else if(instruction.name=="j"){
		return false;
	}
	else{
		string str = "Instruction "+instruction.name;
		undefined_error(instruction.line_no, str, i);
		return false;
	}
}

void parse_line(istringstream &ss, string word, int num, int i) // num is line_no // we pass the istringstream object by reference
{
	string temp;
	if(word=="" || word[0]=='#'){
		return;
	}
	else if(word=="add"){
		string word1, word2, word3;
		extract_arguments(ss, word1, word2, word3, num, 3, i);
		int rdest = read_register_value(word1, num, i);
		int rsrc = read_register_value(word2, num, i);
		int src;
		bool aux;// whether src contains register or immediate value
		if(word3[0]=='$'){aux = true; src = read_register_value(word3, num, i);}
		else{aux = false; src = read_int_value(word3, num, i);}
		core_array[i].instruction_memory[core_array[i].index] = inst_data(true, "add", rdest, rsrc, src, "", aux, 0, num);
		core_array[i].index++;
	}
  else if(word=="addi"){
  	string word1, word2, word3;
		extract_arguments(ss, word1, word2, word3, num, 3, i);
		int rdest = read_register_value(word1, num, i);
		int rsrc = read_register_value(word2, num, i);
		int src = read_int_value(word3, num, i);
		core_array[i].instruction_memory[core_array[i].index] = inst_data(true, "addi", rdest, rsrc, src, "", false, 0, num);
		core_array[i].index++;
	}
	else if(word=="sub"){
		string word1, word2, word3;
		extract_arguments(ss, word1, word2, word3, num, 3, i);
		int rdest = read_register_value(word1, num, i);
		int rsrc = read_register_value(word2, num, i);
		int src;
		bool aux;// whether src contains register or immediate value
		if(word3[0]=='$'){aux = true; src = read_register_value(word3, num, i);}
		else{aux = false; src = read_int_value(word3, num, i);}
		core_array[i].instruction_memory[core_array[i].index] = inst_data(true, "sub", rdest, rsrc, src, "", aux, 0, num);
		core_array[i].index++;
  }
  else if(word=="mul"){
  	string word1, word2, word3;
		extract_arguments(ss, word1, word2, word3, num, 3, i);
		int rdest = read_register_value(word1, num, i);
		int rsrc = read_register_value(word2, num, i);
		int src;
		bool aux;// whether src contains register or immediate value
		if(word3[0]=='$'){aux = true; src = read_register_value(word3, num, i);}
		else{aux = false; src = read_int_value(word3, num, i);}
		core_array[i].instruction_memory[core_array[i].index] = inst_data(true, "mul", rdest, rsrc, src, "", aux, 0, num);
		core_array[i].index++;
	}
	else if(word=="beq"){
		string word1, word2, word3;
		extract_arguments(ss, word1, word2, word3, num, 3, i);
		int rdest = read_register_value(word1, num, i);
		bool aux;// whether src contains register or immediate value
		int src;
		if(word2[0]=='$'){aux = true; src = read_register_value(word2, num, i);}
		else{aux = false; src = read_int_value(word2, num, i);}
		string label = read_label_value(word3, num, i);
		core_array[i].instruction_memory[core_array[i].index] = inst_data(true, "beq", rdest, src, -1, label, aux, 0, num);
		core_array[i].index++;
	}
	else if(word=="bne"){
		string word1, word2, word3;
		extract_arguments(ss, word1, word2, word3, num, 3, i);
		int rdest = read_register_value(word1, num, i);
		int src;
		bool aux;// whether src contains register or immediate value
		if(word2[0]=='$'){aux = true; src = read_register_value(word2, num, i);}
		else{aux = false; src = read_int_value(word2, num, i);}
		string label = read_label_value(word3, num, i);
		core_array[i].instruction_memory[core_array[i].index] = inst_data(true, "bne", rdest, src, -1, label, aux, 0, num);
		core_array[i].index++;
  }
  else if(word=="slt"){
  	string word1, word2, word3;
		extract_arguments(ss, word1, word2, word3, num, 3, i);
		int rdest = read_register_value(word1, num, i);
		int rsrc = read_register_value(word2, num, i);
		int src;
		bool aux;// whether src contains register or immediate value
		if(word3[0]=='$'){aux = true; src = read_register_value(word3, num, i);}
		else{aux = false; src = read_int_value(word3, num, i);}
		core_array[i].instruction_memory[core_array[i].index] = inst_data(true, "slt", rdest, rsrc, src, "", aux, 0, num);
		core_array[i].index++;
  }
  else if(word=="j"){
  	string word1;
  	ss >> word1;
  	if(ss >> temp && temp[0]!='#'){syntax_error(num, "Invalid instruction format", i); return;}
  	string label = read_label_value(word1, num, i);
  	core_array[i].instruction_memory[core_array[i].index] = inst_data(true, "j", -1, -1, -1, label, false, 0, num);
		core_array[i].index++;
  }
  else if(word=="lw"){
  	string word1, word2, word3;
  	extract_arguments(ss, word1, word2, word3, num, 2, i); // extract only two arguments for this instruction
		int rdest = read_register_value(word1, num, i);
		int mem, offset;
		bool aux; // aux=true would mean indirect addressing mode using parentheses and false would mean integer address in memory is given
		if(word2.back()==')'){
			aux = true;
			size_t ind = word2.find("(");
			if(ind == string::npos){syntax_error(num, "Missing parentheses", i); return;}
			else if (ind == 0) {offset = 0;}
			else{offset = read_int_value(word2.substr(0, ind), num, i);} // size of the string representation of offset would be ind
			mem = read_register_value(word2.substr(ind+1, word2.length()-ind-2), num, i);
		}
		else{
			aux = false; offset = 0;
			mem = read_int_value(word2, num, i);
		}
		core_array[i].instruction_memory[core_array[i].index] = inst_data(true, "lw", rdest, mem, offset, "", aux, 0, num);
		core_array[i].index++;
	}
	else if(word=="sw"){
  	string word1, word2, word3;
		extract_arguments(ss, word1, word2, word3, num, 2, i);
		int rsrc = read_register_value(word1, num, i);
		int mem, offset;
		bool aux; // aux=true would mean indirect addressing mode using parentheses and false would mean integer address in memory is given
		if(word2.back()==')'){
			aux = true;
			size_t ind = word2.find("(");
			if(ind == string::npos){syntax_error(num, "Missing parentheses", i); return;}
			else if (ind == 0) {offset = 0;}
			else{offset = read_int_value(word2.substr(0, ind), num, i);} // size of the string representation of offset would be ind
			mem = read_register_value(word2.substr(ind+1, word2.length()-ind-2), num, i);
		}
		else{
			aux = false; offset = 0;
			mem = read_int_value(word2, num, i);
		}
		core_array[i].instruction_memory[core_array[i].index] = inst_data(true, "sw", rsrc, mem, offset, "", aux, 0, num);
		core_array[i].index++;
	}
	else if(word.find(':') != string::npos){
		string label;
		if(word.back()==':'){
  		ss >> temp;
			if(ss >> temp && temp[0]!='#'){syntax_error(num, "Invalid instruction format", i); return;}
			label = read_label_value(word.substr(0, word.length()-1), num, i);
		}
		else{
			size_t ind = word.find(':');
			if(word[ind+1]!='#'){syntax_error(num, "Invalid instruction format", i); return;}
			label = read_label_value(word.substr(0, ind), num, i);
		}
		core_array[i].label_map.insert(make_pair(label, core_array[i].index));
	}
	else if(ss >> temp && temp==":"){
		if(ss >> temp && temp[0]!='#'){syntax_error(num, "Invalid instruction format", i); return;}
		string label = read_label_value(word, num, i);
		core_array[i].label_map.insert(make_pair(label, core_array[i].index));
	}
	else{
		syntax_error(num, "Invalid instruction", i);
	}
	return;
}

void print_cycle_stats(int i) // used to print cycle stats for instructions add, addi, sub, mul, lw, sw, j
{
	inst_data instruction = core_array[i].instruction_memory[core_array[i].PC];
	string reg, addr;
	if(instruction.name=="addi" || instruction.name=="add" || instruction.name=="mul" || instruction.name=="sub" || instruction.name=="slt"){
		if (instruction.field1==0){reg = "$zero";}
		else if(instruction.field1<=3){reg = "$v"+to_string(instruction.field1-2);}
		else if(instruction.field1<=7){reg = "$a"+to_string(instruction.field1-4);}
		else if(instruction.field1<=15){reg = "$t"+to_string(instruction.field1-8);}
		else if(instruction.field1<=23){reg = "$s"+to_string(instruction.field1-16);}
		else if(instruction.field1<=25){reg = "$t"+to_string(instruction.field1-16);}
		else if(instruction.field1==29){reg = "$sp";}
		else{
			string str = "Error in print_cycle_stats: Core"+to_string(i+1)+" Instruction "+instruction.name+" Register number "+to_string(instruction.field1);
			undefined_error(instruction.line_no, str, i);
		}
		cout<<"\tCore "<<(i+1)<<": "<<"Instruction \""<<instruction.name<<"\"\t"<<reg<<"="<<core_array[i].registers[instruction.field1]<<'\n';
	}
	else if(instruction.name=="sw" || instruction.name=="lw"){
		if (instruction.field1==0){reg = "$zero";}
		else if(instruction.field1<=3){reg = "$v"+to_string(instruction.field1-2);}
		else if(instruction.field1<=7){reg = "$a"+to_string(instruction.field1-4);}
		else if(instruction.field1<=15){reg = "$t"+to_string(instruction.field1-8);}
		else if(instruction.field1<=23){reg = "$s"+to_string(instruction.field1-16);}
		else if(instruction.field1<=25){reg = "$t"+to_string(instruction.field1-16);}
		else if(instruction.field1==29){reg = "$sp";}
		else{
			string str = "Error in print_cycle_stats: Core "+to_string(i+1)+" Instruction "+instruction.name+" Register number "+to_string(instruction.field1);
			undefined_error(instruction.line_no, str, i);
		}
		if(instruction.aux){
			if(instruction.field2==0){addr = to_string(instruction.field3)+"($zero)";}
			else if(instruction.field2<=3){addr = to_string(instruction.field3)+"($v"+to_string(instruction.field2-2)+")";}
			else if(instruction.field2<=7){addr = to_string(instruction.field3)+"($a"+to_string(instruction.field2-4)+")";}
			else if(instruction.field2<=15){addr = to_string(instruction.field3)+"($t"+to_string(instruction.field2-8)+")";}
			else if(instruction.field2<=23){addr = to_string(instruction.field3)+"($s"+to_string(instruction.field2-16)+")";}
			else if(instruction.field2<=25){addr = to_string(instruction.field3)+"($t"+to_string(instruction.field2-16)+")";}
			else if(instruction.field2==29){addr = to_string(instruction.field3)+"($sp)";}
			else{
				string str = "Error in print_cycle_stats: Core "+to_string(i+1)+" Instruction "+instruction.name+" Register number "+to_string(instruction.field2);
				undefined_error(instruction.line_no, str, i);
			}
		}
		else{addr = to_string(instruction.field2);}
		cout<<"\tCore "<<(i+1)<<": Instruction \""<<instruction.name<<" "<<reg<<" "<<addr<<"\"\tDRAM request issued\n";
	}
	else if(instruction.name=="j"){
		cout<<"\tCore "<<(i+1)<<": "<<"Instruction \"j\"\t\t"<<"Jump to label "<<instruction.label<<'\n';
	}
	else{
		string str = "Unvalid call to print_cycle_stats: Core "+to_string(i+1)+" Instruction "+instruction.name;
		undefined_error(instruction.line_no, str, i);
	}
	return;
}

void run_instruction(int i)
{
	inst_data current_inst = core_array[i].instruction_memory[core_array[i].PC]; // we have already checked PC < index in run() and then called run_instruction()
	unordered_map<string, int> label_map = core_array[i].label_map;

	if(current_inst.name=="add")
	{
		if(current_inst.field1==0){error(current_inst.line_no, "Cannot write to $zero register", i); return;}
		if((core_array[i].is_being_stored[current_inst.field1]) && (core_array[i].store_valid[current_inst.field1])){
			core_array[i].store_valid[current_inst.field1] = false;
			core_array[i].store_address[current_inst.field1] = -1;
		}
		if(current_inst.aux) {core_array[i].registers[current_inst.field1] = core_array[i].registers[current_inst.field2] + core_array[i].registers[current_inst.field3];}
		else {core_array[i].registers[current_inst.field1] = core_array[i].registers[current_inst.field2] + current_inst.field3;}
		core_array[i].instruction_count[0]++;
		num_completed_instructions++;
		print_cycle_stats(i);
		core_array[i].PC++;
		core_array[i].is_being_written_buffer[current_inst.field1] = false; // it a previous redundant lw was detected, we need to turn off its is_being_written_buffer
	}
	else if(current_inst.name=="addi")
	{
		if(current_inst.field1==0){error(current_inst.line_no, "Cannot write to $zero register", i); return;}
		if((core_array[i].is_being_stored[current_inst.field1]) && (core_array[i].store_valid[current_inst.field1])){
			core_array[i].store_valid[current_inst.field1] = false;
			core_array[i].store_address[current_inst.field1] = -1;
		}
		core_array[i].registers[current_inst.field1] = core_array[i].registers[current_inst.field2] + current_inst.field3;
		core_array[i].instruction_count[1]++;
		num_completed_instructions++;
		print_cycle_stats(i);
		core_array[i].PC++;
		core_array[i].is_being_written_buffer[current_inst.field1] = false; // it a previous redundant lw was detected, we need to turn off its is_being_written_buffer
	}
	else if(current_inst.name=="sub")
	{
		if(current_inst.field1==0){error(current_inst.line_no, "Cannot write to $zero register", i); return;}
		if((core_array[i].is_being_stored[current_inst.field1]) && (core_array[i].store_valid[current_inst.field1])){
			core_array[i].store_valid[current_inst.field1] = false;
			core_array[i].store_address[current_inst.field1] = -1;
		}
		if(current_inst.aux) {core_array[i].registers[current_inst.field1] = core_array[i].registers[current_inst.field2] - core_array[i].registers[current_inst.field3];}
		else {core_array[i].registers[current_inst.field1] = core_array[i].registers[current_inst.field2] - current_inst.field3;}
		core_array[i].instruction_count[2]++;
		num_completed_instructions++;
		print_cycle_stats(i);
		core_array[i].PC++;
		core_array[i].is_being_written_buffer[current_inst.field1] = false; // it a previous redundant lw was detected, we need to turn off its is_being_written_buffer
	}
	else if(current_inst.name=="mul")
	{
		if(current_inst.field1==0){error(current_inst.line_no, "Cannot write to $zero register", i); return;}
		if((core_array[i].is_being_stored[current_inst.field1]) && (core_array[i].store_valid[current_inst.field1])){
			core_array[i].store_valid[current_inst.field1] = false;
			core_array[i].store_address[current_inst.field1] = -1;
		}
		if(current_inst.aux) {core_array[i].registers[current_inst.field1] = core_array[i].registers[current_inst.field2] * core_array[i].registers[current_inst.field3];}
		else {core_array[i].registers[current_inst.field1] = core_array[i].registers[current_inst.field2] * current_inst.field3;}
		core_array[i].instruction_count[3]++;
		num_completed_instructions++;
		print_cycle_stats(i);
		core_array[i].PC++;
		core_array[i].is_being_written_buffer[current_inst.field1] = false; // it a previous redundant lw was detected, we need to turn off its is_being_written_buffer
	}
	else if(current_inst.name=="beq")
	{

		if(label_map.find(current_inst.label)==label_map.end()){
			error(current_inst.line_no, "Undeclared label name", i);
			return;
		}
		if(current_inst.aux){
			if(core_array[i].registers[current_inst.field1] == core_array[i].registers[current_inst.field2]){
				cout<<"\tCore "<<(i+1)<<": Instruction \"beq\"\t"<<"Equal, jump to label "<<current_inst.label<<'\n';
				core_array[i].PC = label_map[current_inst.label];
			}
			else{
				cout<<"\tCore "<<(i+1)<<": Instruction \"beq\"\t"<<"Not equal\n";
				core_array[i].PC++;
			}
		}
		else{
			if(core_array[i].registers[current_inst.field1] == current_inst.field2){
				cout<<"\tCore "<<(i+1)<<": Instruction \"beq\"\t"<<"Equal, jump to label "<<current_inst.label<<'\n';
				core_array[i].PC = label_map[current_inst.label];
			}
			else{
				cout<<"\tCore "<<(i+1)<<": Instruction \"beq\"\t"<<"Not equal\n";
				core_array[i].PC++;
			}
		}
		core_array[i].instruction_count[4]++;
		num_completed_instructions++;
		core_array[i].is_being_written_buffer[current_inst.field1] = false; // it a previous redundant lw was detected, we need to turn off its is_being_written_buffer
	}
	else if(current_inst.name=="bne")
	{
		if(label_map.find(current_inst.label)==label_map.end()){
			error(current_inst.line_no, "Undeclared label name", i);
			return;
		}
		if(current_inst.aux){
			if(core_array[i].registers[current_inst.field1] != core_array[i].registers[current_inst.field2]){
				cout<<"\tCore "<<(i+1)<<": Instruction \"bne\"\t"<<"Not equal, jump to label "<<current_inst.label<<'\n';
				core_array[i].PC = label_map[current_inst.label];
			}
			else{
				cout<<"\tCore "<<(i+1)<<": Instruction \"bne\"\t"<<"Equal\n";
				core_array[i].PC++;
			}
		}
		else{
			if(core_array[i].registers[current_inst.field1] != current_inst.field2){
				cout<<"\tCore "<<(i+1)<<": Instruction \"bne\"\t"<<"Not equal, jump to label "<<current_inst.label<<'\n';
				core_array[i].PC = label_map[current_inst.label];
			}
			else{
				cout<<"\tCore "<<(i+1)<<": Instruction \"bne\"\t"<<"Equal\n";
				core_array[i].PC++;
			}
		}
		core_array[i].instruction_count[5]++;
		num_completed_instructions++;
		core_array[i].is_being_written_buffer[current_inst.field1] = false; // it a previous redundant lw was detected, we need to turn off its is_being_written_buffer
	}
	else if(current_inst.name=="slt")
	{
		if(current_inst.field1==0){error(current_inst.line_no, "Cannot write to $zero register", i); return;}
		if((core_array[i].is_being_stored[current_inst.field1]) && (core_array[i].store_valid[current_inst.field1])){
			core_array[i].store_valid[current_inst.field1] = false;
			core_array[i].store_address[current_inst.field1] = -1;
		}
		if(current_inst.aux){
			if(core_array[i].registers[current_inst.field2] < core_array[i].registers[current_inst.field3]) {core_array[i].registers[current_inst.field1] = 1;}
			else {core_array[i].registers[current_inst.field1] = 0;}
		}
		else{
			if(core_array[i].registers[current_inst.field2] < current_inst.field3) {core_array[i].registers[current_inst.field1] = 1;}
			else {core_array[i].registers[current_inst.field1] = 0;}
		}
		core_array[i].instruction_count[6]++;
		num_completed_instructions++;
		print_cycle_stats(i);
		core_array[i].PC++;
		core_array[i].is_being_written_buffer[current_inst.field1] = false; // it a previous redundant lw was detected, we need to turn off its is_being_written_buffer
	}
	else if(current_inst.name=="j")
	{
		if(label_map.find(current_inst.label)==label_map.end()){
			error(current_inst.line_no, "Undeclared label name", i);
			return;
		}
		else{
			print_cycle_stats(i);
			core_array[i].PC = label_map[current_inst.label];
		}
		core_array[i].instruction_count[7]++;
		num_completed_instructions++;
	}
	else if(current_inst.name=="lw")
	{
		int addr;
		if(current_inst.field1==0){error(current_inst.line_no, "Cannot write to $zero register", i); return;}
		if(current_inst.aux){addr = core_array[i].registers[current_inst.field2] + current_inst.field3;}
		else{addr = current_inst.field2;}
		addr+= core_array[i].offset;
		if(addr < core_array[i].offset || addr >= (size*4) || addr%4 != 0){
			error(current_inst.line_no, "Invalid memory access", i);
			return;
		}
		if((i<N-1) && addr >= core_array[i+1].offset){
			error(current_inst.line_no, "Invalid memory access: Core memory limit exceeded", i);
			return;
		}
		// checking if lw-sw forwarding is possible
		for(int j=0; j<32; j++){
			if((core_array[i].is_being_stored[j]) && (core_array[i].store_valid[j]) && (core_array[i].store_address[j]==addr)){
				string reg, str_addr, fwd_reg;
				if(j==0){fwd_reg = "$zero";}
				else if(j<=3){fwd_reg = "$v"+to_string(j-2);}
				else if(j<=7){fwd_reg = "$a"+to_string(j-4);}
				else if(j<=15){fwd_reg = "$t"+to_string(j-8);}
				else if(j<=23){fwd_reg = "$s"+to_string(j-16);}
				else if(j<=25){fwd_reg = "$t"+to_string(j-16);}
				else if(j==29){fwd_reg = "$sp";}
				else{
					string str = "Error in printing stats for sw-forwarded lw: Core "+to_string(i+1)+" Instruction "+current_inst.name+" Register number "+to_string(j);
					undefined_error(current_inst.line_no, str, i);
				}
				if(current_inst.field1==0){reg = "$zero";}
				else if(current_inst.field1<=3){reg = "$v"+to_string(current_inst.field1-2);}
				else if(current_inst.field1<=7){reg = "$a"+to_string(current_inst.field1-4);}
				else if(current_inst.field1<=15){reg = "$t"+to_string(current_inst.field1-8);}
				else if(current_inst.field1<=23){reg = "$s"+to_string(current_inst.field1-16);}
				else if(current_inst.field1<=25){reg = "$t"+to_string(current_inst.field1-16);}
				else if(current_inst.field1==29){reg = "$sp";}
				else{
					string str = "Error in printing stats for sw-forwarded lw: Core "+to_string(i+1)+" Instruction "+current_inst.name+" Register number "+to_string(current_inst.field1);
					undefined_error(current_inst.line_no, str, i);
				}
				if(current_inst.aux){
				if(current_inst.field2==0){str_addr = to_string(current_inst.field3)+"($zero)";}
				else if(current_inst.field2<=3){str_addr = to_string(current_inst.field3)+"($v"+to_string(current_inst.field2-2)+")";}
				else if(current_inst.field2<=7){str_addr = to_string(current_inst.field3)+"($a"+to_string(current_inst.field2-4)+")";}
				else if(current_inst.field2<=15){str_addr = to_string(current_inst.field3)+"($t"+to_string(current_inst.field2-8)+")";}
				else if(current_inst.field2<=23){str_addr = to_string(current_inst.field3)+"($s"+to_string(current_inst.field2-16)+")";}
				else if(current_inst.field2<=25){str_addr = to_string(current_inst.field3)+"($t"+to_string(current_inst.field2-16)+")";}
					else if(current_inst.field2==29){str_addr = to_string(current_inst.field3)+"($sp)";}
					else{
						string str = "Error in printing stats for sw-forwarded lw: Core "+to_string(i+1)+" Instruction "+current_inst.name+" Register number "+to_string(current_inst.field2);
						undefined_error(current_inst.line_no, str, i);
					}
				}
				else{str_addr = to_string(current_inst.field2);}
				if(is_lw_writing){
					if(lw_overwrite_detected && (lw_to_delete.core_no == i) && (lw_to_delete.register_no == current_inst.field1)) lw_overwrite_detected = false; // since we are stalling this instruction, we want to turn off delete signal for its previous overwritten lw
					cout<<"\tCore "<<(i+1)<<": Instruction \""<<current_inst.name<<" "<<reg<<" "<<str_addr<<"\"\tlw forwarded by sw from register "<<fwd_reg<<": Processor Stalled: Write port busy (DRAM read loading into register)\n";
					return;
				}
				if((core_array[i].is_being_stored[current_inst.field1]) && (core_array[i].store_valid[current_inst.field1])){
					core_array[i].store_valid[current_inst.field1] = false;
					core_array[i].store_address[current_inst.field1] = -1;
				}
				core_array[i].registers[current_inst.field1] = core_array[i].registers[j];
				cout<<"\tCore "<<(i+1)<<": Instruction \""<<current_inst.name<<" "<<reg<<" "<<str_addr<<"\"\tRegister "<<reg<<"="<<core_array[i].registers[current_inst.field1]<<" (lw forwarded by sw from register "<<fwd_reg<<")\n";
				core_array[i].instruction_count[8]++;
				num_completed_instructions++;
				core_array[i].PC++;
				core_array[i].is_being_written_buffer[current_inst.field1] = false; // it a previous redundant lw was detected, we need to turn off its is_being_written_buffer
				return;
			}
		}
		if (DRAM_wait_queue.size()<queue_size){
			// forwarding not possible, add to DRAM_wait_queue
			print_cycle_stats(i);
			DRAM_inst t = DRAM_inst(true, addr, (addr/4)/256, 0, current_inst.field1, DRAM_inst_number++, i);
			DRAM_wait_queue.push_back(t);
			is_busy_wait_buffer = true;
			core_array[i].is_being_written_buffer[current_inst.field1] = true;
			core_array[i].instruction_count[8]++;
			core_array[i].PC++;
		}
		else{
			if(lw_overwrite_detected && (lw_to_delete.core_no == i) && (lw_to_delete.register_no == current_inst.field1)) lw_overwrite_detected = false; // since we are stalling this instruction, we want to turn off delete signal for its previous overwritten lw
			cout<<"\tCore "<<i<<": Instruction \"lw\"\tProcessor Stalled: DRAM Queue Full\n";
		}
	}
	else if(current_inst.name=="sw")
	{
		if (DRAM_wait_queue.size()<queue_size){
			int addr;
			if(current_inst.aux){addr = core_array[i].registers[current_inst.field2] + current_inst.field3;}
			else{addr = current_inst.field2;}
			addr+= core_array[i].offset;
			if(addr < core_array[i].offset || addr >= (size*4) || addr%4 != 0){
				cout<<addr<<'\n';
				error(current_inst.line_no, "Invalid memory access", i);
				return;
			}
			if((i<N-1) && addr >= core_array[i+1].offset){
				error(current_inst.line_no, "Invalid memory access: Core memory limit exceeded", i);
				return;
			}
			for(int j=0; j<32; j++){
				if((core_array[i].is_being_stored[j]) && (core_array[i].store_valid[j]) && (core_array[i].store_address[j]==addr)){
					core_array[i].store_valid[j] = false;
					core_array[i].store_address[j] = -1;
				}
			}
			core_array[i].is_being_stored[current_inst.field1] = true;
			core_array[i].store_valid[current_inst.field1] = true;
			core_array[i].store_address[current_inst.field1] = addr;
			print_cycle_stats(i);
			DRAM_inst t = DRAM_inst(false, addr, (addr/4)/256, core_array[i].registers[current_inst.field1], current_inst.field1, DRAM_inst_number++, i);
			DRAM_wait_queue.push_back(t);
			is_busy_wait_buffer = true;
			core_array[i].instruction_count[9]++;
			core_array[i].PC++;
		}
		else {cout<<"\tCore "<<i<<": Instruction \"sw\"\tProcessor Stalled: DRAM Queue Full\n";}
	}
	else
	{
		string str = "Instruction "+current_inst.name;
		undefined_error(current_inst.line_no, str, i);
	}
	return;
}

void row_activation(int ind) // copy a row from memory to the row buffer
{
	int addr = 256*ind;
	for(int i=0; i<256; i++){
		ROW_BUFFER[i] = DRAM[addr];
		addr++;
	}
	current_row_index = ind;
	num_buffer_updates++;
}

void buffer_row_writeback() // copy row from the row buffer to memory
{
	int addr = 256*current_row_index;
	for(int i=0; i<256; i++){
		DRAM[addr] = ROW_BUFFER[i];
		addr++;
	}
}

void write_at_column(int ind, int data) // writes data at address ind in the row buffer
{
	ROW_BUFFER[ind] = data;
	num_buffer_updates++;
}

int read_from_column(int ind) // returns data stored at address ind in the row buffer
{
	return ROW_BUFFER[ind];
}

void run_memory_request_manager()
{
	if(lw_overwrite_detected){
		if(DRAM_status == 0 && !(DRAM_wait_queue.empty()&&DRAM_wait_queue2.empty())) MRM_delete_cycles++; // MRM takes up this complete cycle for deleting in this case
		is_deleting_MRM = true;
		string reg;
		if(lw_to_delete.register_no==0){reg = "$zero";}
		else if(lw_to_delete.register_no<=3){reg = "$v"+to_string(lw_to_delete.register_no-2);}
		else if(lw_to_delete.register_no<=7){reg = "$a"+to_string(lw_to_delete.register_no-4);}
		else if(lw_to_delete.register_no<=15){reg = "$t"+to_string(lw_to_delete.register_no-8);}
		else if(lw_to_delete.register_no<=23){reg = "$s"+to_string(lw_to_delete.register_no-16);}
		else if(lw_to_delete.register_no<=25){reg = "$t"+to_string(lw_to_delete.register_no-16);}
		else if(lw_to_delete.register_no==29){reg = "$sp";}
		else{
			string str = "Error in run_memory_request_manager while deleting overwritten lw instruction: Instruction DRAM_read; Register number "+to_string(lw_to_delete.register_no);
			undefined_error(-1, str, lw_to_delete.core_no);
		}
		vector<DRAM_inst>::iterator it = DRAM_wait_queue2.begin();
		bool success;
		for(; it<DRAM_wait_queue2.end(); it++)
		{
			if((it->type) && (it->core_no == lw_to_delete.core_no) && (it->register_no == lw_to_delete.register_no))
			{
				cout<<"\tMRM deleting overwritten lw instruction (DRAM read) from inner wait buffer: Core No. = "<<(lw_to_delete.core_no+1)<<" Register = "<<reg<<"\n";
				DRAM_wait_queue2.erase(it);
				success = true;
				break;
			}
		}
		if(!success){
			it = DRAM_wait_queue.begin();
			for(; it<DRAM_wait_queue.end(); it++)
			{
				if((it->type) && (it->core_no == lw_to_delete.core_no) && (it->register_no == lw_to_delete.register_no))
				{
					cout<<"\tMRM deleting overwritten lw instruction (DRAM read) from wait buffer: Core No. = "<<(lw_to_delete.core_no+1)<<" Register = "<<reg<<"\n";
					DRAM_wait_queue.erase(it);
					success = true;
					break;
				}
			}
		}
		if(!success){
			cout<<"\tMRM Unexpected Error: Overwritten lw instruction (DRAM read) to be deleted not found: Core No. = "<<(lw_to_delete.core_no+1)<<" Register = "<<reg<<"\n";
			string str = "Error in run_memory_request_manager while deleting overwritten lw instruction: Instruction not found: Instruction DRAM_read; Register number "+to_string(lw_to_delete.register_no);
			undefined_error(-1, str, lw_to_delete.core_no);
		}
		lw_overwrite_detected = false;
		num_completed_instructions++;
		num_deletes++;
	}
	if(DRAM_status==0){
		if(!DRAM_wait_queue.empty())
		{
			if(!DRAM_wait_queue2.empty())
			{
				current_DRAM_inst = DRAM_wait_queue2.at(0);
				if(current_row_index == -1)
				{
					DRAM_status = 2;
					DRAM_cycles_left = ROW_ACCESS_DELAY;
					MRM_decision_cycles++;
					cout<<"\tMRM sending DRAM Row Activation Request: Row "<<current_DRAM_inst.row<<"\n";
				}
				else if(current_row_index != current_DRAM_inst.row)
				{
					DRAM_status = 1;
					DRAM_cycles_left = ROW_ACCESS_DELAY;
					MRM_decision_cycles++;
					cout<<"\tMRM sending DRAM Buffer Row Writeback Request: Row "<<current_row_index<<"\n";
				}
				else
				{
					DRAM_status = 3;
					DRAM_cycles_left = COL_ACCESS_DELAY;
					MRM_decision_cycles++;
					cout<<"\tMRM Sending DRAM Column Access Request: Row "<<current_DRAM_inst.row<<" Col "<<((current_DRAM_inst.addr/4)%256)<<"\n";
				}
				DRAM_wait_queue2.erase(DRAM_wait_queue2.begin());
				is_valid_current_DRAM_inst = true;
				return;
			}
			else
			{
				vector<DRAM_inst>::iterator it = DRAM_wait_queue.begin();
				if(current_row_index == -1)
				{
					current_DRAM_inst = DRAM_wait_queue.at(0);
					DRAM_status = 2;
					DRAM_cycles_left = ROW_ACCESS_DELAY;
					MRM_decision_cycles++;
					cout<<"\tMRM sending Row Activation Request: Row "<<(current_DRAM_inst.row)<<"\n";
					DRAM_wait_queue.erase(DRAM_wait_queue.begin());
					is_valid_current_DRAM_inst = true;
					return;
				}
				for(; it<DRAM_wait_queue.end(); it++)
				{
					if(it->row == current_row_index)
					{
						DRAM_wait_queue2.push_back(*it);
						DRAM_wait_queue.erase(it);
					}
				}
				if(!DRAM_wait_queue2.empty())
				{
					current_DRAM_inst = DRAM_wait_queue2.at(0);
					DRAM_status = 3;
					DRAM_cycles_left = COL_ACCESS_DELAY;
					MRM_decision_cycles++;
					cout<<"\tMRM Sending DRAM Column Access Request: Row "<<current_DRAM_inst.row<<" Col "<<((current_DRAM_inst.addr/4)%256)<<"\n";
					DRAM_wait_queue2.erase(DRAM_wait_queue2.begin());
					is_valid_current_DRAM_inst = true;
					return;
				}
				else
				{
					current_DRAM_inst = DRAM_wait_queue.at(0);
					DRAM_status = 1;
					DRAM_cycles_left = ROW_ACCESS_DELAY;
					MRM_decision_cycles++;
					cout<<"\tMRM sending DRAM Buffer Row Writeback Request: Row "<<current_row_index<<"\n";
					DRAM_wait_queue.erase(DRAM_wait_queue.begin());
					is_valid_current_DRAM_inst = true;
					return;
				}
			}
		}
		else if(!DRAM_wait_queue2.empty())
		{
			current_DRAM_inst = DRAM_wait_queue2.at(0);
			if(current_row_index == -1)
			{
				DRAM_status = 2;
				DRAM_cycles_left = ROW_ACCESS_DELAY;
				MRM_decision_cycles++;
				cout<<"\tMRM sending DRAM Row Activation Request: Row "<<current_DRAM_inst.row<<"\n";
			}
			else if(current_row_index != current_DRAM_inst.row)
			{
				DRAM_status = 1;
				DRAM_cycles_left = ROW_ACCESS_DELAY;
				MRM_decision_cycles++;
				cout<<"\tMRM sending DRAM Buffer Row Writeback Request: Row "<<current_row_index<<"\n";
			}
			else
			{
				DRAM_status = 3;
				DRAM_cycles_left = COL_ACCESS_DELAY;
				MRM_decision_cycles++;
				cout<<"\tMRM Sending DRAM Column Access Request: Row "<<current_DRAM_inst.row<<" Col "<<((current_DRAM_inst.addr/4)%256)<<"\n";
			}
			DRAM_wait_queue2.erase(DRAM_wait_queue2.begin());
			is_valid_current_DRAM_inst = true;
			return;
		}
		else{
			is_valid_current_DRAM_inst = false;
			cout<<"\tDRAM Vacant: No instructions to execute\n";
		}
	}
	else if(DRAM_status==1){
		if(DRAM_cycles_left==ROW_ACCESS_DELAY){
			DRAM_cycles_left--;
			cout<<"\tDRAM Buffer Row Writeback: Row "<<current_row_index<<" Started\n";
		}
		else if(DRAM_cycles_left==1){
			buffer_row_writeback();
			DRAM_status=2;
			DRAM_cycles_left = ROW_ACCESS_DELAY;
			cout<<"\tDRAM Buffer Row Writeback: Row "<<current_row_index<<" Completed\n";
		}
		else{
			cout<<"\tDRAM Buffer Row Writeback: Row "<<current_row_index<<" Ongoing\n";
			DRAM_cycles_left--;
		}
	}
	else if(DRAM_status==2){
		if(DRAM_cycles_left==ROW_ACCESS_DELAY){
			DRAM_cycles_left--;
			cout<<"\tDRAM Row Activation: Row "<<current_DRAM_inst.row<<" Started\n";
		}
		else if(DRAM_cycles_left==1){
			row_activation(current_DRAM_inst.row);
			DRAM_status=3;
			DRAM_cycles_left = COL_ACCESS_DELAY;
			cout<<"\tDRAM Row Activation: Row "<<current_row_index<<" Completed\n";
		}
		else{
			cout<<"\tDRAM Row Activation: Row "<<current_DRAM_inst.row<<" Ongoing\n";
			DRAM_cycles_left--;
		}
	}
	else if(DRAM_status==3){
		if(current_DRAM_inst.type){
			if (DRAM_cycles_left == COL_ACCESS_DELAY){
				DRAM_cycles_left--;
				cout<<"\tDRAM Column Access: Row "<<current_DRAM_inst.row<<" Col "<<((current_DRAM_inst.addr/4)%256)<<" Started\n";
			}
			else if(DRAM_cycles_left==1){
				core_array[current_DRAM_inst.core_no].registers[current_DRAM_inst.register_no] = read_from_column((current_DRAM_inst.addr/4)%256);
				DRAM_status = 0;
				DRAM_cycles_left = 0;
				is_lw_writing = true;
				num_completed_instructions++;
				string reg;
				if(current_DRAM_inst.register_no==0){reg = "$zero";}
				else if(current_DRAM_inst.register_no<=3){reg = "$v"+to_string(current_DRAM_inst.register_no-2);}
				else if(current_DRAM_inst.register_no<=7){reg = "$a"+to_string(current_DRAM_inst.register_no-4);}
				else if(current_DRAM_inst.register_no<=15){reg = "$t"+to_string(current_DRAM_inst.register_no-8);}
				else if(current_DRAM_inst.register_no<=23){reg = "$s"+to_string(current_DRAM_inst.register_no-16);}
				else if(current_DRAM_inst.register_no<=25){reg = "$t"+to_string(current_DRAM_inst.register_no-16);}
				else if(current_DRAM_inst.register_no==29){reg = "$sp";}
				else{
					string str = "Error in run_memory_request_manager while printing stats: Instruction DRAM_read; Register number "+to_string(current_DRAM_inst.register_no);
					undefined_error(-1, str, current_DRAM_inst.core_no);
				}
				cout<<"\tDRAM Column Read: Row "<<current_DRAM_inst.row<<" Col "<<((current_DRAM_inst.addr/4)%256)<<" Completed\tCore "<<current_DRAM_inst.core_no<<" Register "<<reg<<"="<<core_array[current_DRAM_inst.core_no].registers[current_DRAM_inst.register_no]<<'\n';
				core_array[current_DRAM_inst.core_no].is_being_written_buffer[current_DRAM_inst.register_no] = false;
			}
			else{
				cout<<"\tDRAM Column Access: Row "<<current_DRAM_inst.row<<" Col "<<((current_DRAM_inst.addr/4)%256)<<" Ongoing\n";
				DRAM_cycles_left--;
			}
		}
		else{
			if (DRAM_cycles_left == COL_ACCESS_DELAY){
				DRAM_cycles_left--;
				cout<<"\tDRAM Column Access: Row "<<current_DRAM_inst.row<<" Col "<<((current_DRAM_inst.addr/4)%256)<<" Started\n";
			}
			else if(DRAM_cycles_left==1){
				/* we don't need to turn off store valid and is_being_stored here */
				//core_array[current_DRAM_inst.core_no].store_valid[current_DRAM_inst.register_no] = false;
				//core_array[current_DRAM_inst.core_no].store_valid[current_DRAM_inst.register_no] = false;
				//core_array[current_DRAM_inst.core_no].store_address[current_DRAM_inst.register_no] = -1;
				write_at_column((current_DRAM_inst.addr/4)%256, current_DRAM_inst.data);
				DRAM_status=0;
				DRAM_cycles_left=0;
				num_completed_instructions++;
				cout<<"\tDRAM Column Write: Row "<<current_DRAM_inst.row<<" Col "<<((current_DRAM_inst.addr/4)%256)<<" Completed\tDRAM Address "<<current_DRAM_inst.addr<<"-"<<(current_DRAM_inst.addr+3)<<"="<<ROW_BUFFER[(current_DRAM_inst.addr/4)%256]<<'\n';
				//is_valid_current_DRAM_inst = false;
			}
			else{
				cout<<"\tDRAM Column Access: Row "<<current_DRAM_inst.row<<" Col "<<((current_DRAM_inst.addr/4)%256)<<" Ongoing\n";
				DRAM_cycles_left--;
			}
		}
	}
}

void print_non_zero_memory()
{
	cout<<"------------------------------------------------------------------------------\n";
	cout<<"Execution Completed\n";
	cout<<"------------------------------------------------------------------------------\n";
	cout<<"Memory locations that store non-zero data (data in hex-format):\n";
	char hex_string[20];
	for(int i=0; i<size; i++){
		if(DRAM[i]!=0){
			sprintf(hex_string, "%X", DRAM[i]); // convert number to hexdecimal form
			cout<<4*i<<"-"<<4*i+3<<": "<<hex_string<<'\n';
		}
	}
	return;
}

void print_stats()
{
	cout<<"------------------------------------------------------------------------------\n";
	cout<<"Statistics:\n";
	cout<<"------------------------------------------------------------------------------\n";
	cout<<"Total no. of Row Buffer Updates: "<<num_buffer_updates<<"\n";
	cout<<"Total no. of Instructions Completed: "<<num_completed_instructions<<"\n";
	cout<<"Total no. of Clock Cycles Used: "<<cycles_used<<"\n";
	cout<<"Total no. of overwritten lw's (DRAM reads) deleted from wait buffer: "<<num_deletes<<"\n";
	cout<<"Total no. of Clock Cycles Used by MRM: "<<(MRM_delete_cycles+MRM_decision_cycles)<<'\n';
	cout<<"**** Total Amount of Delay caused by MRM (in number of Clock Cycles): "<<MRM_decision_cycles<<" **** (remaining cycles taken by MRM were in parallel with DRAM execution) \n";
	cout<<"------------------------------------------------------------------------------\n";
	cout<<"Status of each core:\n";
	for(int i=0; i<N; i++){
		if(is_initiated[i]) cout<<"Core "<<to_string(i+1)<<": All instructions initiated\n";
		else if(has_Error[i]) cout<<"Core "<<to_string(i+1)<<": Error encountered\n";
		else cout<<"Core "<<to_string(i+1)<<": Initiation pending for some instructions\n";
	}
	string names[10] = {"add", "addi", "sub", "mul", "beq", "bne", "slt", "j", "lw", "sw"};
	cout<<"------------------------------------------------------------------------------\n";
	cout<<"No. of times each instruction initiated:\n";
	for(int i=0; i<N; i++){
		cout<<"Core "<<to_string(i+1)<<":";
		for(int j=0; j<10; j++){
			if(core_array[i].instruction_count[j] != 0) cout<<" "<<names[j]<<" = "<<core_array[i].instruction_count[j]<<";";
		}
		cout<<'\n';
	}
	cout<<"------------------------------------------------------------------------------\n";
	cout<<"Instructions per cycle (IPC) = "<<(double)num_completed_instructions/(double)cycles_used<<'\n';
	return;
}

void run()
{
	cycle = 0;
	cycles_used = 0;
	bool cycle_used = false;
	while(cycle<M)
	{
		int count = 0;
		for(int p=0; p<N; p++){if(is_initiated[p] || has_Error[p]){count++;}}
		if(((int)completed_core_num_cycles.size()==N || count==N)&& DRAM_wait_queue.empty() && DRAM_wait_queue2.empty() && DRAM_status==0) {break;}
		cout<<"Cycle "<<cycle<<":\n";
		if(DRAM_status!=0) cycle_used = true;
		if((DRAM_status==0) && !(DRAM_wait_queue.empty()&&DRAM_wait_queue2.empty())) {is_deciding_MRM = true; cycle_used = true;}
		for(int i=0; i<N; i++){ // copying is_being_written_buffer to is_being_stored at each cycle
			memcpy(core_array[i].is_being_written, core_array[i].is_being_written_buffer, sizeof(core_array[i].is_being_written));
		}

		//cout<<DRAM_wait_queue.size()<<" "<<DRAM_wait_queue2.size()<<'\n';
		run_memory_request_manager();

		for(int i=0; i<N; i++)
		{
			if(has_Error[i])
			{
				cout<<"\tCore "<<(i+1)<<": Error: Cannot be executed further\n";
				continue;
			}
			else if(core_array[i].PC >= core_array[i].index) // index is the index just after last instruction in memory
			{
				if(!is_initiated[i]) {completed_core_num_cycles.push_back(cycle); is_initiated[i]=true;}
				cout<<"\tCore "<<(i+1)<<": All instructions initiated\n";
				continue;
			}
			else
			{
				cycle_used = true;
				inst_data instruction = core_array[i].instruction_memory[core_array[i].PC];
				if(!dependent_queue(instruction, i)){
					if(instruction.name == "lw"||instruction.name == "sw"){
						if(!is_busy_wait_buffer && !is_deciding_MRM && !is_deleting_MRM) {run_instruction(i);} // is_busy_wait_buffer is made true in run_instruction
						else if(is_busy_wait_buffer) {cout<<"\tCore "<<(i+1)<<": Instruction \""<<instruction.name<<"\"\tProcessor Stalled: DRAM request already being sent to wait buffer\n";}
						// if MRM is making decision or deleting in this cycle then we are not processing any lw or sw in this cycle (even though lw may not need to go to wait buffer)
						else if(is_deciding_MRM) {cout<<"\tCore "<<(i+1)<<": Instruction \""<<instruction.name<<"\"\tProcessor Stalled: MRM is accessing wait buffer to make decision\n";}
						else if(is_deleting_MRM) {cout<<"\tCore "<<(i+1)<<": Instruction \""<<instruction.name<<"\"\tProcessor Stalled: MRM is accessing wait buffer to delete overwritten lw instruction (DRAM read)\n";}
					}
					else{
						if(is_lw_writing && i == current_DRAM_inst.core_no){ // is_lw_writing will also ensure current_DRAM_inst is not NULL
							if(instruction.name == "addi"||instruction.name == "add"||instruction.name == "sub"||instruction.name == "mul"||instruction.name == "slt"){ // lw may also write in the same cycle if it is forwarded, which is handled while running lw
								if(lw_overwrite_detected && (lw_to_delete.core_no == i) && (lw_to_delete.register_no == instruction.field1)) lw_overwrite_detected = false; // since we are stalling this instruction, we want to turn off delete signal for its previous overwritten lw
								cout<<"\tCore "<<(i+1)<<": Instruction \""<<instruction.name<<"\"\tProcessor Stalled: Write port busy (DRAM read loading into register)\n";
							}
							else{run_instruction(i);}
						}
						else{run_instruction(i);}
					}
				}
				else{
					cout<<"\tCore "<<(i+1)<<": Instruction \""<<instruction.name<<"\"\tProcessor Stalled: Dependent instruction\n";
				}
			}
		}
		if(cycle_used){cycles_used++;}
		cycle_used = false;
		is_busy_wait_buffer = false;
		is_deciding_MRM = false;
		is_deleting_MRM = false;
		is_lw_writing = false;
		cycle++;
	}
	if(current_row_index!=-1) buffer_row_writeback(); // before execution ends, we store the contents of the ROW_BUFFER back to the memory
	cout<<"Cycle "<<cycle<<"-"<<(cycle+ROW_ACCESS_DELAY-1)<<": Final Buffer Row Writeback to Memory\n";
	print_non_zero_memory();
	print_stats();
}

int main(int argc, char** argv)
{
	/** redirect cout buffer to console_output.txt **/
	/** COMMENT OUT THIS BLOCK TO WRITE OUTPUT TO FILE INSTEAD OF cout
	ofstream out("console_output.txt");
  //auto *coutbuf = cout.rdbuf();
  cout.rdbuf(out.rdbuf());
  cout<<"This will be redirected to file console_output.txt\n";
	**/

	if(argc != 5)
    {
    	cout<<"Please give N, M, row access delay and column access delay as arguments\n";
    	return -1;
    }
    N = read_int_value(argv[1],-2, -1);
    M = read_int_value(argv[2],-2, -1);
    ROW_ACCESS_DELAY = read_int_value(argv[3], -2, -1);
    COL_ACCESS_DELAY = read_int_value(argv[4], -2, -1);
    cout<<"No. of Cores: "<< N<< "; No. of cycles for simulation: "<<M<<"; Row Access Delay: "<<ROW_ACCESS_DELAY<<"; Column Access Delay: "<<COL_ACCESS_DELAY<<'\n';
    if(ROW_ACCESS_DELAY<0 || COL_ACCESS_DELAY<0 ){
    	cout<<"Error: Invalid access delay\n";
    	return -1;
    }
    if(ROW_ACCESS_DELAY==0){
    	cout<<"Warning: Row access delay is 0\n";
    }
    if(COL_ACCESS_DELAY==0){
    	cout<<"Warning: Column access delay is 0\n";
    }
    if(N<=0){
    	cout<<"Error: N should be a positive integer\n";
    }
    if(M<=0){
    	cout<<"Error: M should be a positive integer\n";
    }
    int num_rows = 1024/N;
    for (int i=0; i<N; i++){
    	ifstream my_file;
    	string filename= "t"+to_string(i+1)+".txt";
			my_file.open(filename, ios::in);
			core_array[i].offset = 1024*i*num_rows;

			if(my_file.is_open())
			{
				string line;
				int line_no = 0;
				while(getline(my_file, line))
				{
					line_no++;
					istringstream ss(line);
					string word;
					ss >> word;
					parse_line(ss, word, line_no, i);
				}
				my_file.close();
			}
			else
			{
				cout<<"Error: File "<<filename<<" not found\n";
				return -1;
			}
    	core_array[i].registers[29] = size-1; // initializing $sp to highest memory address
		}

    run();
    return 0;
}
