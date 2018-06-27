#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#define DEVFILE "/dev/mydev"

int open_file(void)
{
	int fd;

	fd = open(DEVFILE, O_RDWR);
	if (fd < 0) {
		perror("open");
	}
	return (fd);
}

void close_file(int fd)
{
	if (close(fd) != 0) {
		perror("close");
	}
}

int main(void)
{
	int fd;
	int status;

	fd = open_file();

	if (fork() == 0) { /* child process */
		sleep(10);
		close_file(fd);
		exit(1);
	}


	sleep(5);

	close_file(fd);

	wait(&status);
	return 0;
}
