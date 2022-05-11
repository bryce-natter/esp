#include "prc.h"
#include <stdio.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>



int prc_driver;

void reconfigure_fpga(pbs_map *pbs)
{
	if(ioctl(prc_driver, PRC_RECONFIGURE, pbs)) {
		perror("Failed to write bitsream to driver");
		return;
	}

}

int main(int argc, char **argv)
{
	static const char filename[] = "/dev/prc";
	pbs_map pbs;

	int pbs_fd = {0};
	struct stat sb;

	if (argc != 3)
	{
		fprintf(stderr, "Invalid arguement count\n");
		return -1;
		
	}

	if ((prc_driver = open(filename, O_RDWR)) == -1) {
		fprintf(stderr, "Unable to open device %s\n", filename);
		return -1;
	}

	if ((pbs_fd = open(argv[2], O_RDONLY)) == -1) {
		fprintf(stderr, "Unable to open %s\n", argv[2]);
		return -1;
	}

	printf("Opened Bitstream fd: %d\n", pbs_fd);

	fstat(pbs_fd, &sb);
	pbs.pbs_size = sb.st_size;
	printf("Bitstream \"%s\" size: [%lu, %lu]\n", argv[2], pbs.pbs_size, sb.st_size);
	fflush(stdin);

	pbs.pbs_mmap = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, pbs_fd, 0);
	if (pbs.pbs_mmap == MAP_FAILED) {
		fprintf(stderr, "Unable to mmap %s\n", argv[2]);
		return -1;
	}
	printf("mmap'd bitstream...\n");

	pbs.pbs_tile_id = atoi(argv[1]);

	reconfigure_fpga(&pbs);
}
