/* 2016.10.1 roa
gcc select.c
to test select with stdin and midi port

start aplaymidi --port 45:0 a.mid 
and then type a string and \n on keyboard
*/

#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h> /* read() */

int main () {
unsigned char buf[1024],n;
fd_set read_fds;
int mipo;

if ((mipo = open ("/dev/snd/midiC7D1", O_RDWR)) < 0) {
  perror ("fopen C7D1");
}

while(1) {
  FD_ZERO(&read_fds);
  FD_SET(mipo, &read_fds);
  FD_SET(fileno(stdin), &read_fds);

  if (select(mipo+1,&read_fds,NULL,NULL,NULL) == -1){
    perror("select:");
    exit(1);
  }
  
  puts("here");

  if (FD_ISSET(mipo, &read_fds)){
    n = read(mipo,buf,100);
    printf("%d %02x %02x %02x\n",n,buf[0],buf[1],buf[2]);
  }

  if (FD_ISSET(fileno(stdin), &read_fds)){
    fgets(buf,100,stdin);	/* doesn't block here, nice! A second fgets blocks */
    printf("%s\n",buf);
  }
 }
 return(0);
}
