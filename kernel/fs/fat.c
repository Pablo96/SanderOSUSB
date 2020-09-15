#include "../kernel.h"

typedef struct fat_extBS_32{
	//extended fat32 stuff
	unsigned int		table_size_32;
	unsigned short		extended_flags;
	unsigned short		fat_version;
	unsigned int		root_cluster;
	unsigned short		fat_info;
	unsigned short		backup_BS_sector;
	unsigned char 		reserved_0[12];
	unsigned char		drive_number;
	unsigned char 		reserved_1;
	unsigned char		boot_signature;
	unsigned int 		volume_id;
	unsigned char		volume_label[11];
	unsigned char		fat_type_label[8];
 
}__attribute__((packed)) fat_extBS_32_t;
 
typedef struct fat_extBS_16{
	//extended fat12 and fat16 stuff
	unsigned char		bios_drive_num;
	unsigned char		reserved1;
	unsigned char		boot_signature;
	unsigned int		volume_id;
	unsigned char		volume_label[11];
	unsigned char		fat_type_label[8];
	//  TODO boot_code (len 448B) missing
}__attribute__((packed)) fat_extBS_16_t;
 
typedef struct fat_BS{
	unsigned char 		bootjmp[3];
	unsigned char 		oem_name[8];
	unsigned short 	    bytes_per_sector;
	unsigned char		sectors_per_cluster;
	unsigned short		reserved_sector_count;
	unsigned char		table_count;
	unsigned short		root_entry_count;
	unsigned short		total_sectors_16;
	unsigned char		media_type;
	unsigned short		table_size_16;
	unsigned short		sectors_per_track;
	unsigned short		head_side_count;
	unsigned int 		hidden_sector_count;
	unsigned int 		total_sectors_32;
 
	//this will be cast to it's specific type once the driver actually knows what type of FAT this is.
	unsigned char		extended_section[54];
 
}__attribute__((packed)) fat_BS_t;

typedef struct DirectoryEntry{
	unsigned char name[11];
	unsigned char attrib;
	unsigned char userattrib;
	unsigned char undelete;
	unsigned short createtime;
	unsigned short createdate;
	unsigned short accessdate;
	unsigned short clusterhigh; // 00 00 
	unsigned short modifiedtime;
	unsigned short modifieddate; 
	unsigned short clusterlow; // 66 13
	unsigned long filesize;
} __attribute__ ((packed)) fat_dir_t;

/* represent a function of this type generally is a referencce to this function:
 * >> atapi_read_raw(Device *dev,unsigned long lba,unsigned char count,unsigned short *location)
 * located in dev/AHCI.c
 **/
typedef void* (*ReadAHCIFunction)(Device *,unsigned long,unsigned char,unsigned short *);
typedef void* (*WriteAHCIFunction)(Device *, unsigned long, unsigned char, unsigned short *);

void fat_write(Device *device, char *path, char *buffer) {
	// Write to disk
	WriteAHCIFunction write_raw = (WriteAHCIFunction)device->writeRawSector;
	
	unsigned char bytes_count = 1;
	unsigned long from_mem_addr = 0x1745;
	write_raw(device, from_mem_addr, bytes_count, (unsigned short*)buffer);
}

void fat_read(Device *device,char* path,char *buffer){
	ReadAHCIFunction readraw = (ReadAHCIFunction)device->readRawSector;
	unsigned short* rxbuffer = (unsigned short*) malloc(512);
	readraw(device,0,1,rxbuffer); 
	fat_BS_t* fat_boot = (fat_BS_t*) rxbuffer;
	fat_extBS_32_t* fat_boot_ext_32 = (fat_extBS_32_t*) fat_boot->extended_section;
	
	unsigned long total_sectors 	= (fat_boot->total_sectors_16 == 0)? fat_boot->total_sectors_32 : fat_boot->total_sectors_16;
	unsigned long fat_size 			= (fat_boot->table_size_16 == 0)? fat_boot_ext_32->table_size_32 : fat_boot->table_size_16;
	unsigned long root_dir_sectors 	= ((fat_boot->root_entry_count * 32) + (fat_boot->bytes_per_sector - 1)) / fat_boot->bytes_per_sector;
	unsigned long first_data_sector = fat_boot->reserved_sector_count + (fat_boot->table_count * fat_size) + root_dir_sectors;
	unsigned long data_sectors 		= total_sectors - (fat_boot->reserved_sector_count + (fat_boot->table_count * fat_size) + root_dir_sectors);
	unsigned long total_clusters 	= data_sectors / fat_boot->sectors_per_cluster;
	
	unsigned long first_sector_of_cluster = 0;
	if(total_clusters < 65525){
		// FAT 16
		first_sector_of_cluster = fat_boot->reserved_sector_count + (fat_boot->table_count * fat_boot->table_size_16);
	} else if (total_clusters < 268435445){
		// FAT 32
		unsigned long root_cluster_32 = fat_boot_ext_32->root_cluster;
		first_sector_of_cluster = ((root_cluster_32 - 2) * fat_boot->sectors_per_cluster) + first_data_sector;
	} else {
		// FAT type not supported
	}
	
	unsigned short* fatbuffer = (unsigned short*) malloc(512);
	readraw(device,first_sector_of_cluster,1,fatbuffer); 
	unsigned long pathoffset = 0;
	unsigned long pathfileof = 0;
	unsigned char filename[11];
	
	// search for path
	while(1){
		// empty the buffer
		for(int i = 0 ; i < 11 ; i++){
			filename[i] = 0x00;
		}
		// fill buffer with new word
		unsigned char erstw = path[pathoffset];
		if(erstw==0x00){
			break;
		}
		
		for(int i = 0 ; i < 11 ; i++){
			unsigned char deze = path[pathoffset++];
			if(deze=='/'){
				break;
			}
			if(deze==0x00){
				pathoffset--;
				break;
			}
			filename[pathfileof++] = deze;
		}
		
		unsigned long offset = 0;
		unsigned long newsect = 0;
		while(1){
			fat_dir_t* currentdir = (fat_dir_t*) (fatbuffer + offset);
			offset += sizeof(fat_dir_t);
			unsigned char first_char = currentdir->name[0];
			if(first_char==0x00){
				break;
			}
			if(first_char==0xE5){
				continue;
			}
			unsigned long sigma = 0;
			unsigned long yotta = 1;
			for(int i = 0 ; i < 11 ; i++){
				if(currentdir->name[i]!=0x00){
					if(currentdir->name[i]!=filename[sigma++]){
						// name has changed
						yotta = 0;
					}
				}
			}
			if(yotta){
				newsect = ((currentdir->clusterhigh << 8) &0xFFFF0000) | (currentdir->clusterlow & 0xFFFF);
				break;
			}
		}
		if(newsect){
			first_sector_of_cluster = ((newsect - 2) * fat_boot->sectors_per_cluster) + first_data_sector;
			readraw(device,first_sector_of_cluster,1,fatbuffer);
		}else{
			printf("CANNOT FIND DIR\n");
			for(;;);
		}
		
	}
	
	readraw(device,first_sector_of_cluster,1,(unsigned short*)buffer);
}


unsigned long fat_target(Device *device,char* path){
	ReadAHCIFunction readraw = (ReadAHCIFunction)device->readRawSector;
	unsigned char selfloor = 1;

	int pathlengte = strlen(path);
	int paths = 1;
	char is_bestand = 0;
	int laatsteint = 0;
	for(int i = 0 ; i < pathlengte ; i++){
		if(path[i]=='/'){
			paths++;
			laatsteint = i+1;
		}
		if(path[i]=='.'){
			isonameloc = laatsteint;
			is_bestand = 1;
		}
	}
	
	int primairesector = 0;
	for(int i = 0 ; i < 10 ; i++){
		readraw(device,0x10+i,1,(unsigned short *)isobuffer);
		if(isobuffer[0]==0x01&&isobuffer[1]=='C'&&isobuffer[2]=='D'&&isobuffer[3]=='0'&&isobuffer[4]=='0'&&isobuffer[5]=='1'){
			primairesector = 0x10+i;
			break;
		}
	}
	
	if(primairesector==0){
		printf("ISO: primairy sector not found!\n");for(;;);
	}
	
	unsigned long dt = charstoint(isobuffer[148],isobuffer[149],isobuffer[150],isobuffer[151]);
	readraw(device,dt,1,(unsigned short *)isobuffer);
	
	unsigned long res = charstoint(isobuffer[2],isobuffer[3],isobuffer[4],isobuffer[5]);
	if(path[0]==0){
		return res;
	}

	char pathchunk[20];
	int pathsel = 0; // pathchunckcount
	int ipath = 0; // pathchunksel
	char deze = 0;
	int boomdiepte = 1;
	memset(pathchunk,20,0);
	for(int i = 0 ; i < (paths-(is_bestand?1:0)) ; i++){
		memset(pathchunk,20,0);
		ipath = 0;
		kopieernogeen:
		deze = path[pathsel++];
		if(!(deze==0x00||deze=='/')){
			pathchunk[ipath++] = deze;
			goto kopieernogeen;
		}
		pathchunk[ipath] = 0x00;
		//printf("Chunk [%s] word nu behandeld \n",pathchunk);

		// door alle directories lopen van actuele boom
		unsigned char entrytextlength = 0;
		unsigned char entrytotallength = 0;
		unsigned char entrytree = 0;
		int entrypointer = 0;
		int edept = 0;
		nogmaals:
		entrytextlength = isobuffer[entrypointer+0];
		entrytotallength = isobuffer[entrypointer+1];
		entrytree = isobuffer[entrypointer+7];
		if(entrytree==boomdiepte&&entrytextlength==ipath){
			char found = 1;
			for(int t = 0 ; t < entrytextlength ; t++){
				if(isobuffer[entrypointer+8+t]!=pathchunk[t]){
					found = 0;
				}
			}
			if(found){
				boomdiepte = edept+1;
				res = charstoint(isobuffer[entrypointer+2],isobuffer[entrypointer+3],isobuffer[entrypointer+4],isobuffer[entrypointer+5]);
				selfloor = boomdiepte;
				if((paths-(is_bestand?1:0))==(i+1)){
					dummy = res;
					return res;
				}
				continue;
			}
		}
		int z = entrytextlength+entrytotallength+8;
		if(z%2!=0){
			z++;
		}
		entrypointer += z;
		edept++;
		goto nogmaals;
	}
	return res;
}


/**
 * Returns true(1) if the given directory (path) exists in the device, false(0) otherwise
 */
char fat_exists(Device *device,char* path){
	static unsigned long dummy = 0;
	ReadAHCIFunction readraw = (ReadAHCIFunction)device->readRawSector;
	

	int target = fat_target(device,path);
	target = dummy;
	if(target!=0){
		int i = 0;
		int gz = 0;
		readraw(device,target,1,(unsigned short *)isobuffer);
		int ctx = 0;
		for(int i = 0 ; i< strlen(path) ; i++){
			if(path[i]=='/'){
				ctx = i+1;
			}
		}
		unsigned char* fname = (unsigned char*)(path+ctx);
		for(i = 0 ; i < 1000 ; i++){
			int t = 2;
			if(isobuffer[i]==';'&&isobuffer[i+1]=='1'){
				int fnd = 0;
				for(int z = 1 ; z < 30 ; z++){
					if(isobuffer[i-z]==t){
						fnd = z;
						break;
					}
					t++;
				}
				if(fnd){
					//t -= 2;
					int w = 0;
					gz = 1;
					for(int z = 2 ; z < t ; z++){
						if(fname[w++]!=isobuffer[(i-t)+z]){
							gz = 0;
						}
					}
					if(gz){
						return 1;
					}
				}
			}
		}
	}
	return 0;
}

/**
 * Retrieve the files list in the given directory(path)
 * of the device and put it in buffer separated by ';'
 **/
void fat_dir(Device *device,char* path,char *buffer){
	ReadAHCIFunction readraw = (ReadAHCIFunction)device->readRawSector;
	unsigned short* rxbuffer = (unsigned short*) malloc(512);
	readraw(device,0,1,rxbuffer); 
	fat_BS_t* fat_boot = (fat_BS_t*) rxbuffer;
	fat_extBS_32_t* fat_boot_ext_32 = (fat_extBS_32_t*) fat_boot->extended_section;
	
	unsigned long total_sectors 	= (fat_boot->total_sectors_16 == 0)? fat_boot->total_sectors_32 : fat_boot->total_sectors_16;
	unsigned long fat_size 		= (fat_boot->table_size_16 == 0)? fat_boot_ext_32->table_size_32 : fat_boot->table_size_16;
	unsigned long root_dir_sectors 	= ((fat_boot->root_entry_count * 32) + (fat_boot->bytes_per_sector - 1)) / fat_boot->bytes_per_sector;
	unsigned long first_data_sector = fat_boot->reserved_sector_count + (fat_boot->table_count * fat_size) + root_dir_sectors;
	unsigned long data_sectors 	= total_sectors - (fat_boot->reserved_sector_count + (fat_boot->table_count * fat_size) + root_dir_sectors);
	unsigned long total_clusters 	= data_sectors / fat_boot->sectors_per_cluster;
	
	unsigned long first_sector_of_cluster = 0;
	if(total_clusters < 4085){
		
	}else if(total_clusters < 65525){
		first_sector_of_cluster = fat_boot->reserved_sector_count + (fat_boot->table_count * fat_boot->table_size_16);
	}else if (total_clusters < 268435445){
		unsigned long root_cluster_32 = fat_boot_ext_32->root_cluster;
		first_sector_of_cluster = ((root_cluster_32 - 2) * fat_boot->sectors_per_cluster) + first_data_sector;
	}
	
	unsigned short* fatbuffer = (unsigned short*) malloc(512);
	readraw(device,first_sector_of_cluster,1,fatbuffer); 
	unsigned long pathoffset = 0;
	unsigned long pathfileof = 0;
	unsigned char filename[11];
	
	//
	// pad opzoeken
	while(1){
		// buffer leegmaken
		for(int i = 0 ; i < 11 ; i++){
			filename[i] = 0x00;
		}
		// buffer vullen met nieuw woord
		unsigned char erstw = path[pathoffset];
		if(erstw==0x00){
			break;
		}
		
		for(int i = 0 ; i < 11 ; i++){
			unsigned char deze = path[pathoffset++];
			if(deze=='/'){
				break;
			}
			if(deze==0x00){
				pathoffset--;
				break;
			}
			filename[pathfileof++] = deze;
		}
		
		unsigned long offset = 0;
		unsigned long newsect = 0;
		while(1){
			fat_dir_t* currentdir = (fat_dir_t*) (fatbuffer + offset);
			offset += sizeof(fat_dir_t);
			unsigned char eersteteken = currentdir->name[0];
			if(eersteteken==0x00){
				break;
			}
			if(eersteteken==0xE5){
				continue;
			}
			unsigned long sigma = 0;
			unsigned long yotta = 1;
			for(int i = 0 ; i < 11 ; i++){
				if(currentdir->name[i]!=0x00){
					if(currentdir->name[i]==filename[sigma++]){
						// naam (nog) hetzelfde
					}else{
						// naam is veranderd.
						yotta = 0;
					}
				}
			}
			if(yotta){
				newsect = ((currentdir->clusterhigh << 8) &0xFFFF0000) | (currentdir->clusterlow & 0xFFFF);
				break;
			}
		}
		if(newsect){
			first_sector_of_cluster = ((newsect - 2) * fat_boot->sectors_per_cluster) + first_data_sector;
			readraw(device,first_sector_of_cluster,1,fatbuffer);
		}else{
			printf("CANNOT FIND DIR\n");
			for(;;);
		}
		
	}
	
	//
	// bestanden printen
	unsigned long offset = 0;
	unsigned long bufofs = 0;
	while(1){
		fat_dir_t* currentdir = (fat_dir_t*) (fatbuffer + offset);
		offset += sizeof(fat_dir_t);
		unsigned char eersteteken = currentdir->name[0];
		if(eersteteken==0x00){
			break;
		}
		if(eersteteken==0xE5){
			continue;
		}
		for(int i = 0 ; i < 11 ; i++){
			if(currentdir->name[i]!=0x00){
				buffer[bufofs++] = currentdir->name[i];
			}
		}
		buffer[bufofs++] = ';';
	}
	buffer[bufofs-1] = 0x00;
}

void initialiseFAT(Device* device){
	ReadAHCIFunction readraw = (ReadAHCIFunction)device->readRawSector;
	unsigned short* buffer = (unsigned short*) malloc(512);
	readraw(device,0,1,buffer); 
	fat_BS_t* fat_boot = (fat_BS_t*) buffer;
	fat_extBS_32_t* fat_boot_ext_32 = (fat_extBS_32_t*) fat_boot->extended_section;
	printf("[FAT] FAT detected!\n");
	printf("[FAT] OEM-name: ");
	for(int i = 0 ; i < 8 ; i++){
		printf("%c",fat_boot->oem_name[i]);
	}
	printf("\n");
	unsigned long total_sectors 	= (fat_boot->total_sectors_16 == 0)? fat_boot->total_sectors_32 : fat_boot->total_sectors_16;
	unsigned long fat_size 		= (fat_boot->table_size_16 == 0)? fat_boot_ext_32->table_size_32 : fat_boot->table_size_16;
	unsigned long root_dir_sectors 	= ((fat_boot->root_entry_count * 32) + (fat_boot->bytes_per_sector - 1)) / fat_boot->bytes_per_sector;
	unsigned long first_data_sector = fat_boot->reserved_sector_count + (fat_boot->table_count * fat_size) + root_dir_sectors;
	unsigned long first_fat_sector 	= fat_boot->reserved_sector_count;
	unsigned long data_sectors 	= total_sectors - (fat_boot->reserved_sector_count + (fat_boot->table_count * fat_size) + root_dir_sectors);
	unsigned long total_clusters 	= data_sectors / fat_boot->sectors_per_cluster;
	
	printf("[FAT] total sectors %x \n",total_sectors);
	printf("[FAT] fatsize %x \n",fat_size);
	printf("[FAT] root dir sectors %x \n",root_dir_sectors);
	printf("[FAT] first data sector %x \n",first_data_sector);
	printf("[FAT] first fat sector %x \n",first_fat_sector);
	printf("[FAT] data sectors %x \n",data_sectors);
	printf("[FAT] total clusters %x \n",total_clusters);
	unsigned long first_sector_of_cluster = 0;
	if(total_clusters < 4085){
		printf("[FAT] FAT-type is FAT12\n");
	}else if(total_clusters < 65525){
		first_sector_of_cluster = fat_boot->reserved_sector_count + (fat_boot->table_count * fat_boot->table_size_16);
		printf("[FAT] FAT-type is FAT16 at cluster %x \n",first_sector_of_cluster);
	}else if (total_clusters < 268435445){
		printf("[FAT] FAT-type is FAT32\n");
		unsigned long root_cluster_32 = fat_boot_ext_32->root_cluster;
		first_sector_of_cluster = ((root_cluster_32 - 2) * fat_boot->sectors_per_cluster) + first_data_sector;
		printf("[FAT] root cluster is %x and its sector is %x \n",root_cluster_32,first_sector_of_cluster);
	}else{ 
		printf("[FAT] FAT-type is ExFAT\n");
	}
	if(first_sector_of_cluster==0){
		printf("[FAT] FAT-dir is 0. No FAT supported!\n");
		return;
	}
	unsigned short* fatbuffer = (unsigned short*) malloc(512);
	readraw(device,first_sector_of_cluster,1,fatbuffer); 
	unsigned long offset = 0;
	while(1){
		fat_dir_t* currentdir = (fat_dir_t*) (fatbuffer + offset);
		offset += sizeof(fat_dir_t);
		unsigned char eersteteken = currentdir->name[0];
		if(eersteteken==0x00){
			break;
		}
		if(eersteteken==0xE5){
			continue;
		}
		printf("[FAT] found file: ");
		for(unsigned int i = 0 ; i < 11 ; i++){
			printf("%c",currentdir->name[i]);
		}
		printf("\n");
	}
	device->dir	= (unsigned long)&fat_dir;
	device->readFile= (unsigned long)&fat_read;
	device->writeFile= (unsigned long)&fat_write;
}
