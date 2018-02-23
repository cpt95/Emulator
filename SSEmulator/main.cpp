/// includes
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <unordered_map>
#include <queue>
#include <ctime>
#include <ctype.h>
#include <thread>
#include <mutex>

/// defines
#define SP R[0x10]
#define PC R[0x11]
#define REG_NUM 18
#define ADDITIONAL_SIZE 1024
#define O_per_addr 128
#define I_per_addr 132
#define O_per 136
#define I_per 137
#define STACK_START 138
#define STACK_SIZE 512
#define IVT_RET_ROUTINE 650
#define ENDLESS_LOOP 654
#define PROGRAM_START 662

/// global variables
unsigned char* mem;
unsigned int* IVT;
int R[REG_NUM];

char in_key;
std::queue<char> input_queue;
std::mutex lck;
bool read_flag = true;

/// relocation table row
struct reloc_table_row {

	// fields
	unsigned int addr;
	char type;
	int num;

	// constructor
	reloc_table_row(unsigned int addr, char type, int num) :
		addr(addr), type(type), num(num) {
	}
};

/// symbol table row
struct sym_table_row {

	// fields
	std::string type;
	int num;
	std::string name;
	int seg_num;
	int addr_val;
	int seg_size;
	char flag;

	// constructor
	sym_table_row(std::string type, int num, std::string name, int seg_num, int addr_val, char flag) :
		type(type), num(num), name(name), seg_num(seg_num), addr_val(addr_val), flag(flag) {
	}

	// constructor
	sym_table_row(std::string type, int num, std::string name, int seg_num, int addr_val, int seg_size, char flag) :
		type(type), num(num), name(name), seg_num(seg_num), addr_val(addr_val), seg_size(seg_size), flag(flag) {
	}
};

/// segment list element struct and list functions
struct seg_elem {

	// fields
	std::string name;
	unsigned int seg_start; // 0 for non-ORG segment !!!
	unsigned int seg_size;
	std::vector<unsigned char> machine_code;
	std::queue<reloc_table_row> reloc_table;
	seg_elem* next;

	seg_elem() :
		next(nullptr) {
	}

	seg_elem(unsigned int seg_start, unsigned int seg_size) :
		seg_start(seg_start), seg_size(seg_size), next(nullptr) {
	}
};

void add_in_front(seg_elem* &list, seg_elem* elem) {

	elem->next = list;
	list = elem;
}

void add_sorted(seg_elem** list, seg_elem* elem) {

	seg_elem** curr = list;

	while (*curr) {

		if ((*curr)->seg_start < elem->seg_start) {
			curr = &(*curr)->next;
		}
		else {
			break;
		}
	}

	elem->next = *curr;
	*curr = elem;
}

void merge_lists(std::unordered_map<std::string, sym_table_row*> &sym_table_unord, seg_elem* sorted_list, seg_elem* unsorted_list) {

	seg_elem* curr_to_insert = unsorted_list;
	if (unsorted_list) {
		unsorted_list = unsorted_list->next;
	}

	while (curr_to_insert) {

		seg_elem* curr_pos = sorted_list;

		for (;;) {

			if (curr_pos->next->seg_start - curr_pos->seg_start - curr_pos->seg_size >= curr_to_insert->seg_size) {
				curr_to_insert->seg_start = curr_pos->seg_start + curr_pos->seg_size;
				sym_table_unord[curr_to_insert->name]->addr_val = curr_to_insert->seg_start; // !!!!!!!!!!!!!

				curr_to_insert->next = curr_pos->next;
				curr_pos->next = curr_to_insert;
				break;
			}

			curr_pos = curr_pos->next;
		}

		curr_to_insert = unsorted_list;
		if (unsorted_list) {
			unsorted_list = unsorted_list->next;
		}
	}
}

/// operation code handlers
unsigned int handle_addres_type(unsigned char addr_type, unsigned char reg) {

	switch (addr_type) {

		unsigned int temp;

	case 0x04: // immed
		temp = *(unsigned int*)(&mem[PC]); // da li ovde treba read flag, trebalo bi da se ne pozove nikad, ali moze !!?!?!??!!?/
		PC += 4;

		return temp;
	case 0x00: // reg dir
		return R[reg];
		break;
	case 0x06: // mem dir
		temp = *(unsigned int*)(&mem[PC]);
		PC += 4;

		if (temp == I_per) {
			read_flag = true;
		}

		return mem[temp];
		break;
	case 0x02: // reg ind
		if (R[reg] == I_per) {
			read_flag = true;
		}

		return mem[R[reg]];
		break;
	case 0x07: // reg ind off
		temp = *(unsigned int*)(&mem[PC]);
		PC += 4;

		if (R[reg + temp] == I_per) {
			read_flag = true;
		}

		return mem[R[reg] + temp];
		break;
	default:
		SP += 4;
		*(int*)&mem[SP] = PC;
		PC = IVT[3];
		break;
	}
}

unsigned int handle_addres_type_jmp(unsigned char addr_type, unsigned char reg) {

	switch (addr_type) {

		unsigned int temp;

	case 0x06: // mem dir
		temp = *(unsigned int*)(&mem[PC]);
		PC += 4;

		return temp;
		break;
	case 0x02: // reg ind
		return R[reg];
		break;
	case 0x07: // reg ind off
		temp = *(unsigned int*)(&mem[PC]);
		PC += 4;

		return R[reg] + temp;
		break;
	default:
		SP += 4;
		*(int*)&mem[SP] = PC;
		PC = IVT[3];
		break;
	}
}

// check
unsigned int handle_addres_type_store(unsigned char addr_type, unsigned char reg) {

	switch (addr_type) {

		unsigned int temp;

	case 0x06: // mem dir
		temp = *(unsigned int*)(&mem[PC]);
		PC += 4;

		return temp;
		break;
	case 0x02: // reg ind
		return R[reg];
		break;
	case 0x07: // reg ind off
		temp = *(unsigned int*)(&mem[PC]);
		PC += 4;

		return R[reg] + temp;
		break;
	default:
		SP += 4;
		*(int*)&mem[SP] = PC;
		PC = IVT[3];
		break;
	}
}

void process_op_code(unsigned char op_code) {

	switch (op_code) {

		unsigned char addr_type;
		unsigned char instr_type;
		unsigned char reg0;
		unsigned char reg1;
		unsigned char reg2;
		unsigned int second_word;
		
	// Instrukcije za kontrolu toka
	case 0x00: // INT
		// get addr type
		op_code = mem[PC++];
		addr_type = op_code >> 5;

		// get first reg
		reg0 = op_code & 0x1F;

		// skip two bytes
		PC += 2;

		// hadle address type
		second_word = handle_addres_type(addr_type, reg0);

		// execute instruction
		if (second_word == 0) {
			exit(5);
		}

		SP += 4;
		*(int*)&mem[SP] = PC;

		// jump on interrupt
		PC = IVT[second_word];
		break;
	case 0x02: // JMP
		// get addr type
		op_code = mem[PC++];
		addr_type = op_code >> 5;

		// get first reg
		reg0 = op_code & 0x1F;

		// skip two bytes
		PC += 2;

		// hadle address type
		second_word = handle_addres_type_jmp(addr_type, reg0);

		// execute instruction
		PC = second_word; // ?????????? apsolutni ili relativni skok u zavisnosti od adresiranja?????
		break;
	case 0x03: // CALL
		// get addr type
		op_code = mem[PC++];
		addr_type = op_code >> 5;

		// get first reg
		reg0 = op_code & 0x1F;

		// skip two bytes
		PC += 2;

		// hadle address type
		second_word = handle_addres_type_jmp(addr_type, reg0);

		// execute instruction
		SP += 4;
		*(int*)&mem[SP] = PC; // ??
		PC = second_word; // ???
		break;
	case 0x01: // RET
		// skip two bytes
		PC += 3;

		// execute instruction
		if (SP == STACK_START) { // ???????????????????????????????????????????///
			std::cout << R[0] << std::endl;
			exit(4);
			PC = ENDLESS_LOOP;
			break;
		}

		PC = *(int*)&mem[SP]; // ???
		SP -= 4;
		break;
	case 0x04: // JZ
		// get addr type
		op_code = mem[PC++];
		addr_type = op_code >> 5;

		// get first reg
		reg0 = op_code & 0x1F;

		// get second reg
		op_code = mem[PC++];
		reg1 = op_code >> 3;

		// skip one byte
		++PC;

		// hadle address type
		second_word = handle_addres_type_jmp(addr_type, reg1);

		// execute instruction
		if (R[reg0] == 0)
			PC = second_word;
		break;
	case 0x05: // JNZ
		// get addr type
		op_code = mem[PC++];
		addr_type = op_code >> 5;

		// get first reg
		reg0 = op_code & 0x1F;

		// get second reg
		op_code = mem[PC++];
		reg1 = op_code >> 3;

		// skip one byte
		++PC;

		// hadle address type
		second_word = handle_addres_type_jmp(addr_type, reg1);

		// execute instruction
		if (R[reg0] != 0)
			PC = second_word;
		break;
	case 0x06: // JGZ
		// get addr type
		op_code = mem[PC++];
		addr_type = op_code >> 5;

		// get first reg
		reg0 = op_code & 0x1F;

		// get second reg
		op_code = mem[PC++];
		reg1 = op_code >> 3;

		// skip one byte
		++PC;

		// hadle address type
		second_word = handle_addres_type_jmp(addr_type, reg1);

		// execute instruction
		if (R[reg0] > 0)
			PC = second_word;
		break;
	case 0x07: // JGEZ
		// get addr type
		op_code = mem[PC++];
		addr_type = op_code >> 5;

		// get first reg
		reg0 = op_code & 0x1F;

		// get second reg
		op_code = mem[PC++];
		reg1 = op_code >> 3;

		// skip one byte
		++PC;

		// hadle address type
		second_word = handle_addres_type_jmp(addr_type, reg1);

		// execute instruction
		if (R[reg0] >= 0)
			PC = second_word;
		break;
	case 0x08: // JLZ
		// get addr type
		op_code = mem[PC++];
		addr_type = op_code >> 5;

		// get first reg
		reg0 = op_code & 0x1F;

		// get second reg
		op_code = mem[PC++];
		reg1 = op_code >> 3;

		// skip one byte
		++PC;

		// hadle address type
		second_word = handle_addres_type_jmp(addr_type, reg1);

		// execute instruction
		if (R[reg0] < 0)
			PC = second_word;
		break;
	case 0x09: // JLEZ
		// get addr type
		op_code = mem[PC++];
		addr_type = op_code >> 5;

		// get first reg
		reg0 = op_code & 0x1F;

		// get second reg
		op_code = mem[PC++];
		reg1 = op_code >> 3;

		// skip one byte
		++PC;

		// hadle address type
		second_word = handle_addres_type_jmp(addr_type, reg1);

		// execute instruction
		if (R[reg0] <= 0)
			PC = second_word;
		break;
	// Load/Store instrukcije
	case 0x10: // LOAD
		// get addr type
		op_code = mem[PC++];
		addr_type = op_code >> 5;

		// get first reg
		reg0 = op_code & 0x1F;

		// get second reg
		op_code = mem[PC++];
		reg1 = op_code >> 3;

		// get instruction type
		op_code = mem[PC++];
		instr_type = (op_code >> 3) & 0x07;

		// hadle address type
		second_word = handle_addres_type(addr_type, reg1);

		// execute instruction
		switch (instr_type) { // little endian check ??!?!?!?!?!!!!!, check in assembler for DW little endian
		case 0x00: // D
			R[reg0] = *(int*)(&second_word);
			break;
		case 0x01: // UW
			R[reg0] = *(unsigned short*)(&second_word);
			break;
		case 0x05: // SW
			R[reg0] = *(short*)(&second_word);
			break;
		case 0x03: // UB
			R[reg0] = *(unsigned char*)(&second_word);
			break;
		case 0x07: // SB
			R[reg0] = *(char*)(&second_word);
			break;
		default:
			SP += 4;
			*(int*)&mem[SP] = PC;
			PC = IVT[3];
			break;
		}
		break;
	case 0x11: // STORE
		// get addr type
		op_code = mem[PC++];
		addr_type = op_code >> 5;

		// get first reg
		reg0 = op_code & 0x1F;

		// get second reg
		op_code = mem[PC++];
		reg1 = op_code >> 3;

		// get instruction type
		op_code = mem[PC++];
		instr_type = (op_code >> 3) & 0x07;

		// hadle address type
		if (addr_type == 0x00) { // check
			*(unsigned int*)&R[reg1] = R[reg0];
			break;
		}
		else {
			second_word = handle_addres_type_store(addr_type, reg1);
		}

		// execute instruction
		switch (instr_type) {
		case 0x00: // D
			*(int*)(&mem[second_word]) = R[reg0];
			break;
		case 0x01: // UW
			*(unsigned short*)(&mem[second_word]) = R[reg0];
			break;
		case 0x03: // UB
			*(unsigned char*)(&mem[second_word]) = R[reg0];

			if (second_word == O_per) { // check
				std::cout << (char)R[reg0];
			}
			break;
		default:
			SP += 4;
			*(int*)&mem[SP] = PC;
			PC = IVT[3];
			break;
		}
		break;
	// Instrukcije za rad sa stekom
	case 0x20: // PUSH
		// get first reg
		op_code = mem[PC];
		reg0 = op_code & 0x1F;

		// skip two bytes
		PC += 3;

		// execute instruction
		SP += 4;
		*(int*)&mem[SP] = R[reg0];
		break;
	case 0x21: // POP
		// get first reg
		op_code = mem[PC];
		reg0 = op_code & 0x1F;

		// skip two bytes
		PC += 3;

		// execute instruction
		R[reg0] = *(int*)&mem[SP];
		SP -= 4;
		break;
	// Aritmetičke i logičke instrukcije
	case 0x30: // ADD
		// get first reg
		op_code = mem[PC++];
		reg0 = op_code & 0x1F;

		// get second reg
		op_code = mem[PC++];
		reg1 = op_code >> 3;
		
		//get third reg
		reg2 = (op_code << 2) & 0x1F;
		op_code = mem[PC++];
		reg2 |= (op_code >> 6);

		// execute instruction
		R[reg0] = R[reg1] + R[reg2];
		break;
	case 0x31: // SUB
		// get first reg
		op_code = mem[PC++];
		reg0 = op_code & 0x1F;

		// get second reg
		op_code = mem[PC++];
		reg1 = op_code >> 3;

		//get third reg
		reg2 = (op_code << 2) & 0x1F;
		op_code = mem[PC++];
		reg2 |= (op_code >> 6);

		// execute instruction
		R[reg0] = R[reg1] - R[reg2];
		break;
	case 0x32: // MUL
		// get first reg
		op_code = mem[PC++];
		reg0 = op_code & 0x1F;

		// get second reg
		op_code = mem[PC++];
		reg1 = op_code >> 3;

		//get third reg
		reg2 = (op_code << 2) & 0x1F;
		op_code = mem[PC++];
		reg2 |= (op_code >> 6);

		// execute instruction
		R[reg0] = R[reg1] * R[reg2];
		break;
	case 0x33: // DIV
		// get first reg
		op_code = mem[PC++];
		reg0 = op_code & 0x1F;

		// get second reg
		op_code = mem[PC++];
		reg1 = op_code >> 3;

		//get third reg
		reg2 = (op_code << 2) & 0x1F;
		op_code = mem[PC++];
		reg2 |= (op_code >> 6);

		// execute instruction
		R[reg0] = R[reg1] / R[reg2];
		break;
	case 0x34: // MOD
		// get first reg
		op_code = mem[PC++];
		reg0 = op_code & 0x1F;

		// get second reg
		op_code = mem[PC++];
		reg1 = op_code >> 3;

		//get third reg
		reg2 = (op_code << 2) & 0x1F;
		op_code = mem[PC++];
		reg2 |= (op_code >> 6);

		// execute instruction
		R[reg0] = R[reg1] % R[reg2];
		break;
	case 0x35: // AND
		// get first reg
		op_code = mem[PC++];
		reg0 = op_code & 0x1F;

		// get second reg
		op_code = mem[PC++];
		reg1 = op_code >> 3;

		//get third reg
		reg2 = (op_code << 2) & 0x1F;
		op_code = mem[PC++];
		reg2 |= (op_code >> 6);

		// execute instruction
		R[reg0] = R[reg1] & R[reg2];
		break;
	case 0x36: // OR
		// get first reg
		op_code = mem[PC++];
		reg0 = op_code & 0x1F;

		// get second reg
		op_code = mem[PC++];
		reg1 = op_code >> 3;

		//get third reg
		reg2 = (op_code << 2) & 0x1F;
		op_code = mem[PC++];
		reg2 |= (op_code >> 6);

		// execute instruction
		R[reg0] = R[reg1] | R[reg2];
		break;
	case 0x37: // XOR
		// get first reg
		op_code = mem[PC++];
		reg0 = op_code & 0x1F;

		// get second reg
		op_code = mem[PC++];
		reg1 = op_code >> 3;

		//get third reg
		reg2 = (op_code << 2) & 0x1F;
		op_code = mem[PC++];
		reg2 |= (op_code >> 6);

		// execute instruction
		R[reg0] = R[reg1] ^ R[reg2];
		break;
	case 0x38: // NOT
		// get first reg
		op_code = mem[PC++];
		reg0 = op_code & 0x1F;

		// get second reg
		op_code = mem[PC++];
		reg1 = op_code >> 3;

		// skip one byte
		++PC;

		// execute instruction
		R[reg0] = -R[reg1];
		break;
	case 0x39: // ASL
		// get first reg
		op_code = mem[PC++];
		reg0 = op_code & 0x1F;

		// get second reg
		op_code = mem[PC++];
		reg1 = op_code >> 3;

		//get third reg
		reg2 = (op_code << 2) & 0x1F;
		op_code = mem[PC++];
		reg2 |= (op_code >> 6);

		// execute instruction
		R[reg0] = R[reg1] << R[reg2];
		break;
	case 0x3A: // ASR
		// get first reg
		op_code = mem[PC++];
		reg0 = op_code & 0x1F;

		// get second reg
		op_code = mem[PC++];
		reg1 = op_code >> 3;

		//get third reg
		reg2 = (op_code << 2) & 0x1F;
		op_code = mem[PC++];
		reg2 |= (op_code >> 6);

		// execute instruction
		R[reg0] = R[reg1] >> R[reg2];
		break;
	default:
		SP += 4;
		*(int*)&mem[SP] = PC;
		PC = IVT[3];
		break;
	}
}

/// init
void init_mem(unsigned int mem_size) {

	mem = new unsigned char[mem_size]; // !?!?
}

void init_IVT() {

	IVT = (unsigned int*)mem;
	IVT[0] = STACK_START; // ???????????
	IVT[3] = IVT_RET_ROUTINE;
	IVT[4] = IVT_RET_ROUTINE;
	IVT[5] = IVT_RET_ROUTINE;

	mem[IVT_RET_ROUTINE] = 0x01;

	mem[ENDLESS_LOOP] = 0x02;
	*(int*)&mem[ENDLESS_LOOP + 4] = ENDLESS_LOOP;
}

void init_O_I() {

	*(int*)&mem[O_per_addr] = O_per;
	*(int*)&mem[I_per_addr] = I_per;
}

void init_stack() {

	SP = STACK_START;
}

void init_PC(int start_addr) {

	PC = start_addr;
}

/// keyboard listener function
void keyboard_event() {
	
	for (;;) {

		// get key
		std::cin.get(in_key);

		lck.lock();

		input_queue.push(in_key);

		lck.unlock();
	}
}

/// main
int main(int argc, char** argv) {
	
	// variables
	std::ifstream in_file(argv[1]);
	std::string temp;
	std::unordered_map<std::string, sym_table_row*> sym_table_unord;
	std::vector<sym_table_row*> sym_table;
	unsigned int mem_bound = 0;
	unsigned int mem_size = 0;

	// skip first line
	std::getline(in_file, temp);

	// read symbol table
	in_file >> temp;
	while (temp == "SEG" || temp == "SYM") {

		if (temp == "SYM") {
			int num, seg_num;
			std::string addr_val;
			char flag;
			std::string name;

			in_file >> num >> name >> seg_num >> addr_val >> flag;

			sym_table.push_back(new sym_table_row(temp, num, name, seg_num, std::stoi(addr_val, nullptr, 16), flag));
			sym_table_unord[name] = sym_table[sym_table.size() - 1];

			in_file >> temp;
		}
		else if (temp == "SEG") {
			int num, seg_num, addr_val_num, seg_size_num;
			std::string addr_val, seg_size;
			char flag;
			std::string name;

			in_file >> num >> name >> seg_num >> addr_val >> seg_size >> flag;

			addr_val_num = std::stoi(addr_val, nullptr, 16);
			seg_size_num = std::stoi(seg_size, nullptr, 16);

			sym_table.push_back(new sym_table_row(temp, num, name, seg_num, addr_val_num, seg_size_num, flag));
			sym_table_unord[name] = sym_table[sym_table.size() - 1];

			mem_size += seg_size_num;
			mem_bound = mem_bound < addr_val_num + seg_size_num ? addr_val_num + seg_size_num : mem_bound;

			in_file >> temp;
		}
	}

	// init mem, IVT, init I/O, stack
	unsigned int mem_limit = (mem_size > mem_bound ? mem_size : mem_bound) + ADDITIONAL_SIZE; // treba da se stavi najveci org + size te sekcije
	init_mem(mem_limit);
	init_IVT();
	init_O_I();
	init_stack();

	// variables
	seg_elem* curr_seg = new seg_elem();

	seg_elem* org_list = new seg_elem(PROGRAM_START, 0);
	org_list->next = new seg_elem(mem_limit, 0); // sigurno ce imati next pri ubacivanju u sortiranu listu
	seg_elem* non_org_list = nullptr;

	std::string start_seg_name;
	bool org_start_seg = false;

	// read reloc tables and machine code
	while (temp != "#end") {

		if (*temp.begin() == '#') {
			in_file >> temp;
			while (*temp.begin() != '#' && *temp.begin() != '.') {

				char type;
				int num;

				in_file >> type >> num;

				curr_seg->reloc_table.emplace(std::stoi(temp, nullptr, 16), type, num);

				in_file >> temp;
			}
		}
		else if (*temp.begin() == '.') {
			std::string curr_seg_name = temp;

			if (sym_table_unord[curr_seg_name]->seg_num == sym_table_unord["START"]->seg_num) {
				start_seg_name = curr_seg_name;
				if (sym_table_unord[curr_seg_name]->addr_val > 0) {
					org_start_seg = true;
				}
			}

			in_file >> temp;
			while (*temp.begin() != '#' && *temp.begin() != '.') {

				curr_seg->machine_code.push_back(std::stoi(temp, nullptr, 16));

				in_file >> temp;
			}

			// add current segment in appropriate list
			curr_seg->seg_start = sym_table_unord[curr_seg_name]->addr_val;
			curr_seg->seg_size = sym_table_unord[curr_seg_name]->seg_size;
			curr_seg->name = curr_seg_name;

			if (curr_seg->seg_start > 0) {
				add_sorted(&org_list, curr_seg);
			}
			else {
				add_in_front(non_org_list, curr_seg);
			}

			curr_seg = new seg_elem();
		}
	}
	
	// add elements form non_org_list to org_lsit
	merge_lists(sym_table_unord, org_list, non_org_list);

	// add machine code in memory
	curr_seg = org_list;
	while (curr_seg) {
		
		for (int it = 0; it < curr_seg->machine_code.size(); ++it) {
			
			mem[curr_seg->seg_start + it] = curr_seg->machine_code[it];
		}

		curr_seg = curr_seg->next;
	}

	// apply reloc tables
	// check !!!
	curr_seg = org_list;
	while (curr_seg) {
		// apply reloc table
		while (curr_seg->reloc_table.size() > 0) { // this will skip first and last elements, because they are not segments !!!!!!!!!!!!

			unsigned int reloc_addr = curr_seg->seg_start + curr_seg->reloc_table.front().addr;

			if (curr_seg->reloc_table.front().type == 'A' && sym_table[curr_seg->reloc_table.front().num - 1]->flag == 'L') {
				*(int*)&mem[reloc_addr] += sym_table[curr_seg->reloc_table.front().num - 1]->addr_val;
			}
			else if (curr_seg->reloc_table.front().type == 'A' && sym_table[curr_seg->reloc_table.front().num - 1]->flag == 'G' && sym_table[curr_seg->reloc_table.front().num - 1]->seg_num != 0) { // seg num ??
				*(int*)&mem[reloc_addr] += (sym_table[sym_table[curr_seg->reloc_table.front().num - 1]->seg_num - 1]->addr_val + sym_table[curr_seg->reloc_table.front().num - 1]->addr_val); // check
			}
			else if (curr_seg->reloc_table.front().type == 'R' && sym_table[curr_seg->reloc_table.front().num - 1]->flag == 'L') {
				*(int*)&mem[reloc_addr] += (sym_table[curr_seg->reloc_table.front().num - 1]->addr_val - reloc_addr - 4);
			}
			else if (curr_seg->reloc_table.front().type == 'R' && sym_table[curr_seg->reloc_table.front().num - 1]->flag == 'G' && sym_table[curr_seg->reloc_table.front().num - 1]->seg_num != 0) { // seg num ??
				*(int*)&mem[reloc_addr] += (sym_table[sym_table[curr_seg->reloc_table.front().num - 1]->seg_num - 1]->addr_val - reloc_addr - 4 + sym_table[curr_seg->reloc_table.front().num - 1]->addr_val); // check
			}

			curr_seg->reloc_table.pop();
		}

		curr_seg = curr_seg->next;
	}

	// init PC
	int start_pc;
	if (org_start_seg) {
		start_pc = sym_table_unord["START"]->addr_val;
	}
	else {
		start_pc = sym_table_unord[start_seg_name]->addr_val + sym_table_unord["START"]->addr_val;
	}
	init_PC(start_pc);

	// create thread to listen events
	std::thread first(keyboard_event); // !!!!!!!!!!!!!!!!!!!!!!!!!!!

	// init clock
	clock_t last_clock = clock();

	// main loop
	for (;;) {

		// read operation code
		unsigned char op_code = mem[PC++];
		process_op_code(op_code);

		if (input_queue.size() > 0 && read_flag) {

			// push current PC
			SP += 4;
			*(int*)&mem[SP] = PC;

			// jump on interrupt
			PC = IVT[5];

			// handle input register
			lck.lock();

			mem[I_per] = input_queue.front();
			input_queue.pop();

			read_flag = false;

			lck.unlock();
		}

		// check for interrupts
		/*if ((double)(clock() - last_clock) / CLOCKS_PER_SEC > 0.1) {
			last_clock = clock();
			
			SP += 4;
			*(int*)&mem[SP] = PC;
			PC = IVT[4];
		}*/
	}

	return 0;
	std::cout << std::endl;
}
