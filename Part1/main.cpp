#include <iostream>
#include <fstream>
#include <string>
#include <sstream>
#include <unordered_map>
#include <algorithm>
#include <cmath>
#include <vector>

using namespace std;

struct inst_data
{
	bool type; // true for instruction, false for data
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

const long long size = pow(2, 18);
inst_data memory[size];
int r[32]; // all registers initialized to 0
int cycle;
int pc;
int index = 0;
int instruction_count[10];
unordered_map<string, int> label_map;

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

int read_register_value(string word, int line_no)
{
	if(word.substr(0,2)=="$r")
	{
		stringstream s(word.substr(2, word.length()-2));
		int x = 0;
		s >> x; // this will give error if x is not integer
		if(x<0 || x>30)
		{
			syntax_error(line_no, "Invalid register number");
			return -1;
		}
		return x;				
	}
	else if(word=="$zero")
	{
		return 31;				
	}
	else{syntax_error(line_no, "Invalid register name");}
	return -1;
}

int read_int_value(string word, int line_no)
{
	if(word=="")
	{
		syntax_error(line_no, "Invalid instruction format");
		return -1;	
	}
	stringstream s(word);
	int x = 0;
	s >> x; // this will give error if x is not integer
	return x;
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
		else{syntax_error(num, "Missing comma"); return;}
		if(ss >> token){
			word2 = trim(token);
			size_t ind = word2.find("#");
			if(ind != string::npos){word2 = word2.substr(0, ind);} // removing comment, if any
			else if(ss >> token && token[0]!='#'){syntax_error(num, "Invalid instruction format"); return;}
		}
		else{syntax_error(num, "Invalid instruction format"); return;}
	}
	else if(num==3){
		if(getline(ss, token, ',')){
			word1 = trim(token);	
		}
		else{syntax_error(num, "Missing comma"); return;}
		if(getline(ss, token, ',')){
			word2 = trim(token);	
		}
		else{syntax_error(num, "Missing comma"); return;}
		if(ss >> token){
			word3 = trim(token);
			size_t ind = word3.find("#");
			if(ind != string::npos){word3 = word3.substr(0, ind);} // removing comment, if any
			else if(ss >> token && token[0]!='#'){syntax_error(num, "Invalid instruction format"); return;}
		}
		else{syntax_error(num, "Invalid instruction format"); return;}
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
			int mem = read_int_value(word2, num);
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
			int mem = read_int_value(word2, num);
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

void print_registers()
{
	cout<<"Cycle no.: "<<cycle<<'\n';
	for(int i=0; i<31; i++){
   		char hex_string[20];
   		sprintf(hex_string, "%X", r[i]); //convert number to hexdecimal form   
		cout<<"Register r"<<i<<'\t'<<hex_string<<'\n';	
	}	
	cout<<"------------------------------------------------------------------------------\n";
	return;	
}

void print_stats()
{
	cout<<"Execution Completed\nStatistics:\nTotal no. of clock cycles: "<<cycle<<"\nTotal no. of times each instructions executed:\n";
	string names[10] = {"add", "addi", "sub", "mul", "beq", "bne", "slt", "j", "lw", "sw"};
	for(int i=0; i<10; i++){
		cout<<names[i]<<'\t'<<instruction_count[i]<<'\n';
	}
	return;
}

void run_instruction()
{
	inst_data current_inst = memory[pc]; // we have already checked pc < index in run() and then called run_instruction()
	if(current_inst.name=="add")
	{
		if(current_inst.field1==31){error(current_inst.line_no, "Cannot write to $zero register"); return;}
		if(current_inst.aux) {r[current_inst.field1] = r[current_inst.field2] + r[current_inst.field3];}
		else {r[current_inst.field1] = r[current_inst.field2] + current_inst.field3;}
		pc++; cycle++;
		instruction_count[0]++;
		print_registers();	
	}
	else if(memory[pc].name=="addi")
	{
		if(current_inst.field1==31){error(current_inst.line_no, "Cannot write to $zero register"); return;}
		r[current_inst.field1] = r[current_inst.field2] + current_inst.field3;
		pc++; cycle++;
		instruction_count[1]++;
		print_registers();		
	}
	else if(memory[pc].name=="sub")
	{
		if(current_inst.field1==31){error(current_inst.line_no, "Cannot write to $zero register"); return;}
		if(current_inst.aux) {r[current_inst.field1] = r[current_inst.field2] - r[current_inst.field3];}
		else {r[current_inst.field1] = r[current_inst.field2] - current_inst.field3;}
		pc++; cycle++;
		instruction_count[2]++;
		print_registers();		
	}
	else if(memory[pc].name=="mul")
	{
		if(current_inst.field1==31){error(current_inst.line_no, "Cannot write to $zero register"); return;}
		if(current_inst.aux) {r[current_inst.field1] = r[current_inst.field2] * r[current_inst.field3];}
		else {r[current_inst.field1] = r[current_inst.field2] * current_inst.field3;}
		pc++; cycle++;
		instruction_count[3]++;
		print_registers();		
	}
	else if(memory[pc].name=="beq")
	{
		if(label_map.find(current_inst.label)==label_map.end()){
			error(current_inst.line_no, "Undeclared label name");
			return;
		}
		if(current_inst.aux){			
			if(r[current_inst.field1] == r[current_inst.field2]){
				pc = label_map[current_inst.label];					
			}
			else {pc++;}
		}
		else{
			if(r[current_inst.field1] == current_inst.field2){
				pc = label_map[current_inst.label];					
			}
			else {pc++;}
		}		
		cycle++; instruction_count[4]++;
		print_registers();	
	}
	else if(memory[pc].name=="bne")
	{
		if(label_map.find(current_inst.label)==label_map.end()){
			error(current_inst.line_no, "Undeclared label name");
			return;
		}
		if(current_inst.aux){			
			if(r[current_inst.field1] != r[current_inst.field2]){
				pc = label_map[current_inst.label];					
			}
			else {pc++;}
		}
		else{
			if(r[current_inst.field1] != current_inst.field2){
				pc = label_map[current_inst.label];					
			}
			else {pc++;}
		}		
		cycle++; instruction_count[5]++;
		print_registers();		
	}
	else if(memory[pc].name=="slt")
	{
		if(current_inst.field1==31){error(current_inst.line_no, "Cannot write to $zero register"); return;}
		if(current_inst.aux){
			if(r[current_inst.field2] < r[current_inst.field3]) {r[current_inst.field1] = 1;}
			else {r[current_inst.field1] = 0;}
		}
		else{
			if(r[current_inst.field2] < current_inst.field3) {r[current_inst.field1] = 1;}
			else {r[current_inst.field1] = 0;}
		}
		pc++; cycle++;	
		instruction_count[6]++;
		print_registers();		
	}
	else if(memory[pc].name=="j")
	{
		if(label_map.find(current_inst.label)==label_map.end()){
			error(current_inst.line_no, "Undeclared label name");
			return;					
		}
		else{
			pc = label_map[current_inst.label];					
		}
		cycle++; instruction_count[7]++;
		print_registers();	
	}
	else if(memory[pc].name=="lw")
	{
		if(current_inst.field1==31){error(current_inst.line_no, "Cannot write to $zero register"); return;}
		if(current_inst.aux){
			int addr = r[current_inst.field2] + current_inst.field3;
			if(addr < index || addr >= size){
				error(current_inst.line_no, "Invalid memory access");
				return;
			}
			else{r[current_inst.field1] = memory[addr].value;}			
		}
		else{
			if(current_inst.field2 < index || current_inst.field2 >= size){
				error(current_inst.line_no, "Invalid memory access");
				return;
			}
			else{r[current_inst.field1] = memory[current_inst.field2].value;}
		}
		pc++; cycle++;
		instruction_count[8]++;
		print_registers();		
	}
	else if(memory[pc].name=="sw")
	{
		if(current_inst.aux){		
			int addr = r[current_inst.field2] + current_inst.field3;
			if(addr < index || addr >= size){
				error(current_inst.line_no, "Invalid memory access");
				return;
			}
			else{memory[addr] = inst_data(false, "", -1, -1, -1, "", false, r[current_inst.field1], -1);}			
		}
		else{
			if(current_inst.field2 < index || current_inst.field2 >= size){
				error(current_inst.line_no, "Invalid memory access");
				return;
			}
			else{memory[current_inst.field2] = inst_data(false, "", -1, -1, -1, "", false, r[current_inst.field1], -1);}
		}		
		pc++; cycle++;
		instruction_count[9]++;
		print_registers();		
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
	print_registers();
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
	print_stats();
	return;	
}

int main(int argc, char** argv) 
{ 
    if (argc != 2)
    {
    	cout<<"Please give the filename as an argument\n";
    	return -1;	
    }
    ifstream my_file;
    my_file.open(argv[1], ios::in);
    if (my_file.is_open())
    {
    	string line;
    	int line_no = 0;
    	while(getline(my_file, line))
    	{
    		line_no++;
    		istringstream ss(line);
    		string word;
    		ss >> word;
    		parse_line(ss, word, line_no);   		
    	}
    	my_file.close();
    	run();   	
    }
    else
    {
    	cout<<"File not found\n";
    	return -1;
    }  
    return 0; 
} 
