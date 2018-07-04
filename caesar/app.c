#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#define DEVFILE "/dev/caesar"
#define STRING1 "hello, world!"
#define STRING2 "Don't be Lazy!"
#define STRING3 "I'm nelio"

int open_file(char *filename)
{
	int fd;
	fd = open(filename, O_RDWR);

	if (fd == -1) {
		perror("open");
	}
	return fd;
}

void close_file(int fd)
{
	if (close(fd) < 0) {
		perror("close");
	}
}

void read_file(int fd, int len)
{
	unsigned char buf[256] = {'\0'};

	ssize_t ret;

	ret = read(fd, buf, len);
	if (ret > 0) {
		printf("%s\n", buf);
	} else {
		perror("read");
	}
}

void write_file(int fd, char *buf)
{
	ssize_t ret;

	ret = write(fd, buf, strlen(buf));
	if (ret <= 0) {
		perror("write");
	}
}

int main(void)
{
	int fd;
	char *str1 = STRING1;
	char *str2 = STRING2;
	char *str3 = STRING3;

	fd = open_file(DEVFILE);

	write_file(fd, str1);
	read_file(fd, strlen(str1));

	write_file(fd, str2);
	read_file(fd, strlen(str2));

	write_file(fd, str3);
	read_file(fd, strlen(str3));

	close_file(fd);

	return 0;
}
