#include <sys/timeb.h>
#include <sys/wait.h>

#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>

#define MSSEC 1000
#define WAIT 10
#define BUFFER_SIZE 131072
#define UNIT "KB/s"
#define UNITSIZE 1000

char *subprocess;

int gettime(char *buffer, size_t size) {
    time_t rawtime;
    struct tm *timeinfo;
    
    if (time(&rawtime) == -1) {
        return -1;
    }

    timeinfo = localtime( &rawtime );

    if (timeinfo == NULL) {
        return -1;
    }
    
    strftime(buffer, size, "%Y-%m-%d %H:%M:%S", timeinfo);
}

FILE *openlog(const char *logfile) {
    return fopen(logfile, "a");
}

unsigned long long msdiff(struct timeb now, struct timeb then) {
  return ((now.time - then.time) * MSSEC) + (now.millitm - then.millitm);
}

int monitor_process(int in_fd, int out_fd, int argc, char *argv[]) {
  const char *name = argv[0];
  const char *logfile = argv[1];
  
  char *buffer = malloc(BUFFER_SIZE * sizeof(char));
  
  if (buffer == NULL) {
    fprintf(stderr, "%s: malloc: %s\n", name, strerror(errno));
    return 1;
  }

  size_t read_s, write_s;

  struct timeb now, then;

  unsigned long long diff = 0;
  unsigned long long byte = 0;

  char now_time[512];
  
  FILE *log_fd;
  
  if (ftime(&then)) {
    fprintf(stderr, "%s: ftime: %s\n", name, strerror(errno));
    return 1;
  }
  
  while(1) {
    if (ftime(&then)) {
      fprintf(stderr, "%s: ftime: %s\n", name, strerror(errno));
      return 1;
    }
    
    read_s = read(in_fd, buffer, BUFFER_SIZE);
    
    if (read_s == -1) {
      fprintf(stderr, "%s: read: %s\n", name, strerror(errno));
      return 1;
    }
    
    if (read_s == 0) {
      break;
    }
    
    write_s = write(out_fd, buffer, read_s);
    
    if (write_s == -1) {
      fprintf(stderr, "%s: write: %s\n", name, strerror(errno));
      break;
    }

    if (ftime(&now)) {
      fprintf(stderr, "%s: ftime: %s\n", name, strerror(errno));
      break;
    }
    
    diff += msdiff(now, then);
    byte += write_s;
    
    if (diff <= (WAIT * MSSEC)) {
      continue;
    }

    if ((log_fd = openlog(logfile)) == NULL) {
      fprintf(stderr, "%s: openlog: %s\n", name, strerror(errno));
      return 1;
    }
    
    if (gettime(now_time, 512) == -1) {
      fprintf(stderr, "%s: gettime: %s\n", name, strerror(errno));
      return 1;
    }
    
    fprintf(log_fd, "%s: %s - %lld " UNIT "\n", name, now_time, ((byte / diff) * 1000) / UNITSIZE);
    fclose(log_fd);

    diff = 0;
    byte = 0;
  }
  
  return 0;
}

int fork_proc(int (*cb)(int, int, int, char**), FILE *io[], int argc, char *argv[]) {
    int c_in[2];
    int c_out[2];
    
    if (io[0] == NULL) {
        pipe(c_in);
    }

    if (io[1] == NULL) {
        pipe(c_out);
    }
    
    int p = fork();

    if (p == -1) {
        perror("fork");
        return 0;
    }
    
    if (p == 0) {
        if (io[0] == NULL) {
            close(c_in[1]);         // close child in write end.
            
            io[0] = fdopen(c_in[0], "r");

            if (io[0] == NULL) {
                perror("fdopen");
                _exit(1);
            }
        }
        
        if (io[1] == NULL) {
            close(c_out[0]);         // close child out read end.
            io[1] = fdopen(c_out[1], "w");
            
            if (io[1] == NULL) {
                perror("fdopen");
                _exit(1);
            }
        }
        
        _exit(cb(fileno(io[0]), fileno(io[1]), argc, argv));
    }
    
    if (io[0] == NULL) {
        close(c_in[0]);
        
        io[0] = fdopen(c_in[1], "w");    // stdin write end for parent.
        
        if (io[0] == NULL) {
            perror("fdopen");
            _exit(1);
        }
    }
    
    if (io[1] == NULL) {
        close(c_out[1]);
        
        io[1] = fdopen(c_out[0], "r");   // stdout read end for parent.
        
        if (io[1] == NULL) {
            perror("fdopen");
            _exit(0);
        }
    }
    
    return p;
}

int main(int argc, char *argv[]) {
  if (argc < 3) {
    fprintf(stderr, "usage: pv <logfile> <subprocess> [arguments]\n");
    return 1;
  }

  int status;

  char* logfile = argv[1];
  
  char *pin_argv[] =  {"IN ", logfile};
  char *pout_argv[] = {"OUT", logfile};

  FILE *pin_io[2] = {stdin, NULL};
  int pin = fork_proc(&monitor_process, pin_io, 2, pin_argv);
  
  FILE *pout_io[2] = {NULL, stdout};
  int pout = fork_proc(&monitor_process, pout_io, 2, pout_argv);
  
  if (waitpid(pin, &status, WNOHANG) != 0) {
    fprintf(stderr, "fork: unable to start monitor child\n");
    return 1;
  }

  if (waitpid(pout, &status, WNOHANG) != 0) {
    fprintf(stderr, "fork: unable to start monitor child\n");
    return 1;
  }
  
  char **argvc = &argv[2];
  
  int cpid = fork();

  if (cpid == -1) {
    perror("fork");
    return 1;
  }
  
  if (cpid == 0) {
    if (dup2(fileno(pin_io[1]), 0) == -1) {
      perror("dup2");
      _exit(1);
    }
    
    if (dup2(fileno(pout_io[0]), 1) == -1) {
      perror("dup2");
      _exit(1);
    }
    
    if (execvp(argvc[0], argvc) == -1) {
      perror("execvp");
    }
    
    fprintf(stderr, "stopping: cat\n");

    _exit(1);
  }
  
  int i;
  
  for (i = 0; i < 3; i++) {
    pid_t pid = waitpid(0, &status, 0);

    if (WIFEXITED(status)) {
      // this is normal, each child process exits O.K.
      continue;
    }
    
    if (WIFSIGNALED(status)) {
      // completely bail out, we do not, and should not handle this.
      return 1;
    }
  }
  
  return status;
}
