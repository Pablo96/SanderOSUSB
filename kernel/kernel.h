#include "stdarg.h"

void printf(char *,...); 				//Our printf function
char* convert(unsigned int, int); 		//Convert integer number into octal, hex, etc.
unsigned char inportb (unsigned short _port);
void outportb (unsigned short _port, unsigned char _data);
unsigned short inportw(unsigned short _port);
void outportw(unsigned short _port, unsigned short _data);
unsigned long inportl(unsigned short _port);
void outportl(unsigned short _port, unsigned long _data);
void kernel_main();

// MEMORY
void *malloc(unsigned long size);

// BLOCKDEVICE
//void init_blockdevice();
//void introduceDevice(unsigned char type,unsigned long pointertodevice,unsigned long readsector,unsigned long writesector,unsigned long eject,unsigned long framesize,unsigned char* name);
//void dirdev();

// STRING
void printstring(char* msg);
void putc(char a);
void init_video();
void hexdump(unsigned long msg);

// GDT
void init_gdt();

// IDT
void init_idt();
void idt_set_gate(unsigned char num, unsigned long base, unsigned short sel, unsigned char flags);
void setErrorInt(unsigned char num,unsigned long base);
void setNormalInt(unsigned char num,unsigned long base);

// TIMER MOD
void init_timer();
int getTicks();
void resetTicks();

// PS2
void init_ps2();

// PCI
void init_pci();

// SERIAL
void init_serial();

// IDE
void init_ide(unsigned short BAR);

struct Registers{
    unsigned int gs, fs, es, ds;      /* pushed the segs last */
    unsigned int edi, esi, ebp, esp, ebx, edx, ecx, eax;  /* pushed by 'pusha' */
    unsigned int int_no, err_code;    /* our 'push byte #' and ecodes do this */
    unsigned int eip, cs, eflags, useresp, ss;   /* pushed by the processor automatically */ 
};
//void dirdev();