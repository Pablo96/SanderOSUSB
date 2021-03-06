#include "../kernel.h"

#define USB_STORAGE_ENABLE_ENQ 1
#define USB_STORAGE_ENABLE_CAP 0
#define USB_STORAGE_ENABLE_SEC 1
#define USB_STORAGE_SECTOR_SIZE 512
#define USB_STORAGE_CSW_SIGN 0x53425355

unsigned char usb_stick_get_max_lun(USB_DEVICE *device){
	EhciCMD* commando = (EhciCMD*) malloc_align(sizeof(EhciCMD),0x1FF);
	commando->bRequest = 0xFE; // get_max_lun
    commando->bRequestType |= 0x80; // dir=IN
	commando->bRequestType |= (0x01 << 5); // TYPE= class
	commando->bRequestType |= 1; // TO= interface
    commando->wIndex = 0; // windex=0
    commando->wLength = 1; // getlength=8
    commando->wValue = 0; // get config info
	unsigned char *res = usb_send_and_recieve_control(device,commando,malloc(1));
	if((unsigned long)res==EHCI_ERROR){
		return (unsigned char)(EHCI_ERROR&0xFF);
	}
	return res[0];
}

struct cdbres_inquiry {
    unsigned char pdt;
    unsigned char removable;
    unsigned char reserved_02[2];
    unsigned char additional;
    unsigned char reserved_05[3];
    char vendor[8];
    char product[16];
    char rev[4];
} __attribute__ ((packed));

struct cbw_t {
	unsigned long sig;
	unsigned long tag;
	unsigned long xfer_len;
	unsigned char flags;
	unsigned char lun;
	unsigned char wcb_len;
	unsigned char cmd[16];
}  __attribute__ ((packed));

typedef struct __attribute__ ((packed)) {
	unsigned long signature;
	unsigned long tag;
	unsigned long dataResidue;
	unsigned char status;
}CommandStatusWrapper;

typedef struct{
	unsigned char lun;
	USB_DEVICE *master;
} EHCI_USBSTICK;

struct rdcap_10_response_t {
	unsigned long max_lba;
	unsigned long blk_size;
} __attribute__ ((packed));

unsigned char* usb_stick_send_and_recieve_scsi_command(USB_DEVICE *device,unsigned char* out,unsigned long expectedIN,unsigned long expectedOut){
	unsigned long lstatus = usb_send_bulk(device,expectedOut,out);
    if(lstatus==EHCI_ERROR){
        return (unsigned char*)EHCI_ERROR;
    }

	unsigned char* buffer = malloc(expectedIN);
	unsigned long t1 = usb_recieve_bulk(device,expectedIN,buffer);
	if((unsigned long)t1==EHCI_ERROR){
		return (unsigned char *)EHCI_ERROR;
	}

	CommandStatusWrapper* csw = (CommandStatusWrapper*)0;
	csw = (CommandStatusWrapper*)buffer;
	if(csw->signature!=USB_STORAGE_CSW_SIGN){
		unsigned char* cuv = malloc(13);
		unsigned long t2 = usb_recieve_bulk(device,13,cuv);
		if((unsigned long)t2==EHCI_ERROR){
			return (unsigned char *)EHCI_ERROR;
		}
		csw = (CommandStatusWrapper*) cuv;
		printf("[SMSD] Status at end\n");
		if(csw->signature!=USB_STORAGE_CSW_SIGN){
			printf("[SMSD] Command Status Wrapper has a invalid signature\n");
			return (unsigned char *)EHCI_ERROR;
		}
	}
	printf("[SMSD] Status=%x \n",csw->status);
	if(csw->status){
		return (unsigned char *)EHCI_ERROR;
	}
	if(csw->dataResidue){
		printf("[SMSD] Data residu %x \n",csw->dataResidue);
		printf("[SMSD] Asking for a re-read\n");
		buffer = ehci_recieve_bulk(device,csw->dataResidue,malloc(csw->dataResidue));
		if((unsigned long)buffer==EHCI_ERROR){printf("D");
			return (unsigned char *)EHCI_ERROR;
		}
	}
	return buffer;
}

unsigned char* usb_stick_get_inquiry(USB_DEVICE *device){
	unsigned char bufoutsize = 31;
	unsigned char bufinsize = sizeof(struct cdbres_inquiry);
	struct cbw_t* bufout = (struct cbw_t*)malloc(bufoutsize);
	// 55 53 42 43 e7 3 0 0 24 0 0 0 80 0 c 12 0 0 0 24 0 0 0 0 0 0 0 0 0 0 0
	bufout->lun = 0;
	bufout->tag = 1;
	bufout->sig = 0x43425355;
	bufout->wcb_len = 6; // 0xA -> 12
	bufout->flags = 0x80;
	bufout->xfer_len = bufinsize; // 0x8 -> 36
	bufout->cmd[0] = 0x12;// type is 0x12
	bufout->cmd[1] = 0;
	bufout->cmd[2] = 0;
	bufout->cmd[3] = 0;
	bufout->cmd[4] = bufinsize;
	bufout->cmd[5] = 0;
	unsigned char* bufin = usb_stick_send_and_recieve_scsi_command(device,(unsigned char*)bufout,bufinsize,bufoutsize);
	return bufin;
}

unsigned char* usb_stick_get_capacity(USB_DEVICE *device){
	unsigned char bufoutsize = 31;
	unsigned char bufinsize = 36;
	struct cbw_t* bufout = (struct cbw_t*)malloc(bufoutsize);
	bufout->lun = 0;
	bufout->tag = 1;
	bufout->sig = 0x43425355;
	bufout->wcb_len = 6; // 0xA -> 12
	bufout->flags = 0x80;
	bufout->xfer_len = 36; // 0x8 -> 36
	bufout->cmd[0] = 0x25;// type is 0x12
	bufout->cmd[1] = 0;
	bufout->cmd[2] = 0;
	bufout->cmd[3] = 0;
	bufout->cmd[4] = bufinsize;
	bufout->cmd[5] = 0;
	unsigned char* bufin = usb_stick_send_and_recieve_scsi_command(device,(unsigned char*)bufout,bufinsize,bufoutsize);
	return bufin;
}

unsigned char* usb_stick_read_sector(USB_DEVICE *device,unsigned long lba){
	unsigned long bufoutsize = 31;
	unsigned long bufinsize = USB_STORAGE_SECTOR_SIZE; 
	struct cbw_t* bufout = (struct cbw_t*)malloc(bufoutsize);
	bufout->lun = 0;
	bufout->tag = 1;
	bufout->sig = 0x43425355;
	bufout->wcb_len = 6; // 0xA -> 12 6
	bufout->flags = 0x80;
	bufout->xfer_len = bufinsize;
	bufout->cmd[0] = 0x08;// type is 0x12
	bufout->cmd[1] = (lba >> 16) & 0xFF;
	bufout->cmd[2] = (lba >> 8) & 0xFF;
	bufout->cmd[3] = (lba) & 0xFF;
	bufout->cmd[4] = 1;
	unsigned char* bufin = usb_stick_send_and_recieve_scsi_command(device,(unsigned char*)bufout,bufinsize,bufoutsize);
	return bufin;
}

void usb_stick_read_raw_sector(Device *dxv,unsigned long LBA,unsigned char count,unsigned short *l0cation){
	EHCI_USBSTICK *stick = (EHCI_USBSTICK*) ((unsigned long)dxv->arg1);
	unsigned char *location = (unsigned char*)l0cation;
	for(int i = 0 ; i < count ; i++){
		unsigned char* tak = usb_stick_read_sector(stick->master,dxv->arg2+LBA);
		if((unsigned long)tak!=EHCI_ERROR){
			for(int z = 0 ; z < 512 ; z++){
				location[(i*512)+z] = tak[z];
			}
		}
	}
}

//
// On emulator, the value is:
// subclass= 0x06
// protocol= 0x50
void usb_stick_init(USB_DEVICE *device){//unsigned char addr,unsigned char subclass,unsigned char protocol){
	printf("[SMSD] Reached USB Mass Storage Device endpoint. subclass=%x protocol=%x \n",device->subclass,device->protocol);
	if(!(device->subclass==0x02||device->subclass==0x05||device->subclass==0x06)){
		printf("[SMSD] Unsupported subclass version. Requested 2/5/6, found 0x%x \n",device->subclass);
		return;
	}
	if(device->protocol!=0x50){
		printf("[SMSD] Unsupported protocol version. Requested 0x50, found 0x%x \n",device->protocol);
		return;
	}

	// get maxlun
	unsigned char maxlun = usb_stick_get_max_lun(device);
	if(maxlun==(EHCI_ERROR&0xFF)){
		printf("[SMSD] An error occured while getting max lun \n");	
		return;
	}

	printf("[SMSD] Maxlun is %x \n",maxlun);

	// inquiry
	if(USB_STORAGE_ENABLE_ENQ){
		unsigned char* inquiry_raw = usb_stick_get_inquiry(device);
		struct cdbres_inquiry* inc = (struct cdbres_inquiry*)inquiry_raw;
		if((unsigned long)inquiry_raw==(unsigned long)EHCI_ERROR){
			printf("[SMSD] An error occured while getting inquiry info \n");
			return;
		}
		printf("[SMSD] vendor=%c%c%c%c%c%c%c%c  product=%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c  rev=%c%c%c \n",
			inc->vendor[0],inc->vendor[1],inc->vendor[2],inc->vendor[3],inc->vendor[4],inc->vendor[5],inc->vendor[6],inc->vendor[7],
			inc->product[0],inc->product[1],inc->product[2],inc->product[3],inc->product[4],inc->product[5],inc->product[6],inc->product[7],inc->product[8],inc->product[9],inc->product[10],inc->product[11],inc->product[12],inc->product[13],inc->product[14],inc->product[15],
			inc->rev[0],inc->rev[1],inc->rev[2]
		);
	}

	// get capacity
	if(USB_STORAGE_ENABLE_CAP){
		unsigned char* capacity_raw = usb_stick_get_capacity(device);
		if((unsigned long)capacity_raw==(unsigned long)EHCI_ERROR){
			printf("[SMSD] An error occured while getting capacity info \n");
			return;
		}
		struct rdcap_10_response_t* capacity = (struct rdcap_10_response_t*)capacity_raw;
		unsigned long maxlba = capacity->max_lba;
		unsigned long blocksize = capacity->blk_size;
		printf("[SMSD] Capacity information: maxlba=%x blocksize=%x \n",maxlba,blocksize);
	}
	
	if(USB_STORAGE_ENABLE_SEC){
		unsigned char* t = usb_stick_read_sector(device,0);
		if((unsigned long)t==(unsigned long)EHCI_ERROR){
			printf("[SMSD] An error occured while reading a sector \n");
			return;
		}
		for(int i = 0 ; i < 512 ; i++){printf("%x ",t[i]);}
		printf("[SMSD] Reading testsector succeed\n");
	}

	// setup bootdevice
	EHCI_USBSTICK *usbdevice = (EHCI_USBSTICK*) malloc(sizeof(EHCI_USBSTICK));
	usbdevice->master = device;

	Device *regdev = (Device*) malloc(sizeof(Device));
	regdev->readRawSector = (unsigned long) usb_stick_read_raw_sector;
	regdev->arg1 = (unsigned long)usbdevice;
	regdev->arg5 = 512;

	detectFilesystemsOnMBR(regdev);
	for(;;);
}