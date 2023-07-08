#include <iostream>
#include <fstream>
#include <string>
#include <sstream>
#include <unordered_map>
#include <algorithm>
#include <vector>
#include <queue>
#include <climits> // this contains INT_MAX
#include <set> // contains multiset

#include "main.h"

using namespace std;

struct DRAM_inst
{
	bool type; // true for lw, false for sw
	int addr;
	int row;
	int data;
	int register_no;
	int priority;
	int inst_no;
	int required_cycle_no;
	DRAM_inst(){}
	DRAM_inst(bool my_type, int my_addr, int my_row, int my_data, int my_register_no, int my_priority, int my_inst_no, int my_required_cycle_no)
	{
		type = my_type; row = my_row; addr = my_addr; data = my_data; register_no = my_register_no; priority = my_priority; inst_no = my_inst_no;	required_cycle_no = my_required_cycle_no;
	}		
};

struct compare_DRAM_inst
{
    bool operator()(const DRAM_inst &a, const DRAM_inst &b) const
    {
    	if(a.priority == b.priority) return a.inst_no > b.inst_no;
        return a.priority < b.priority;
    }
};

/* Assignment 3 Global Variables (declaring variables corresponding to tentative definitions in main.h) */

inst_data memory[size];
int registers[32]; // all registers initialized to 0
int cycle;
int pc;
int index = 0;
int instruction_count[10];
unordered_map<string, int> label_map;

int ROW_ACCESS_DELAY;
int COL_ACCESS_DELAY;
int ROW_BUFFER[256];
int current_row_index = -1; // the index of the row stored in the row buffer currently
int num_buffer_updates = 0;

/* Assignment 3 Global Variables end here */

/* Assignment 4 Global Variables */

priority_queue<DRAM_inst, vector<DRAM_inst>, compare_DRAM_inst> DRAM_priority_queue;
vector<int> register_dependency_list;
int num_cycles; // number of cycles all the DRAM requests in DRAM_priority_queue will take to execute
multiset<int> rows_list; // stores the rows that will be accessed by the DRAM instructions in DRAM_priority_queue
int DRAM_inst_number = 0; // this is used to assign order to DRAM_inst objects stored in DRAM_priority_queue

/* Assignment 4 Global Variables end here */

int find_priority(int row)
{
	if(row == current_row_index) return INT_MAX;
	else return current_row_index - row;
}

void find_cycles_taken_queue(int &num_cycles, int addr) // determines number of cycles a DRAM operation at address addr will take, using the current , result stored in num_cycles
{
	int row_to_be_accessed = (addr/4)/256;
	if(rows_list.find(row_to_be_accessed) != rows_list.end()){
		num_cycles = COL_ACCESS_DELAY;
	}
	else{
		if(rows_list.empty()){
			if(current_row_index == -1){
				num_cycles = ROW_ACCESS_DELAY + COL_ACCESS_DELAY;
			}
			else{
				num_cycles = 2*ROW_ACCESS_DELAY + COL_ACCESS_DELAY;			
			}
		}
		else{
			num_cycles = 2*ROW_ACCESS_DELAY + COL_ACCESS_DELAY;		
		}
	}
}

bool dependent_queue(inst_data instruction)
{
	if(instruction.name=="addi"){
		if(find(register_dependency_list.begin(), register_dependency_list.end(), instruction.field1) != register_dependency_list.end()
		|| find(register_dependency_list.begin(), register_dependency_list.end(), instruction.field2) != register_dependency_list.end())
		return true;
		else return false;		
	}
	else if(instruction.name=="add" || instruction.name=="mul" || instruction.name=="sub" || instruction.name=="slt"){
		if(instruction.aux)
		{
			if(find(register_dependency_list.begin(), register_dependency_list.end(), instruction.field1) != register_dependency_list.end()
			|| find(register_dependency_list.begin(), register_dependency_list.end(), instruction.field2) != register_dependency_list.end()
			|| find(register_dependency_list.begin(), register_dependency_list.end(), instruction.field3) != register_dependency_list.end())
			return true;
			else return false;
		}
		else
		{
			if(find(register_dependency_list.begin(), register_dependency_list.end(), instruction.field1) != register_dependency_list.end()
			|| find(register_dependency_list.begin(), register_dependency_list.end(), instruction.field2) != register_dependency_list.end())
			return true;
			else return false;
		}
	}
	else if(instruction.name=="beq" || instruction.name=="bne"){
		if(instruction.aux)
		{
			if(find(register_dependency_list.begin(), register_dependency_list.end(), instruction.field1) != register_dependency_list.end()
			|| find(register_dependency_list.begin(), register_dependency_list.end(), instruction.field2) != register_dependency_list.end())
			return true;
			else return false;
		}
		else
		{
			if(find(register_dependency_list.begin(), register_dependency_list.end(), instruction.field1) != register_dependency_list.end())
			return true;
			else return false;
		}
	}
	else if(instruction.name=="sw" || instruction.name=="lw"){ // "lw" instruction is dependent if it wants to write to a dependent register (dependent means that register is already being written to)
									// "sw" instruction is dependent if it wants to read from a dependent register (dependent means that register is already being written to)
		if(instruction.aux){
			if(find(register_dependency_list.begin(), register_dependency_list.end(), instruction.field1) != register_dependency_list.end()
			|| find(register_dependency_list.begin(), register_dependency_list.end(), instruction.field3) != register_dependency_list.end())
			return true;
			else return false;
		}
		else{
			if(find(register_dependency_list.begin(), register_dependency_list.end(), instruction.field1) != register_dependency_list.end())
			return true;
			else return false;
		}
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

void run_DRAM_inst(DRAM_inst cur_DRAM_inst, int prev_cycle)
{
	if(cur_DRAM_inst.type)
	{
		register_dependency_list.erase(remove(register_dependency_list.begin(), register_dependency_list.end(), cur_DRAM_inst.register_no), register_dependency_list.end());
		DRAM_read(cur_DRAM_inst.addr, cur_DRAM_inst.register_no);
		string reg;
		if(cur_DRAM_inst.register_no<16){reg = "$t"+to_string(cur_DRAM_inst.register_no);}
		else{reg = "$s"+to_string(cur_DRAM_inst.register_no-16);}
		if(prev_cycle==cycle+1){cout<<"Cycle "<<prev_cycle-1<<"-"<<cycle<<":\tDRAM read\t\t\t"<<reg<<" = "<<registers[cur_DRAM_inst.register_no]<<'\n';}
		else{cout<<"Cycle "<<prev_cycle<<"-"<<cycle<<":\tDRAM read\t\t\t"<<reg<<" = "<<registers[cur_DRAM_inst.register_no]<<'\n';}	
	}
	else
	{
		DRAM_write(cur_DRAM_inst.addr, cur_DRAM_inst.data);
		if(prev_cycle==cycle+1){cout<<"Cycle "<<prev_cycle-1<<"-"<<cycle<<":\tDRAM write\t\t\tMemory address "<<cur_DRAM_inst.addr<<"-"<<cur_DRAM_inst.addr+3<<" = "<<ROW_BUFFER[(cur_DRAM_inst.addr/4)%256]<<'\n';}
		else{cout<<"Cycle "<<prev_cycle<<"-"<<cycle<<":\tDRAM write\t\t\tMemory address "<<cur_DRAM_inst.addr<<"-"<<cur_DRAM_inst.addr+3<<" = "<<ROW_BUFFER[(cur_DRAM_inst.addr/4)%256]<<'\n';}	
	}

}

void run_further_instructions_queue(); // prototyping this function so that it can be called by run_instruction_queue()

void run_DRAM_priority_queue()
{
	while(!DRAM_priority_queue.empty())
	{
		//cout<<cycle<<'\n';
		int prev_cycle = cycle+1; // prev_cycle holds the value of the first free cycle where next instruction can be run
		int prev_buffer_row = current_row_index;
		DRAM_inst cur_DRAM_inst = DRAM_priority_queue.top();
		if(cur_DRAM_inst.required_cycle_no > prev_cycle)
		{
			int cur_row = cur_DRAM_inst.row;
			multiset<int> dependent_addresses_sw;
			multiset<int> dependent_addresses_lw;
			if(cur_DRAM_inst.type) dependent_addresses_lw.insert(cur_DRAM_inst.addr);
			else dependent_addresses_sw.insert(cur_DRAM_inst.addr);
			priority_queue<DRAM_inst, vector<DRAM_inst>, compare_DRAM_inst> temp_DRAM_priority_queue;
			temp_DRAM_priority_queue.push(cur_DRAM_inst);
			DRAM_priority_queue.pop();
			DRAM_inst temp_inst; 
			if(DRAM_priority_queue.empty()){
				cout<<"Cycle "<<prev_cycle<<":\tDRAM vacant\n";
				cycle++;
				DRAM_priority_queue.push(cur_DRAM_inst);
				continue;	
			}
			else{
				temp_inst = DRAM_priority_queue.top();			
			}			
			while(temp_inst.row == cur_row){
				if(temp_inst.type){
					if(dependent_addresses_sw.find(temp_inst.addr) != dependent_addresses_sw.end()){
						cout<<"Cycle "<<prev_cycle<<":\tDRAM vacant\n";
						cycle++;
						while(!temp_DRAM_priority_queue.empty()){
							DRAM_priority_queue.push(temp_DRAM_priority_queue.top());
							temp_DRAM_priority_queue.pop();
						}
						break; // go back to run cur_DRAM_inst
					}
					else{
						if(temp_inst.required_cycle_no > prev_cycle){
							temp_DRAM_priority_queue.push(temp_inst);
							dependent_addresses_lw.insert(temp_inst.addr);
							DRAM_priority_queue.pop();
							if(!DRAM_priority_queue.empty()) temp_inst = DRAM_priority_queue.top();
							else{
								cout<<"Cycle "<<prev_cycle<<":\tDRAM vacant\n";
								cycle++;
								while(!temp_DRAM_priority_queue.empty()){
									DRAM_priority_queue.push(temp_DRAM_priority_queue.top());
									temp_DRAM_priority_queue.pop();
								}
								break; // go back to run cur_DRAM_inst 
							}
						}
						else{
							run_DRAM_inst(temp_inst, prev_cycle);
							while(!temp_DRAM_priority_queue.empty()){
								DRAM_priority_queue.push(temp_DRAM_priority_queue.top());
								temp_DRAM_priority_queue.pop();
							}
							break; // go back to run cur_DRAM_inst				
						}					
					}
				}
				else{
					if(dependent_addresses_sw.find(temp_inst.addr) != dependent_addresses_sw.end() || dependent_addresses_lw.find(temp_inst.addr) != dependent_addresses_lw.end()){
						cout<<"Cycle "<<prev_cycle<<":\tDRAM vacant\n";
						cycle++;
						while(!temp_DRAM_priority_queue.empty()){
							DRAM_priority_queue.push(temp_DRAM_priority_queue.top());
							temp_DRAM_priority_queue.pop();
						}
						break; // go back to run cur_DRAM_inst 	
					}
					else{
						if(temp_inst.required_cycle_no > prev_cycle){
							temp_DRAM_priority_queue.push(temp_inst);
							dependent_addresses_sw.insert(temp_inst.addr);
							DRAM_priority_queue.pop();
							if(!DRAM_priority_queue.empty()) temp_inst = DRAM_priority_queue.top();
							else{
								cout<<"Cycle "<<prev_cycle<<":\tDRAM vacant\n";
								cycle++;
								while(!temp_DRAM_priority_queue.empty()){
									DRAM_priority_queue.push(temp_DRAM_priority_queue.top());
									temp_DRAM_priority_queue.pop();
								}
								break; // go back to run cur_DRAM_inst 
							}
						}
						else{
							run_DRAM_inst(temp_inst, prev_cycle);
							while(!temp_DRAM_priority_queue.empty()){
								DRAM_priority_queue.push(temp_DRAM_priority_queue.top());
								temp_DRAM_priority_queue.pop();
							}
							break; // go back to run cur_DRAM_inst							
						}					
					}			
				}
			}
			if(temp_inst.row != cur_row){
				cout<<"Cycle "<<prev_cycle<<":\tDRAM vacant\n";
				cycle++;
				while(!temp_DRAM_priority_queue.empty()){
					DRAM_priority_queue.push(temp_DRAM_priority_queue.top());
					temp_DRAM_priority_queue.pop();
				}
				// go back to run cur_DRAM_inst							
			}		
		}
		else
		{
			DRAM_priority_queue.pop();
			rows_list.erase(rows_list.find(cur_DRAM_inst.row)); // removing one instance of row address from rows_list
			run_DRAM_inst(cur_DRAM_inst, prev_cycle);
			num_cycles -= cycle - prev_cycle + 1;
			if(current_row_index != prev_buffer_row) // reordering priorities so that new instuctions that may come via run_further_instructions_queue have priorities based on the same current_row_index
			{						// num_cycles, register_dependency_list and rows_list do not change
				priority_queue<DRAM_inst, vector<DRAM_inst>, compare_DRAM_inst> temp_DRAM_priority_queue;
				while(!DRAM_priority_queue.empty())
				{
					DRAM_inst temp_DRAM_inst = DRAM_priority_queue.top();
					DRAM_priority_queue.pop();
					int priority = find_priority((temp_DRAM_inst.addr/4)/256);
					temp_DRAM_priority_queue.emplace(temp_DRAM_inst.type, temp_DRAM_inst.addr, temp_DRAM_inst.row, temp_DRAM_inst.data, temp_DRAM_inst.register_no, priority, temp_DRAM_inst.inst_no, temp_DRAM_inst.required_cycle_no);
					//cout<<temp_DRAM_inst.type<<" "<<temp_DRAM_inst.addr<<" "<<temp_DRAM_inst.row<<" "<<temp_DRAM_inst.data<<" "<<(temp_DRAM_inst.register_no)<<" "<<priority<<" "<<temp_DRAM_inst.inst_no<<'\n';
				}
				DRAM_priority_queue = temp_DRAM_priority_queue;
			}		
			prev_cycle = cycle+1;
			run_further_instructions_queue();
			cycle = prev_cycle-1;
		}		
	}
}

void run_instruction_queue()
{
	inst_data current_inst = memory[pc]; // we have already checked pc < index in run_queue() and then called run_instruction_queue()
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
		int prev_cycle, addr;
		if(current_inst.field1==31){error(current_inst.line_no, "Cannot write to $zero register"); return;}
		if(current_inst.aux){addr = registers[current_inst.field2] + current_inst.field3;}
		else{addr = current_inst.field2;}
		if(addr < index || addr >= size || addr%4 != 0){
			error(current_inst.line_no, "Invalid memory access");
			return;
		}
		print_cycle_stats();
		pc++;
		
		////
		
		prev_cycle = cycle+1;		
		int priority = find_priority((addr/4)/256);
		DRAM_priority_queue.emplace(true, addr, (addr/4)/256, 0, current_inst.field1, priority, DRAM_inst_number++, cycle+1);
		register_dependency_list.push_back(current_inst.field1);
		find_cycles_taken_queue(num_cycles, addr);
		rows_list.insert((addr/4)/256); // we have calculated cycles before inserting the new row address in rows_list			
		run_further_instructions_queue();
		cycle = prev_cycle-1;
		if(!DRAM_priority_queue.empty()){cycle--; run_DRAM_priority_queue();}
		else{undefined_error(current_inst.line_no, "In lw, didn't expect DRAM_priority_queue to be empty");}
				
		////	
		
		instruction_count[8]++;
		//print_registers();
		
	}
	else if(memory[pc].name=="sw")
	{
		cycle++;
		int prev_cycle, addr;
		if(current_inst.aux){addr = registers[current_inst.field2] + current_inst.field3;}
		else{addr = current_inst.field2;}
		if(addr < index || addr >= size || addr%4 != 0){
			error(current_inst.line_no, "Invalid memory access");
			return;
		}
		print_cycle_stats();
		pc++;
		
		////
		
		prev_cycle = cycle+1;		
		int priority = find_priority((addr/4)/256);
		DRAM_priority_queue.emplace(false, addr, (addr/4)/256, registers[current_inst.field1], -1, priority, DRAM_inst_number++, cycle+1);
		find_cycles_taken_queue(num_cycles, addr);
		rows_list.insert((addr/4)/256); // we have calculated cycles before inserting the new row address in rows_list				
		run_further_instructions_queue();
		cycle = prev_cycle-1;
		if(!DRAM_priority_queue.empty()){cycle--; run_DRAM_priority_queue();}
		else{undefined_error(current_inst.line_no, "In sw, didn't expect DRAM_priority_queue to be empty");}
				
		////
		
		instruction_count[9]++;
		//print_registers();
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


void run_further_instructions_queue()
{
	if(pc >= index){return;}
	inst_data lookahead_inst = memory[pc];
	int i = 0;
	while(i < num_cycles){
		while(pc<index && lookahead_inst.name == "label"){lookahead_inst = memory[++pc];}
		if(pc<index && ! dependent_queue(lookahead_inst)){
			if(lookahead_inst.name == "lw")
			{
				cycle++;
				int addr;
				if(lookahead_inst.field1==31){error(lookahead_inst.line_no, "Cannot write to $zero register"); return;}
				if(lookahead_inst.aux){addr = registers[lookahead_inst.field2] + lookahead_inst.field3;}
				else{addr = lookahead_inst.field2;}
				if(addr < index || addr >= size || addr%4 != 0){
					error(lookahead_inst.line_no, "Invalid memory access");
					return;
				}
				print_cycle_stats();
				instruction_count[8]++;
				int priority = find_priority((addr/4)/256);
				DRAM_priority_queue.emplace(true, addr, (addr/4)/256, 0, lookahead_inst.field1, priority, DRAM_inst_number++, cycle+1);
				register_dependency_list.push_back(lookahead_inst.field1);
				int temp;
				find_cycles_taken_queue(temp, addr);
				num_cycles += temp;
				rows_list.insert((addr/4)/256);
				pc++;			
			}
			else if(lookahead_inst.name == "sw")
			{
				cycle++;
				int addr;
				if(lookahead_inst.aux){addr = registers[lookahead_inst.field2] + lookahead_inst.field3;}
				else{addr = lookahead_inst.field2;}
				if(addr < index || addr >= size || addr%4 != 0){
					error(lookahead_inst.line_no, "Invalid memory access");
					return;
				}
				print_cycle_stats();
				instruction_count[9]++;
				int priority = find_priority((addr/4)/256);
				DRAM_priority_queue.emplace(false, addr, (addr/4)/256, registers[lookahead_inst.field1], -1, priority, DRAM_inst_number++, cycle+1);
				int temp;
				find_cycles_taken_queue(temp, addr);
				num_cycles += temp;
				rows_list.insert((addr/4)/256);
				pc++;
			}
			else
			{
				run_instruction_queue();
			}
			lookahead_inst = memory[pc];
			i++;					
		}
		else{
			break; // break as soon as first dependent instruction is encountered
		}				
	}	
	return;	
}

void run_queue()
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
			run_instruction_queue();
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

int main(int argc, char** argv) 
{
    if(argc != 4)
    {
    	cout<<"Please give the filename, row access delay and column access delay as arguments\n";
    	return -1;	
    }
    ROW_ACCESS_DELAY = read_int_value(argv[2], -2);
    COL_ACCESS_DELAY = read_int_value(argv[3], -2);
    cout<<"Row Access Delay: "<<ROW_ACCESS_DELAY<<"; Column Access Delay: "<<COL_ACCESS_DELAY<<'\n';
    if(ROW_ACCESS_DELAY<0 || COL_ACCESS_DELAY<0){
    	cout<<"Error: Invalid access delay\n";
    	return -1;   	
    }
    if(ROW_ACCESS_DELAY==0){
    	cout<<"Warning: Row access delay is 0\n";   	
    }
    if(COL_ACCESS_DELAY==0){
    	cout<<"Warning: Column access delay is 0\n";   	
    }
    ifstream my_file;
    my_file.open(argv[1], ios::in);
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
    		parse_line(ss, word, line_no);   		
    	}
    	my_file.close();
    	run_queue();   	
    }
    else
    {
    	cout<<"File not found\n";
    	return -1;
    }
    return 0; 
}
