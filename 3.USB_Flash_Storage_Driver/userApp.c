#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>

#define MAX 100

int main(void)
{
  int fd, ret;
  char wrbuff[MAX] = "The Eagle Has Landed";
  char rdbuff[MAX] = {0};

  fd = open("/dev/storage0", O_RDWR);
  if(fd < 0)
  {
    printf("Open failed with %d\n",fd);
    return fd;
  }

  ret = write(fd, wrbuff, strlen(wrbuff));

  ret = read(fd, rdbuff, sizeof(rdbuff));

  close(fd);
  return 0;
}

