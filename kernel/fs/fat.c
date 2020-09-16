#include "../kernel.h"
#define BLOCK_SIZE	512
#define ENTRY_UNUSED_FLAG 0xE5

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
typedef void* (*ReadAHCIFunction)(Device *, unsigned long, unsigned char, unsigned short *);
typedef void* (*WriteAHCIFunction)(Device *, unsigned long, unsigned char, unsigned short *);

/**
 * Get the first sectors for data (data_sector) and the first sector of the cluster (cluster_sector)
 * returns the fat boot sector
 **/
fat_BS_t* fat_first_sectors(Device *device, unsigned long *cluster_sector, unsigned long *data_sector) {
	ReadAHCIFunction readraw = (ReadAHCIFunction)device->readRawSector;
	// Read the first block of the device/partition
	unsigned short* rxbuffer = (unsigned short*) malloc(BLOCK_SIZE);
	readraw(device, 0, 1, rxbuffer); 
	
	// Get the fat boot data
	fat_BS_t* fat_boot = (fat_BS_t*) rxbuffer;
	fat_extBS_32_t* fat_boot_ext_32 = (fat_extBS_32_t*) fat_boot->extended_section;
	
	unsigned long total_sectors 	= (fat_boot->total_sectors_16 == 0)? fat_boot->total_sectors_32 : fat_boot->total_sectors_16;
	unsigned long fat_size 			= (fat_boot->table_size_16 == 0)? fat_boot_ext_32->table_size_32 : fat_boot->table_size_16;
	unsigned long root_dir_sectors 	= ((fat_boot->root_entry_count * 32) + (fat_boot->bytes_per_sector - 1)) / fat_boot->bytes_per_sector;
	unsigned long first_data_sector = fat_boot->reserved_sector_count + (fat_boot->table_count * fat_size) + root_dir_sectors;
	unsigned long data_sectors 		= total_sectors - (fat_boot->reserved_sector_count + (fat_boot->table_count * fat_size) + root_dir_sectors);
	unsigned long total_clusters 	= data_sectors / fat_boot->sectors_per_cluster;
	
	// Get the first cluster of the partition
	unsigned long first_sector_of_cluster = 0;
	if(total_clusters < MAX_UNSIGNED_SHORT){
		// FAT 16
		first_sector_of_cluster = fat_boot->reserved_sector_count + (fat_boot->table_count * fat_boot->table_size_16);
	} else if (total_clusters < MAX_UNSIGNED_LONG){
		// FAT 32
		unsigned long root_cluster_32 = fat_boot_ext_32->root_cluster;
		first_sector_of_cluster = ((root_cluster_32 - 2) * fat_boot->sectors_per_cluster) + first_data_sector;
	} else {
		printf("FAT type not supported\n");
		for(;;);
	}

	if (cluster_sector)
		*cluster_sector = first_sector_of_cluster;
	if (data_sector)
		*data_sector = first_data_sector;
	
	return fat_boot;
}

/**
 * Returns true(1) if the given directory (path) exists in the device, false(0) otherwise.
 * Set entry to pointer to the entry/cluster
 * Set entry_sector to point to the entry/cluster sector
 */
char fat_exists(Device *device,char *path, unsigned long *entry_sector, unsigned short **entry) {
	ReadAHCIFunction readraw = (ReadAHCIFunction)device->readRawSector;
	
	unsigned long first_sector_of_cluster, first_data_sector;
	fat_BS_t* fat_boot = fat_first_sectors(device, &first_sector_of_cluster, &first_data_sector);

	unsigned short* fatbuffer = (unsigned short*) malloc(BLOCK_SIZE);
	readraw(device, first_sector_of_cluster, 1 ,fatbuffer); 
	
	unsigned long pathoffset = 0;
	unsigned long pathfileof = 0;
	unsigned char filename[11];
	
	//
	while(1){
		// clear buffer
		for(int i = 0 ; i < 11 ; i++){
			filename[i] = 0x00;
		}
		// fill buffer with new word
		unsigned char erstw = path[pathoffset];

		if(erstw==0x00){
			break;
		}
		
		// Get the filename (discard directory name)
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

		// now search the file in the file system		
		unsigned long offset = 0;
		unsigned long newsect = 0;
		while(1){
			fat_dir_t* currentdir = (fat_dir_t*) (fatbuffer + offset);
			offset += sizeof(fat_dir_t);
			unsigned char first_char = currentdir->name[0];
			// is end of the directory?
			if(first_char==0x00){
				break;
			}
			// else is it a unused entry
			if(first_char==ENTRY_UNUSED_FLAG){
				continue;
			}
			unsigned long sigma = 0;
			unsigned long yotta = 1;
			for(int i = 0 ; i < 11 ; i++){
				if(currentdir->name[i]!=0x00){
					if(currentdir->name[i] != filename[sigma++]){
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
			// Read the found cluster/file into fatbuffer
			readraw(device,first_sector_of_cluster,1,fatbuffer);
		}else{
			return 0;
		}
	}

	if (entry_sector)
		*entry_sector = first_data_sector;
	
	if (entry)
		*entry = fatbuffer;
	
	return 1;
}

char fat_exists_file(Device *device,char *path) {
	return fat_exists(device, path, 0, 0);
}

void fat_write(Device *device, char *path, char *buffer, unsigned long buffer_size) {
	unsigned long entry_sector = 0;
	unsigned short *fatbuffer = 0;
	if (!fat_exists(device, path, &entry_sector, &fatbuffer)) {
		printf("CANNOT FIND DIR\n");
		for(;;);
	}

	// Write to disk
	WriteAHCIFunction write_raw = (WriteAHCIFunction)device->writeRawSector;
	fat_dir_t *file =  (fat_dir_t*) fatbuffer;
	if (buffer_size > file->filesize) {
		// fragment the file.
	} else {
		write_raw(device, entry_sector, buffer_size, (unsigned short*)buffer);
	}
}

void fat_read(Device *device,char* path,char *buffer){
	ReadAHCIFunction readraw = (ReadAHCIFunction)device->readRawSector;
	 
	unsigned long first_sector_of_cluster;
	if (!fat_exists(device, path, &first_sector_of_cluster, 0)) {
		printf("CANNOT FIND DIR\n");
		for(;;);
	}
	
	readraw(device, first_sector_of_cluster, 1, (unsigned short*)buffer);
}

/**
 * Retrieve the files list in the given directory(path)
 * of the device and put it in buffer separated by ';'
 **/
void fat_dir(Device *device,char* path,char *buffer){
	unsigned short* fatbuffer;
	if (!fat_exists(device, path, 0, &fatbuffer)) {
		printf("CANNOT FIND DIR\n");
		for(;;);
	}

	//
	// print files
	unsigned long offset = 0;
	unsigned long bufofs = 0;
	while(1){
		fat_dir_t* currentdir = (fat_dir_t*) (fatbuffer + offset);
		offset += sizeof(fat_dir_t);
		unsigned char eersteteken = currentdir->name[0];
		if(eersteteken==0x00){
			break;
		}
		if(eersteteken==ENTRY_UNUSED_FLAG){
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
	unsigned short* buffer = (unsigned short*) malloc(BLOCK_SIZE);
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
	unsigned short* fatbuffer = (unsigned short*) malloc(BLOCK_SIZE);
	readraw(device,first_sector_of_cluster,1,fatbuffer); 
	unsigned long offset = 0;
	while(1){
		fat_dir_t* currentdir = (fat_dir_t*) (fatbuffer + offset);
		offset += sizeof(fat_dir_t);
		unsigned char eersteteken = currentdir->name[0];
		if(eersteteken==0x00){
			break;
		}
		if(eersteteken==ENTRY_UNUSED_FLAG){
			continue;
		}
		printf("[FAT] found file: ");
		for(unsigned int i = 0 ; i < 11 ; i++){
			printf("%c",currentdir->name[i]);
		}
		printf("\n");
	}
	device->dir		 	= (unsigned long)&fat_dir;
	device->readFile 	= (unsigned long)&fat_read;
	device->writeFile 	= (unsigned long)&fat_write;
	device->existsFile	= (unsigned long)&fat_exists_file;
}

#undef BLOCK_SIZE
#undef ENTRY_UNUSED_FLAG
