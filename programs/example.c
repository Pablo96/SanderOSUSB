#include "../SanderOSUSB.h"

const char *messagestring = "This is an example program\n";
unsigned char dinges[1];

void main(){
	printf(messagestring);
	while(1){
		read(STDIO,(unsigned char*)&dinges,1);
		if(dinges[0]!=0x00){
			break;
		}
	}
	printf("\nacknowledge!");
	for(;;);
}
