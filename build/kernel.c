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

struct Registers{
    unsigned int gs, fs, es, ds;      /* pushed the segs last */
    unsigned int edi, esi, ebp, esp, ebx, edx, ecx, eax;  /* pushed by 'pusha' */
    unsigned int int_no, err_code;    /* our 'push byte #' and ecodes do this */
    unsigned int eip, cs, eflags, useresp, ss;   /* pushed by the processor automatically */ 
};
unsigned char* videomemory = (unsigned char*)0xb8000;

void kernel_main(){
	init_video();
	printstring("Welcome to the Sanderslando Kernel!!\n");
	printstring("Loading core components...\n");
	printstring("=> Global Description Table...\n");
	init_gdt();
	printstring("=> Interrupt Description Table...\n");
	init_idt();
	printstring("Loading utilities...\n");
	printstring("=> Programmable Interrupt Timer...\n");
	init_timer();
	printstring("=> PS2...\n");
	init_ps2();
	printstring("=> PCI...\n");
	init_pci();
	printstring("=> Serial ports...\n");
	init_serial();
	printf("Shashwat %d sss %s",1, "test2");
	printstring("\nEnd of loading system!\n");
	for(;;);
}


//
// UHCI
//
//

unsigned long uhciframes[5];
extern void uhciirq();
volatile unsigned long uhciBAR;

void irq_uhci(){
	//printstring("UHCI: interrupt\n");
	unsigned short status = inportw(uhciBAR+0x02);
	if(status & 0b0000000000011010){
		printstring("UHCI: PANIC");
		asm volatile ("cli\nhlt");
		for(;;);
	}else if(status & 1 ){
		printstring("*");
	}
//	if(videomemory[2]=='-'){
//		videomemory[2]='\\';
//	}else if(videomemory[2]=='\\'){
//		videomemory[2]='|';
//	}else if(videomemory[2]=='|'){
//		videomemory[2]='/';
//	}else if(videomemory[2]=='/'){
//		videomemory[2]='-';
//	}else{
//		videomemory[2]='-';
//	}
//	// EOI
	outportb(0x20,0x20);
	outportb(0xA0,0x20);
}

void init_uhci_port(unsigned long BAR){
	uhciBAR = BAR;
	printstring("UHCI: initialising port at BAR ");
	hexdump(BAR);
	printstring("\n");
	printstring("UHCI: initial value ");
	hexdump(inportw(BAR));
	printstring("\n");
	outportw(BAR,0b0000000010000100);
	resetTicks();
	while(1){
		if(getTicks()==10){
			break;
		}
	}
	printstring("UHCI: end of initialising port ");
	hexdump(BAR);
	printstring(" ending initialising subroutine with value ");
	hexdump(inportw(BAR));
	printstring(" \n");
}

void init_uhci(unsigned long BAR,unsigned char intnum){
	printstring("UHCI: Initialising UHCI\n");
	if((BAR & 0b00000000000000000000000000000001) > 0 ){
		BAR--;
		printstring("UHCI: using I/O port ");
		hexdump(BAR);
		printstring(" and irq ");
		hexdump(intnum);
		printstring("\n");
	}else{
		printstring("UHCI: using memoryregister ");
		hexdump(BAR);
		printstring("\nUHCI: memoryregister not supported yet!\n");
		return;
	}
//	outportw(BAR+0xC0,0x8f00);
	outportw(BAR+0xC0,0x0000);
	unsigned short beforereset1 = inportw(BAR);
	printstring("UHCI: USBCMD register before reset: ");
	hexdump(beforereset1);
	printstring("\n");
	unsigned short beforereset2 = inportw(BAR+2);
	printstring("UHCI: USBSTS register before reset: ");
	hexdump(beforereset2);
	printstring("\n");
	outportw(BAR,0b0000000000000000);
	outportw(BAR,0b0000000000000010);
	while(1){
		volatile unsigned short duringreset = inportw(BAR);
		if((duringreset & 0b0000000000000010)==0){
			break;
		}
	}
	printstring("UHCI: reset completed!\n");
	printstring("UHCI: USBCMD register after reset: ");
	hexdump(inportw(BAR));
	printstring("\nUHCI: USBSTS register after reset: ");
	hexdump(inportw(BAR+2));
	printstring("\n");
	printstring("UHCI: default framecount register: ");
	hexdump(inportw(BAR+0x06));
	outportw(BAR+0x06,1);
	printstring(" new value: 0\n");
	outportw(BAR+4,0b0000000000001111);
    	setNormalInt(intnum,(unsigned long)uhciirq);
	printstring("UHCI: All interrupts enabled!\nUHCI: default FLBASEADD register: ");
	hexdump(inportl(BAR+0x08));
	unsigned long uhciframesloc = (unsigned long)&uhciframes;
	outportl(BAR+0x08,uhciframesloc<<12);
	uhciframes[0] = 0x00000001;
	uhciframes[1] = 0x00000001;
	uhciframes[2] = 0x00000001;
	outportw(BAR,0b0000000000000001);
}

//
// SERIAL
//
//

extern void serialirq();


unsigned int serial_received(unsigned short PORT) {
   return inportb(PORT + 5) & 1;
}
 
unsigned char read_serial(unsigned short PORT) {
   while (serial_received(PORT) == 0);
 
   return inportb(PORT);
}

int is_transmit_empty(unsigned short PORT) {
   return inportb(PORT + 5) & 0x20;
}
 
void write_serial(char a,unsigned short PORT) {
   while (is_transmit_empty(PORT) == 0);
 
   outportb(PORT,a);
}

void irq_serial(){
	unsigned char binnengekomen = read_serial(0x3f8);
	putc(binnengekomen);
	outportb(0x20,0x20);
}

void init_serial_device(unsigned short PORT) {
   outportb(PORT + 1, 0x00);    // Disable all interrupts
   outportb(PORT + 3, 0x80);    // Enable DLAB (set baud rate divisor)
   outportb(PORT + 0, 0x03);    // Set divisor to 3 (lo byte) 38400 baud
   outportb(PORT + 1, 0x00);    //                  (hi byte)
   outportb(PORT + 3, 0x03);    // 8 bits, no parity, one stop bit
   outportb(PORT + 2, 0xC7);    // Enable FIFO, clear them, with 14-byte threshold
   outportb(PORT + 4, 0x0B);    // IRQs enabled, RTS/DSR set
   outportb(PORT + 1, 1);
   setNormalInt(4,(unsigned long)serialirq);
}

void init_serial(){
	init_serial_device(0x3f8);
}

//
// PCI
//
//
#define PCI_ADDRESS 0xCF8
#define PCI_DATA 0xCFC

unsigned short pciConfigReadWord (unsigned char bus, unsigned char slot, unsigned char func, unsigned char offset) {
    unsigned long address;
    unsigned long lbus  = (unsigned long)bus;
    unsigned long lslot = (unsigned long)slot;
    unsigned long lfunc = (unsigned long)func;
    unsigned short tmp = 0;
 
    /* create configuration address as per Figure 1 */
    address = (unsigned long)((lbus << 16) | (lslot << 11) |
              (lfunc << 8) | (offset & 0xfc) | ((unsigned long)0x80000000));
 
    /* write out the address */
    outportl(PCI_ADDRESS, address);
    /* read in the data */
    /* (offset & 2) * 8) = 0 will choose the first word of the 32 bits register */
    tmp = (unsigned short)((inportl(PCI_DATA) >> ((offset & 2) * 8)) & 0xffff);
    return (tmp);
}

void init_pci(){
	printstring("PCI: detecting devices....\n");
	for(int bus = 0 ; bus < 256 ; bus++){
		for(int slot = 0 ; slot < 32 ; slot++){
			for(int function = 0 ; function < 7 ; function++){
				unsigned short vendor = pciConfigReadWord(bus,slot,function,0);
				if(vendor != 0xFFFF){
					printstring("PCI: device detected, ");
					unsigned char classc = (pciConfigReadWord(bus,slot,function,0x0A)>>8)&0xFF;
					unsigned char sublca = (pciConfigReadWord(bus,slot,function,0x0A))&0xFF;
					unsigned char subsub = (pciConfigReadWord(bus,slot,function,0x08)>>8)&0xFF;
					if(classc==0x00){
						printstring("unclassified: ");
					}else if(classc==0x01){
						printstring("mass storage device: ");
						if(sublca==0x00){
							printstring(" SCSI Bus");
						}else if(sublca==0x01){
							printstring(" IDE controller");
						}else if(sublca==0x02){
							printstring(" Floppy disk");
						}else if(sublca==0x03){
							printstring(" IPI Bus");
						}else if(sublca==0x04){
							printstring(" RAID Controller");
						}else if(sublca==0x05){
							printstring(" ATA Controller");
						}else if(sublca==0x06){
							printstring(" Serial ATA");
						}else if(sublca==0x07){
							printstring(" Serial attached SCSI");
						}else if(sublca==0x08){
							printstring(" Non-volatile memory controller");
						}else{
							printstring("UNDEFINED");
						}
					}else if(classc==0x02){
						printstring("network controller: ");
						if(sublca==0x00){
							printstring(" Ethernet controller");
						}else if(sublca==0x01){
							printstring(" Token ring controller");
						}else if(sublca==0x02){
							printstring(" FDDI controller");
						}else if(sublca==0x03){
							printstring(" ATM controller");
						}else if(sublca==0x04){
							printstring(" ISDN controller");
						}else if(sublca==0x05){
							printstring(" WorldFlip controller");
						}else if(sublca==0x06){
							printstring(" PCMG controller");
						}else if(sublca==0x07){
							printstring(" Infiniband controller");
						}else if(sublca==0x08){
							printstring(" Fabric controller");
						}else if(sublca==0x80){
							printstring(" Other controller");
						}
					}else if(classc==0x03){
						printstring("displaycontroller: ");
						if(sublca==0x00){
							printstring(" VGA controller");
						}else if(sublca==0x01){
							printstring(" XGA controller");
						}else if(sublca==0x02){
							printstring(" 3D controller");
						}else if(sublca==0x80){
							printstring(" Other controller");
						}
					}else if(classc==0x04){
						printstring("multimedia controller: ");
					}else if(classc==0x05){
						printstring("memory controller: ");
						if(sublca==0x00){
							printstring(" RAM controller");
						}else if(sublca==0x01){
							printstring(" FLASH controller");
						}else if(sublca==0x80){
							printstring(" Other controller");
						}
					}else if(classc==0x06){
						printstring("bridge device: ");
						if(sublca==0x00){
							printstring(" host bridge");
						}else if(sublca==0x01){
							printstring(" ISA bridge");
						}else if(sublca==0x02){
							printstring(" EISA bridge");
						}else if(sublca==0x03){
							printstring(" MCA");
						}else if(sublca==0x04){
							printstring(" PCI to PCI bridge");
						}else if(sublca==0x05){
							printstring(" PCMCIA bridge");
						}else if(sublca==0x06){
							printstring(" NuBus bridge");
						}else if(sublca==0x07){
							printstring(" CardBus bridge");
						}else if(sublca==0x08){
							printstring(" RACEWay bridge");
						}else if(sublca==0x09){
							printstring(" PCI to PCI bridge");
						}else if(sublca==0x0A){
							printstring(" Infiniband to PCI bridge");
						}else if(sublca==0x80){
							printstring(" bridge");
						}
					}else if(classc==0x07){
						printstring("simple communication controller: ");
					}else if(classc==0x08){
						printstring("base system peripel: ");
					}else if(classc==0x09){
						printstring("inputdevice: ");
					}else if(classc==0x0A){
						printstring("docking station: ");
					}else if(classc==0x0B){
						printstring("processor: ");
					}else if(classc==0x0C){
						printstring("serial buss controller: ");
						if(sublca==0x00){
							printstring(" FireWire controller");
						}else if(sublca==0x01){
							printstring(" Access controller");
						}else if(sublca==0x02){
							printstring(" SSA controller");
						}else if(sublca==0x03){
							printstring(" USB controller, ");
							if(subsub==0x00){
								printstring("UHCI [USB 1]\n");
								unsigned short BAR4E[2];
								BAR4E[0] = pciConfigReadWord(bus,slot,function,0x20);
								BAR4E[1] = pciConfigReadWord(bus,slot,function,0x22);
								unsigned long BAR4 = ((unsigned long*)BAR4E)[0];
								unsigned char irqlne = (pciConfigReadWord(bus,slot,function,0x3C))&0xFF;
								init_uhci(BAR4,irqlne);
							}else if(subsub==0x10){
								printstring("OHCI [USB 1]");
							}else if(subsub==0x20){
								printstring("EHCI [USB 2]");
							}else if(subsub==0x30){
								printstring("XHCI [USB 3]");
							}else if(subsub==0x80){
								printstring("unspecified");
							}else if(subsub==0xFE){
								printstring("devicecontroller");
							}else{
								printstring("unknown");
							}
						}else if(sublca==0x04){
							printstring(" fibre controller");
						}else if(sublca==0x05){
							printstring(" SMBus controller");
						}else if(sublca==0x06){
							printstring(" Infiniband controller");
						}else if(sublca==0x07){
							printstring(" IPMI controller");
						}else if(sublca==0x08){
							printstring(" SERCOS controller");
						}else if(sublca==0x09){
							printstring(" CANbus controller");
						}else if(sublca==0x80){
							printstring(" Other controller");
						}
					}else if(classc==0x0D){
						printstring("wireless controller: ");
					}else if(classc==0x0E){
						printstring("inteligent controller: ");
					}else if(classc==0x0F){
						printstring("satalite controller: ");
					}else if(classc==0x10){
						printstring("encryption controller: ");
					}else if(classc==0x11){
						printstring("signal controller: ");
					}else if(classc==0x12){
						printstring("accelerator controller: ");
					}else if(classc==0x13){
						printstring("non essential controller: ");
					}else if(classc==0x40){
						printstring("co processor controller: ");
					}else if(classc==0xFF){
						printstring("unassigned controller: ");
					}else{
						printstring("UNKNOWN");
					}
					printstring("\n");
				}
			}
		}
	}
}

//
// PS2
//
//

#define PS2_DATA 0x60
#define PS2_STATUS 0x64
#define PS2_COMMAND 0x64
#define PS2_TIMEOUT 10

char getPS2StatusRegisterText(){
	return inportb(PS2_STATUS);
}

int getPS2ReadyToRead(){
	return getPS2StatusRegisterText() & 0b00000001;
}

int getPS2ReadyToWrite(){
	return getPS2StatusRegisterText() & 0b00000010;
}

int writeToFirstPS2Port(unsigned char data){
	resetTicks();
	while(getPS2ReadyToWrite()>0){
		if(getTicks()==PS2_TIMEOUT){
			return 0;
		}
	}
	outportb(PS2_DATA,data);
	return 1;
}

int writeToSecondPS2Port(unsigned char data){
	outportb(PS2_COMMAND,0xD4);
	resetTicks();
	while(getPS2ReadyToWrite()>0){
		if(getTicks()==PS2_TIMEOUT){return 0;}
	}
	outportb(PS2_DATA,data);
	return 1;
}

int waitforps2ok(){
	resetTicks();
	while(inportb(PS2_DATA)!=0xFA){
		if(getTicks()==PS2_TIMEOUT){
			return 0;
		}
	}
	return 1;
}

void printps2devicetype(unsigned char a){
	if(a==0x00){
		printstring("PS2: standard ps/2 mouse\n");
	}else if(a==0x03){
		printstring("PS2: mouse with scroll wheel\n");
	}else if(a==0x04){
		printstring("PS2: 5 button mouse\n");
	}else if(a==0xab||a==0x41||a==0xab||a==0xc1||a==0x83){
		printstring("PS2: keyboard\n");
	}
}

extern void mouseirq();
extern void keyboardirq();

int csr_y = 12;
int csr_x = 40;
volatile int csr_t = 0;
volatile int ccr_x = 50;
volatile int ccr_y = 50;
volatile int ccr_a = 0;
volatile int ccr_b = 0;
void irq_mouse(){
	if(csr_t==0){
		char A = inportb(PS2_DATA);
		if(ccr_b){
			if((ccr_y+A)<200){
				ccr_y += A;
			}
		}else{
			if((ccr_y-A)>0){
				ccr_y -= A;
			}
		}
		csr_t = 1;
	}else if(csr_t==1){
		char A = inportb(PS2_DATA);
		if((A & 0b00000001)>0){
			printstring("_LEFT");
		}
		if((A & 0b00000010)>0){
			printstring("_RIGHT");
		}
		if((A & 0b00000100)>0){
			printstring("_MIDDLE");
			ccr_x = 50;
			ccr_y = 50;
			csr_y = 12;
			csr_x = 40;
		}
		if((A & 0b00001000)>0){
			ccr_a = 1;
		}else{
			ccr_a = 0;
		}
		if((A & 0b00010000)>0){
			ccr_b = 1;
		}else{
			ccr_b = 0;
		}
		csr_t = 2;
	}else if(csr_t==2){
		char A = inportb(PS2_DATA);
		if(ccr_a){
			if((ccr_x+A)<1600){
				ccr_x += A;
			}
		}else{
			if((ccr_x-A)>0){
				ccr_x -= A;
			}
		}
		csr_t = 0;
	}
	
	// hardware cursor updaten
	unsigned temp;
	csr_x = ccr_x/20;
	csr_y = ccr_y/20;
	if(csr_x>75){
		csr_x = 70;
	}
	if(csr_y>24){
		csr_y = 20;
	}
    	temp = csr_y * 80 + csr_x;
    	outportb(0x3D4, 14);
    	outportb(0x3D5, temp >> 8);
    	outportb(0x3D4, 15);
    	outportb(0x3D5, temp);
	
	// EOI
	outportb(0x20,0x20);
	outportb(0xA0,0x20);
}

unsigned char kbdus[128] ={
    0,  27, '1', '2', '3', '4', '5', '6', '7', '8',	/* 9 */
  '9', '0', '-', '=', '\b',	/* Backspace */
  '\t',			/* Tab */
  'q', 'w', 'e', 'r',	/* 19 */
  't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',	/* Enter key */
    0,			/* 29   - Control */
  'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';',	/* 39 */
 '\'', '`',   0,		/* Left shift */
 '\\', 'z', 'x', 'c', 'v', 'b', 'n',			/* 49 */
  'm', ',', '.', '/',   0,				/* Right shift */
  '*',
    0,	/* Alt */
  ' ',	/* Space bar */
    0,	/* Caps lock */
    0,	/* 59 - F1 key ... > */
    0,   0,   0,   0,   0,   0,   0,   0,
    0,	/* < ... F10 */
    0,	/* 69 - Num lock*/
    0,	/* Scroll Lock */
    0,	/* Home key */
    0,	/* Up Arrow */
    0,	/* Page Up */
  '-',
    0,	/* Left Arrow */
    0,
    0,	/* Right Arrow */
  '+',
    0,	/* 79 - End key*/
    0,	/* Down Arrow */
    0,	/* Page Down */
    0,	/* Insert Key */
    0,	/* Delete Key */
    0,   0,   0,
    0,	/* F11 Key */
    0,	/* F12 Key */
    0,	/* All other keys are undefined */
};	

void irq_keyboard(){
	unsigned char x = inportb(PS2_DATA);
	if((x&0x80)==0){
		putc(kbdus[x]);
	}
	outportb(0x20,0x20);
}

int init_ps2_keyboard(){
	
	// detectie
	if(!writeToFirstPS2Port(0xF5)){goto error;}
	if(!waitforps2ok()){goto error;}
	if(!writeToFirstPS2Port(0xF2)){goto error;}
	if(!waitforps2ok()){goto error;}
	resetTicks();
	while(getPS2ReadyToRead()==0){
		if(getTicks()==PS2_TIMEOUT){
			goto error;
		}
	}
	unsigned char a = inportb(PS2_DATA);
	resetTicks();
	while(getPS2ReadyToRead()==0){
		if(getTicks()==PS2_TIMEOUT){
			goto error;
		}
	}
	unsigned char b = inportb(PS2_DATA);
	printps2devicetype(a);
	printps2devicetype(b);
	
	if(!writeToFirstPS2Port(0xFF)){goto error;}
	resetTicks();
	while(inportb(PS2_DATA)!=0xAA){
		if(getTicks()==PS2_TIMEOUT){
			goto error;
		}
	}
	if(!writeToFirstPS2Port(0xF6)){goto error;}
	if(!waitforps2ok()){goto error;}
	if(!writeToFirstPS2Port(0xF4)){goto error;}
	if(!waitforps2ok()){goto error;}
	
    	setNormalInt(1,(unsigned long)keyboardirq);
    	return 1;
    	
    	error:
    	return 0;
}

int init_ps2_mouse(){
	
	// detectie
	if(!writeToSecondPS2Port(0xFF)){goto error;}
	resetTicks();
	while(inportb(PS2_DATA)!=0xAA){
		if(getTicks()==PS2_TIMEOUT){
			goto error;
		}
	}
	if(!writeToSecondPS2Port(0xF5)){goto error;}
	if(!waitforps2ok()){goto error;}
	if(!writeToSecondPS2Port(0xF2)){goto error;}
	if(!waitforps2ok()){goto error;}
	resetTicks();
	while(getPS2ReadyToRead()==0){
		if(getTicks()==PS2_TIMEOUT){
			goto error;
		}
	}
	unsigned char c = inportb(PS2_DATA);
	resetTicks();
	while(getPS2ReadyToRead()==0){
		if(getTicks()==PS2_TIMEOUT){
			goto error;
		}
	}
	unsigned char d = inportb(PS2_DATA);
	printps2devicetype(c);
	printps2devicetype(d);
	
	if(!writeToSecondPS2Port(0xFF)){goto error;}
	resetTicks();
	while(inportb(PS2_DATA)!=0xAA){
		if(getTicks()==PS2_TIMEOUT){
			goto error;
		}
	}
	if(!writeToSecondPS2Port(0xF6)){goto error;}
	if(!waitforps2ok()){goto error;}
	if(!writeToSecondPS2Port(0xF4)){goto error;}
	if(!waitforps2ok()){goto error;}
	
    	setNormalInt(12,(unsigned long)mouseirq);
    	return 1;
    	
    	error:
    	return 0;
}

void init_ps2(){
	char ps2status = getPS2StatusRegisterText();
	if((ps2status & 0b00000001)>0){
		printstring("PS2: read buffer full\n");
	}
	if((ps2status & 0b00000010)>0){
		printstring("PS2: write buffer full\n");
	}
	if((ps2status & 0b00000100)>0){
		printstring("PS2: passed selftest\n");
	}
	if((ps2status & 0b00001000)>0){
		printstring("PS2: data for controller\n");
	}else{
		printstring("PS2: data for device\n");
	}
	while(getPS2ReadyToWrite()!=0){}
	outportb(PS2_COMMAND,0x20);
	while(getPS2ReadyToRead()==0){
		if(getTicks()>=PS2_TIMEOUT){
			printstring("__TIMEOUT__\n");
			break;
		}
	}
	char ps2enable = inportb(PS2_DATA);
	if((ps2enable & 0b00000001)>0){
		printstring("PS2: port1 interrupt enabled\n");
	}
	if((ps2enable & 0b00000010)>0){
		printstring("PS2: port2 interrupt enabled\n");
	}
	if((ps2enable & 0b00000100)>0){
		printstring("PS2: passed POST\n");
	}
	if((ps2enable & 0b00010000)>0){
		printstring("PS2: port1 clock enabled\n");
	}
	if((ps2enable & 0b00100000)>0){
		printstring("PS2: port2 clock enabled\n");
	}
	if((ps2enable & 0b01000000)>0){
		printstring("PS2: porttranslation enabled\n");
	}
	//while(getPS2ReadyToWrite()!=0){}
	//outportb(PS2_COMMAND,0xAE);
	//while(getPS2ReadyToRead()==0){
	//	if(getTicks()>=10){
	//		printstring("__TIMEOUT__\n");
	//		break;
	//	}
	//}
	//while(getPS2ReadyToWrite()!=0){}
	//outportb(PS2_COMMAND,0xA8);
	//while(getPS2ReadyToRead()==0){
	//	if(getTicks()>=10){
	//		printstring("__TIMEOUT__\n");
	//		break;
	//	}
	//}
	if(init_ps2_keyboard()){
		printstring("PS2: keyboard enabled!\n");
	}else{
		printstring("PS2: keyboard disabled!\n");
	}
	if(init_ps2_mouse()){
		printstring("PS2: mouse enabled!\n");
	}else{
		printstring("PS2: mouse disabled!\n");
	}
	
    	
}

//
// TIMER
//
//

extern void timerirq();

int clock = 0;
volatile int ticks = 0;

int getTicks(){
	return ticks;
}

void resetTicks(){
	ticks = 0;
}

void irq_timer(){
	clock++;
	outportb(0x20,0x20);
	if(clock % 18 == 0){
		ticks++;
		if(videomemory[0]=='-'){
			videomemory[0]='\\';
		}else if(videomemory[0]=='\\'){
			videomemory[0]='|';
		}else if(videomemory[0]=='|'){
			videomemory[0]='/';
		}else if(videomemory[0]=='/'){
			videomemory[0]='-';
		}else{
			videomemory[0]='-';
		}
	}
}

void init_timer(){
	int divisor = 1193180 / 100;       /* Calculate our divisor */
    	outportb(0x43, 0x36);             /* Set our command byte 0x36 */
    	outportb(0x40, divisor & 0xFF);   /* Set low byte of divisor */
    	outportb(0x40, divisor >> 8);     /* Set high byte of divisor */
    	setNormalInt(0,(unsigned long)timerirq);
}

//
// STRING
//
//


int vidpnt = 0;
int curx = 0;
int cury = 0;

void init_video(){
	// set cursor shape
	outportb(0x3D4, 0x0A);
	outportb(0x3D5, (inportb(0x3D5) & 0xC0) | 0);
	outportb(0x3D4, 0x0B);
	outportb(0x3D5, (inportb(0x3D5) & 0xE0) | 15);
	// set cursor location
	vidpnt = 0;
	curx = 0;
	cury = 0;
}

void printstring(char* message){
	int a = 0;
	char b = 0;
	while((b=message[a++])!=0x00){
		putc(b);
	}
}

void putc(char a){
	if(a!='\n'){
		vidpnt = (curx*2)+(160*cury);
		videomemory[vidpnt++] = a;
		videomemory[vidpnt++] = 0x07;
		curx++;
	}
	if(curx==80||a=='\n'){
		curx = 0;
		cury++;
	}
	if(cury==25){
		cury = 24;	
		curx = 0;
		for(int i = 0 ; i < 24 ; i++){
			int v1dpnt = (160*(0+i));
			int v2dpnt = (160*(1+i));
			for(int z = 0 ; z < 160 ; z++){
				videomemory[v1dpnt+z] = videomemory[v2dpnt+z];
				videomemory[v2dpnt+z] = 0x00;
			}
		}
	}
}


void printf(char* format,...) 
{ 
	char *traverse; 
	unsigned int i; 
	signed int t;
	char *s; 
	
	//Module 1: Initializing Myprintf's arguments 
	va_list arg; 
	va_start(arg, format); 
	
	for(traverse = format; *traverse != '\0'; traverse++) 
	{ 
		while( *traverse != '%' && *traverse != '\0' ) 
		{ 
			putc(*traverse);
			traverse++; 
		} 
		if(*traverse =='\0'){
		    break; 
		}
		traverse++; 
		
		//Module 2: Fetching and executing arguments
		switch(*traverse) 
		{ 
			case 'c' : i = va_arg(arg,int);		//Fetch char argument
						putc(i);
						break; 
						
			case 'd' : 
						t = va_arg(arg,int); 		//Fetch Decimal/Integer argument
						if(t<0) 
						{ 
							t = -t;
							putc('-'); 
						} 
						printstring(convert(t,10));
						break; 
						
			case 'o': i = va_arg(arg,unsigned int); //Fetch Octal representation
						printstring(convert(i,8));
						break; 
						
			case 's': s = va_arg(arg,char *); 		//Fetch string
						printstring(s); 
						break; 
						
			case 'x': i = va_arg(arg,unsigned int); //Fetch Hexadecimal representation
						printstring(convert(i,16));
						break; 
				
		}	
	} 
	
	//Module 3: Closing argument list to necessary clean-up
	va_end(arg); 
} 
 
char *convert(unsigned int num, int base) 
{ 
	static char Representation[]= "0123456789ABCDEF";
	static char buffer[50]; 
	char *ptr; 
	
	ptr = &buffer[49]; 
	*ptr = '\0'; 
	
	do 
	{ 
		*--ptr = Representation[num%base]; 
		num /= base; 
	}while(num != 0); 
	
	return(ptr); 
}

// FROM: https://wiki.osdev.org/Printing_To_Screen
char * itoa( int value, char * str, int base )
{
    char * rc;
    char * ptr;
    char * low;
    // Check for supported base.
    if ( base < 2 || base > 36 )
    {
        *str = '\0';
        return str;
    }
    rc = ptr = str;
    // Set '-' for negative decimals.
    if ( value < 0 && base == 10 )
    {
        *ptr++ = '-';
    }
    // Remember where the numbers start.
    low = ptr;
    // The actual conversion.
    do
    {
        // Modulo is negative for negative value. This trick makes abs() unnecessary.
        *ptr++ = "zyxwvutsrqponmlkjihgfedcba9876543210123456789abcdefghijklmnopqrstuvwxyz"[35 + value % base];
        value /= base;
    } while ( value );
    // Terminating the string.
    *ptr-- = '\0';
    // Invert the numbers.
    while ( low < ptr )
    {
        char tmp = *low;
        *low++ = *ptr;
        *ptr-- = tmp;
    }
    return rc;
}

void hexdump(unsigned long a){
	char msg[10];
	itoa(a,msg,16);
	printstring(msg);
}

//
// Interrupt Descriptor Table
//
//

/* Defines an IDT entry */
struct idt_entry
{
    unsigned short base_lo;
    unsigned short sel;        /* Our kernel segment goes here! */
    unsigned char always0;     /* This will ALWAYS be set to 0! */
    unsigned char flags;       /* Set using the above table! */
    unsigned short base_hi;
} __attribute__((packed));

struct idt_ptr
{
    unsigned short limit;
    unsigned int base;
} __attribute__((packed));

struct idt_entry idt[256];
struct idt_ptr idtp;

/* This exists in 'start.asm', and is used to load our IDT */
extern void idt_load();

void idt_set_gate(unsigned char num, unsigned long base, unsigned short sel, unsigned char flags){
    /* The interrupt routine's base address */
    idt[num].base_lo = (base & 0xFFFF);
    idt[num].base_hi = (base >> 16) & 0xFFFF;

    /* The segment or 'selector' that this IDT entry will use
    *  is set here, along with any access flags */
    idt[num].sel = sel;
    idt[num].always0 = 0;
    idt[num].flags = flags;
}

void setErrorInt(unsigned char num,unsigned long base){
	idt_set_gate(num, base, 0x08, 0x8E);
}

void setNormalInt(unsigned char num,unsigned long base){
	idt_set_gate(32+num, base, 0x08, 0x8E);
}


extern void isr_common_stub();
extern void irq_common_stub();

void fault_handler(){
	printstring(" -= KERNEL PANIC =- ");
	asm volatile("cli");
	asm volatile("hlt");
}


void irq_handler(){
	outportb(0x20, 0x20);
	
}

/* Installs the IDT */
void init_idt(){
    	/* Sets the special IDT pointer up, just like in 'gdt.c' */
    	idtp.limit = (sizeof (struct idt_entry) * 256) - 1;
    	idtp.base = (unsigned long)&idt;

	outportb(0x20, 0x11);
	outportb(0xA0, 0x11);
	outportb(0x21, 0x20);
	outportb(0xA1, 0x28);
	outportb(0x21, 0x04);
	outportb(0xA1, 0x02);
	outportb(0x21, 0x01);
	outportb(0xA1, 0x01);
	outportb(0x21, 0x0);
	outportb(0xA1, 0x0);

    	/* Add any new ISRs to the IDT here using idt_set_gate */
	for(int i = 0 ; i < 32 ; i++){
		idt_set_gate(i, (unsigned)isr_common_stub, 0x08, 0x8E);
	}
    	/* Add any new ISRs to the IDT here using idt_set_gate */
	for(int i = 0 ; i < 32 ; i++){
		idt_set_gate(32+i, (unsigned)irq_common_stub, 0x08, 0x8E);
	}
    	/* Points the processor's internal register to the new IDT */
    	idt_load();
    	asm("sti");
}

//
// Global Description Table
//
//

struct gdt_entry{
    unsigned short limit_low;
    unsigned short base_low;
    unsigned char base_middle;
    unsigned char access;
    unsigned char granularity;
    unsigned char base_high;
} __attribute__((packed));

struct gdt_ptr{
    unsigned short limit;
    unsigned int base;
} __attribute__((packed));

struct gdt_entry gdt[3];
struct gdt_ptr gp;

extern void gdt_flush();

void gdt_set_gate(int num, unsigned long base, unsigned long limit, unsigned char access, unsigned char gran){
    /* Setup the descriptor base address */
    gdt[num].base_low = (base & 0xFFFF);
    gdt[num].base_middle = (base >> 16) & 0xFF;
    gdt[num].base_high = (base >> 24) & 0xFF;

    /* Setup the descriptor limits */
    gdt[num].limit_low = (limit & 0xFFFF);
    gdt[num].granularity = ((limit >> 16) & 0x0F);

    /* Finally, set up the granularity and access flags */
    gdt[num].granularity |= (gran & 0xF0);
    gdt[num].access = access;
}

void init_gdt(){
    gp.limit = (sizeof(struct gdt_entry) * 3) - 1;
    gp.base = (unsigned long)&gdt;
    gdt_set_gate(0, 0, 0, 0, 0);
    gdt_set_gate(1, 0, 0xFFFFFFFF, 0x9A, 0xCF);
    gdt_set_gate(2, 0, 0xFFFFFFFF, 0x92, 0xCF);
    gdt_flush();
}

//
// CORE
//
//

unsigned long inportl (unsigned short _port){
    unsigned long rv;
    __asm__ __volatile__ ("inl %1, %0" : "=a" (rv) : "dN" (_port));
    return rv;
}

void outportl (unsigned short _port, unsigned long _data){
    __asm__ __volatile__ ("outl %1, %0" : : "dN" (_port), "a" (_data));
}

unsigned short inportw (unsigned short _port){
    unsigned short rv;
    __asm__ __volatile__ ("inw %1, %0" : "=a" (rv) : "dN" (_port));
    return rv;
}

void outportw (unsigned short _port, unsigned short _data){
    __asm__ __volatile__ ("outw %1, %0" : : "dN" (_port), "a" (_data));
}

unsigned char inportb (unsigned short _port){
    unsigned char rv;
    __asm__ __volatile__ ("inb %1, %0" : "=a" (rv) : "dN" (_port));
    return rv;
}

void outportb (unsigned short _port, unsigned char _data){
    __asm__ __volatile__ ("outb %1, %0" : : "dN" (_port), "a" (_data));
}
