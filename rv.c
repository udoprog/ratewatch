#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/timeb.h>

#define MSSEC 1000
#define WAIT 1
#define BUFFER_SIZE (1048576 * 10)
#define UNIT "KB/s"
#define UNITSIZE 1000

FILE *logfile_fd;
char *logfile;
char *buffer;

int openlog() {
  logfile_fd = fopen(logfile, "w");
  return logfile_fd == NULL ? -1 : 0;
}

int closelog() {
  fclose(logfile_fd);
  return 0;
}

unsigned long long msdiff(struct timeb now, struct timeb then) {
  return ((now.time - then.time) * MSSEC) + (now.millitm - then.millitm);
}

void monitor_process(int in_fd, int out_fd) {
  size_t read_s, write_s;

  struct timeb now, then;

  unsigned long long diff = 0;
  unsigned long long bytecount = 0;
  
  if (ftime(&then)) {
    perror("ftime");
    return;
  }
  
  while(1) {
    if (ftime(&then)) {
      perror("ftime");
      return;
    }
    
    read_s = read(in_fd, buffer, BUFFER_SIZE);
    
    if (read_s == -1) {
      perror("read");
      return;
    }
    
    if (read_s == 0) {
      break;
    }
    
    write_s = write(out_fd, buffer, read_s);
    
    if (write_s == -1) {
      perror("write");
      break;
    }

    if (ftime(&now)) {
      perror("ftime");
      break;
    }
    
    diff += msdiff(now, then);
    bytecount += write_s;
    
    if (diff > (WAIT * MSSEC)) {
      if (openlog() == -1) {
        perror("openlog");
        break;
      }
      
      fprintf(logfile_fd, "rate: %lld " UNIT "\n", ((bytecount / diff) * 1000) / UNITSIZE);
      fprintf(logfile_fd, "diff: %lld\n", diff);
      fprintf(logfile_fd, "byte: %lld\n", bytecount);
      
      if (closelog() == -1) {
        perror("closelog");
        break;
      }
      
      diff = 0;
      bytecount = 0;
    }
  }
}

int main(int argc, char *argv[]) {
  if (argc < 2) {
    fprintf(stderr, "usage: pv <logfile>\n");
    return 1;
  }
  
  logfile = argv[1];
  
  if (logfile == NULL) {
    return 1;
  }
  
  if (openlog() == -1) {
    perror("openlog");
    return 1;
  }
  
  fprintf(logfile_fd, "NO_SAMPLE\n");
  
  if (closelog() == -1) {
    perror("closelog");
    return 1;
  }
  
  buffer = malloc(BUFFER_SIZE * sizeof(char));
  
  if (buffer == NULL) {
    perror("malloc");
    return 1;
  }
  
  monitor_process(fileno(stdin), fileno(stdout));
}
