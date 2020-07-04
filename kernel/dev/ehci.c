#include "../kernel.h"
// cd ../kernel
// bash build.sh 
// ../../qemu/x86_64-softmmu/qemu-system-x86_64 --cdrom ../cdrom.iso  -trace enable=usb*  -device usb-ehci,id=ehci -drive if=none,id=usbstick,file=../../myos.iso -device usb-storage,bus=ehci.0,drive=usbstick
// much thanks to https://github.com/dgibson/SLOF and https://github.com/pdoane/osdev and https://github.com/coreboot/seabios

#define EHCI_PERIODIC_FRAME_SIZE    1024
#define RT_DEV_TO_HOST              0x80
#define RT_STANDARD                 0x00
#define RT_DEV                      0x00
#define REQ_GET_DESC                0x06
#define USB_DESC_DEVICE             0x01
#define USB_PACKET_OUT              0x00
#define USB_PACKET_IN               0x01
#define USB_PACKET_SETUP            0x02
#define TD_TOK_D_SHIFT              31
#define TD_TOK_LEN_SHIFT            16
#define TD_TOK_CERR_SHIFT           10
#define TD_TOK_PID_SHIFT            8
#define TD_TOK_ACTIVE               (1 << 7)
#define USB_HIGH_SPEED              0x02
#define QH_CH_MPL_SHIFT             16
#define QH_CH_DTC                   0x4000
#define QH_CH_EPS_SHIFT             12
#define QH_CH_ENDP_SHIFT            8
#define QH_CAP_MULT_SHIFT           30
#define QH_CAP_INT_SCHED_SHIFT      0
#define QH_CH_NAK_RL_SHIFT          28
#define TD_TOK_HALTED               (1 << 6)
#define PTR_TERMINATE               (1 << 0)

typedef struct {
    volatile unsigned long nextlink;
    volatile unsigned long altlink;
    volatile unsigned long token;
    volatile unsigned long buffer[5];
    volatile unsigned long extbuffer[5];
}EhciTD;

typedef struct {
    volatile unsigned long horizontal_link_pointer;
    volatile unsigned long characteristics;
    volatile unsigned long capabilities;
    volatile unsigned long curlink;

    volatile unsigned long nextlink;
    volatile unsigned long altlink;
    volatile unsigned long token;
    volatile unsigned long buffer[5];
    volatile unsigned long extbuffer[5];
    
}EhciQH;

unsigned long periodic_list[EHCI_PERIODIC_FRAME_SIZE] __attribute__ ((aligned (0x1000)));
unsigned long portbaseaddress;
unsigned long usbcmd_addr;
unsigned long usbsts_addr;
unsigned long usbint_addr;
unsigned long usbfin_addr;
unsigned long usbctr_addr;
unsigned long usbper_addr;
unsigned long usbasc_addr;
unsigned long usbcon_addr;
unsigned char available_ports;

extern void ehciirq();
void irq_ehci(){
	outportb(0xA0,0x20);
	outportb(0x20,0x20);
    
    //
    // what is happening?
    unsigned long status = ((unsigned long*)usbsts_addr)[0];
    if(status&0b000001){
        printf("[EHCI] Interrupt: transaction completed\n");
    }else if(status&0b000010){
        printf("[EHCI] Interrupt: error interrupt\n");
    }else if(status&0b000100){
        printf("[EHCI] Interrupt: portchange\n");
    }else if(status&0b001000){
        printf("[EHCI] Interrupt: frame list rollover\n");
    }else if(status&0b010000){
        printf("[EHCI] Interrupt: host system error\n");
    }else if(status&0b100000){
        printf("[EHCI] Interrupt: interrupt on advance\n");
    }else{
        printf("[EHCI] Interrupt: unknown\n");
    }
    ((unsigned long*)usbsts_addr)[0] = status;
}

typedef struct  {
    unsigned char bRequestType;
    unsigned char bRequest;
    unsigned short wValue;
    unsigned short wIndex;
    unsigned short wLength;
} EhciCMD;

unsigned char ehci_wait_for_completion(EhciTD *status){
    unsigned char lstatus = 1;
    while(1){
        unsigned long tstatus = status->token;
        if(tstatus & (1 << 6)){
            // not anymore active and failed miserably
            printf("EHCI FAIL A\n");
            lstatus = 0;
            break;
        }
        if(tstatus & (1 << 5)){
            // not anymore active and failed miserably
            printf("EHCI FAIL B\n");
            lstatus = 0;
            break;
        }
        if(tstatus & (1 << 3)){
            // not anymore active and failed miserably
            printf("EHCI FAIL C\n");
            lstatus = 0;
            break;
        }
        if(!(tstatus & (1 << 7))){
            // not anymore active and succesfull ended
            lstatus = 1;
            break;
        }
    }
    return lstatus;
}

unsigned char ehci_set_device_address(unsigned char addr){

    //
    // ok, to set a address to a device, we need 4 components:
    // 1) QH: a queuehead to connect to us
    // 2) QTD: a command descriptor
    // 3) QTD: a status descriptor
    // 4) CMD: a command

    // first make the command
    EhciCMD *cmd = (EhciCMD*) malloc_align(sizeof(EhciCMD),0xFFF);
    cmd->bRequest = 0x05; // USB-REQUEST type SET-ADDR is 0x05
    cmd->bRequestType |= 0; // direction is out
    cmd->bRequestType |= 0 << 5; // type is standard
    cmd->bRequestType |= 0; // recieve
    cmd->wIndex = 0; // ok
    cmd->wLength = 0; // ok
    cmd->wValue = addr;

    EhciTD *command = (EhciTD*) malloc_align(sizeof(EhciTD),0xFF);
    EhciTD *status = (EhciTD*) malloc_align(sizeof(EhciTD),0xFF);
    EhciQH *head1 = (EhciQH*) malloc_align(sizeof(EhciQH),0xFF);
    EhciQH *head2 = (EhciQH*) malloc_align(sizeof(EhciQH),0xFF);

    command->nextlink = (unsigned long)status;
    command->altlink = 1;
    command->token |= (8 << 16); // setup size
    command->token |= (1 << 7); // actief
    command->token |= (0x2 << 8); // type is setup
    command->token |= (0x3 << 10); // maxerror
    command->buffer[0] = (unsigned long)cmd;

    status->nextlink = 1;
    status->altlink = 1;
    status->token |= (1 << 8); // PID instellen
    status->token |= (1 << 31); // toggle
    status->token |= (1 << 7); // actief
    status->token |= (0x3 << 10); // maxerror

    //
    // Tweede commando


    head2->altlink = 1;
    head2->nextlink = (unsigned long)command; // qdts2
    head2->horizontal_link_pointer = ((unsigned long)head1) | 2;
    head2->curlink = 0; // qdts1
    head2->characteristics |= 1 << 14; // dtc
    head2->characteristics |= 64 << 16; // mplen
    head2->characteristics |= 2 << 12; // eps
    head2->capabilities = 0x40000000;

    //
    // Eerste commando
    head1->altlink = 1;
    head1->nextlink = 1;
    head1->horizontal_link_pointer = ((unsigned long)head2) | 2;
    head1->curlink = 0;
    head1->characteristics = 1 << 15; // T
    head1->token = 0x40;

    ((unsigned long*) usbasc_addr)[0] = ((unsigned long)head1) ;
    ((unsigned long*)usbcmd_addr)[0] |= 0b100000;

    unsigned char lstatus = ehci_wait_for_completion(status);
    
    ((unsigned long*)usbcmd_addr)[0] &= ~0b100000;
    ((unsigned long*) usbasc_addr)[0] = 1 ;
    return lstatus;
}

unsigned char* ehci_get_device_descriptor(unsigned char addr,unsigned char size){
    EhciQH* qh = (EhciQH*) malloc_align(sizeof(EhciQH),0x1FF);
    EhciQH* qh2 = (EhciQH*) malloc_align(sizeof(EhciQH),0x1FF);
    EhciQH* qh3 = (EhciQH*) malloc_align(sizeof(EhciQH),0x1FF);
    EhciTD* td = (EhciTD*) malloc_align(sizeof(EhciTD),0x1FF);
    EhciTD* trans = (EhciTD*) malloc_align(sizeof(EhciTD),0x1FF);
    EhciTD* status = (EhciTD*) malloc_align(sizeof(EhciTD),0x1FF);
    EhciCMD* commando = (EhciCMD*) malloc_align(sizeof(EhciCMD),0x1FF);

    unsigned char *buffer = malloc(size);

    commando->bRequest = 0x06; // get_descriptor
    commando->bRequestType |= 0x80; // dir=IN
    commando->wIndex = 0; // windex=0
    commando->wLength = size; // getlength=8
    commando->wValue = 1 << 8; // 

    //
    // Derde commando
    td->token |= 2 << 8; // PID=2
    td->token |= 3 << 10; // CERR=3
    td->token |= size << 16; // TBYTE=8
    td->token |= 1 << 7; // ACTIVE=1
    td->nextlink = (unsigned long)trans;
    td->altlink = 1;
    td->buffer[0] = (unsigned long)commando;

    trans->nextlink = (unsigned long)status;
    trans->altlink = 1;
    trans->token |= (size << 16); // verwachte lengte
    trans->token |= (1 << 31); // toggle
    trans->token |= (1 << 7); // actief
    trans->token |= (1 << 8); // IN token
    trans->token |= (0x3 << 10); // maxerror
    trans->buffer[0] = (unsigned long)buffer;

    status->nextlink = 1;
    status->altlink = 1;
    status->token |= (0 << 8); // PID instellen
    status->token |= (1 << 31); // toggle
    status->token |= (1 << 7); // actief
    status->token |= (0x3 << 10); // maxerror


    //
    // Tweede commando
    qh2->altlink = 1;
    qh2->nextlink = (unsigned long)td; // qdts2
    qh2->horizontal_link_pointer = ((unsigned long)qh) | 2;
    qh2->curlink = (unsigned long)qh3; // qdts1
    qh2->characteristics |= 1 << 14; // dtc
    qh2->characteristics |= 64 << 16; // mplen
    qh2->characteristics |= 2 << 12; // eps
    qh2->characteristics |= addr; // device
    qh2->capabilities = 0x40000000;

    //
    // Eerste commando
    qh->altlink = 1;
    qh->nextlink = 1;
    qh->horizontal_link_pointer = ((unsigned long)qh2) | 2;
    qh->curlink = 0;
    qh->characteristics = 1 << 15; // T
    qh->token = 0x40;

    ((unsigned long*) usbasc_addr)[0] = ((unsigned long)qh) ;
    ((unsigned long*)usbcmd_addr)[0] |= 0b100000;

    unsigned char result = ehci_wait_for_completion(status);
    
    ((unsigned long*)usbcmd_addr)[0] &= ~0b100000;
    ((unsigned long*) usbasc_addr)[0] = 1 ;
    if(result==0){
        return 0;
    }

    return buffer;
}

void init_ehci_port(int portnumber){
    unsigned long avail_port_addr = portbaseaddress + 0x44 + (portnumber*4);
    unsigned long portinfo = ((unsigned long*)avail_port_addr)[0];

    // reset the port
    ((unsigned long*)avail_port_addr)[0] |=  0b100000000;
    sleep(20);
    sleep(20);
    sleep(20);
    ((unsigned long*)avail_port_addr)[0] &= ~0b100000000;
    sleep(20);
    portinfo = ((volatile unsigned long*)avail_port_addr)[0];
    printf("[EHCI] Port %x : End of port reset with %x \n",portnumber,portinfo);

    // check if port is enabled
    if((portinfo&0b100)==0){
        printf("[EHCI] Port %x : Port is not enabled but connected\n",portnumber);
        return;
    }

    unsigned char deviceaddress = 1;
    printf("[EHCI] Port %x : Setting device address to %x \n",portnumber,deviceaddress);
    unsigned char deviceaddressok = ehci_set_device_address(deviceaddress);
    if(deviceaddressok==0){
        printf("[EHCI] Port %x : Unable to set device address...\n",portnumber);
        for(;;);
        return;
    }

    printf("[EHCI] Port %x : Getting devicedescriptor...\n",portnumber);
    unsigned char* desc = ehci_get_device_descriptor(deviceaddress,8);
    if(desc==0){
        printf("[EHCI] Port %x : Unable to get devicedescriptor...\n",portnumber);
        for(;;);
        return;
    }
    printf("[EHCI] Port %x : %x %x %x %x %x %x %x %x \n",portnumber,desc[0],desc[1],desc[2],desc[3],desc[4],desc[5],desc[6],desc[7]);
    if(!(desc[0]==0x12&&desc[1]==0x1)){
        printf("[EHCI] Port %x : Invalid magic number at descriptor (%x %x)...\n",portnumber,desc[0],desc[1]);
        for(;;);
        return;
    }
}

void ehci_probe(){
    // checking all ports
    for(int i = 0 ; i < available_ports ; i++){
        unsigned long avail_port_addr = portbaseaddress + 0x44 + (i*4);
        unsigned long portinfo = ((unsigned long*)avail_port_addr)[0];
        printf("[EHCI] Port %x info : %x \n",i,portinfo);
        if(portinfo&3){
            printf("[EHCI] Port %x : connection detected!\n",i);
            init_ehci_port(i);
        }
    }
}

void init_ehci(unsigned long bus,unsigned long slot,unsigned long function){
    printf("[EHCI] Entering EHCI module\n");
    unsigned long baseaddress = getBARaddress(bus,slot,function,0x10);
    printf("[EHCI] The base address of EHCI is %x \n",baseaddress);
    portbaseaddress = baseaddress + ((unsigned char*)baseaddress)[0];
    printf("[EHCI] The address to the first EHCI registers is %x \n",portbaseaddress);
    unsigned long hci_version_addr = baseaddress+0x02;
    unsigned short hci_version = ((unsigned short*)hci_version_addr)[0];
    printf("[EHCI] The versionnumber of the EHCI protocol is %x \n",hci_version);
    if(hci_version!=0x100){
        printf("[EHCI] This driver is not compliant with current EHCIprotocol version\n");
        return;
    }
    unsigned long hcsparams_addr = baseaddress+0x04;
    unsigned long hcsparams = ((unsigned long*)hcsparams_addr)[0];
    printf("[EHCI] HCSParams are %x \n",hcsparams);
    available_ports = hcsparams & 0b1111;
    printf("[EHCI] We have %x ports available\n",available_ports);
    unsigned long hccparams_addr = baseaddress+0x08;
    unsigned long hccparams = ((unsigned long*)hccparams_addr)[0];
    printf("[EHCI] HCCParams are %x \n",hccparams);
    unsigned char bit64cap = hccparams & 1;
    if(bit64cap){
        printf("[EHCI] This controller is 64 bit capable\n");
    }
    unsigned char capabilitypointer = (hccparams & 0b1111111100000000) >> 8;
    if(capabilitypointer){
        unsigned long capabilitypointer_addr =  capabilitypointer;
        printf("[EHCI] We have capabilitypointers at %x \n",capabilitypointer_addr);
        while(1){
            unsigned long cap = getBARaddress(bus,slot,function,capabilitypointer_addr);
            unsigned char cid = cap & 0xFF;
            if(cid==0x01){
                printf("[EHCI] Legacy support available\n");
                if(cap&(1<<16)){
                    printf("[EHCI] BIOS owns controller\n");
                }
            }else{
                printf("[EHCI] Unknown extended cappoint %x : %x \n",cid,cap);
            } 
            unsigned char cxt = (cap & 0xFF00)>>8;
            if(cxt==0){
                break;
            }
            capabilitypointer_addr += cxt;
        }
    }
	unsigned long usbint = getBARaddress(bus,slot,function,0x3C) & 0x000000FF;
    setNormalInt(usbint,(unsigned long)ehciirq);
    printf("[EHCI] Installing interrupts at %x \n",usbint);

    //
    // Now, let the hostcontroller do what we are asking.
    usbcmd_addr = portbaseaddress + 0x00;
    usbsts_addr = portbaseaddress + 0x04;
    usbint_addr = portbaseaddress + 0x08;
    usbfin_addr = portbaseaddress + 0x0C;
    usbctr_addr = portbaseaddress + 0x10;
    usbper_addr = portbaseaddress + 0x14;
    usbasc_addr = portbaseaddress + 0x18;
    usbcon_addr = portbaseaddress + 0x40;

    // stop already running controllers
    unsigned long default_usbcmd_value = ((unsigned long*)usbcmd_addr)[0];
    printf("[EHCI] Default value USBCMD %x \n",default_usbcmd_value);
    if(default_usbcmd_value & 1){
        printf("[EHCI] EHCI is already running. Stopping it.\n");
        ((unsigned long*)usbcmd_addr)[0] &= ~1; // stopping
        while(1){
            if((((volatile unsigned long*)usbcmd_addr)[0]&1)==0){
                break;
            }
        }
        // stop succesfull
    }

    // Reset the controller
    ((unsigned long*)usbcmd_addr)[0] |= 2;
    while(1){
        if((((volatile unsigned long*)usbcmd_addr)[0]&2)==0){
            break;
        }
    }
    printf("[EHCI] Reset of EHCI Host Controller is succeed.\n");

    // clearning periodic_list
    for(int i = 0 ; i < EHCI_PERIODIC_FRAME_SIZE ; i++){
        periodic_list[i] |= 1;
    }

    // set correct segment
    ((unsigned long*)usbctr_addr)[0] = 0;

    // set correct ints
    ((unsigned long*)usbint_addr)[0] |= 0b10111; // enable all interrupts except: frame_list_rollover async_adv

    // set periodic list base
    ((unsigned long*)usbper_addr)[0] = (unsigned long)&periodic_list;

    // set interrupt treshold (usbcmd) : default is 8mf with value 08
    ((unsigned long*)usbcmd_addr)[0] |= (0x40<<16);

    // set frame list size : default is 1024 with value 0
    ((unsigned long*)usbcmd_addr)[0] |= (0<<2);

    // set run bit
    ((unsigned long*)usbcmd_addr)[0] |= 1;

    // set routing
    ((unsigned long*)usbcon_addr)[0] |= 1;

    ehci_probe();

}