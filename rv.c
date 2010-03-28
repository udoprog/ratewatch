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

int gettime(buffer, size)
    char *buffer;
    size_t size;
{
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

FILE *openlog(logfile)
    const char *logfile;
{
    return fopen(logfile, "a");
}

unsigned long long msdiff(now, then)
    struct timeb now;
    struct timeb then;
{
  return ((now.time - then.time) * MSSEC) + (now.millitm - then.millitm);
}

int monitor_process(in_fd, out_fd, argc, argv)
    int in_fd;
    int out_fd;
    int argc;
    char *argv[];
{
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
        
        if ((read_s = read(in_fd, buffer, BUFFER_SIZE)) == -1) {
            fprintf(stderr, "%s: read: %s\n", name, strerror(errno));
            return 1;
        }
        
        if (read_s == 0) {
            break;
        }
        
        if ((write_s = write(out_fd, buffer, read_s)) == -1) {
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
        
        if (fclose(log_fd) == EOF) {
            fprintf(stderr, "%s: fclose: %s\n", name, strerror(errno));
            return 1;
        }

        diff = 0;
        byte = 0;
    }
    
    return 0;
}

pid_t fork_proc(cb, io, argc, argv)
    int (*cb)(int, int, int, char**);
    int io[];
    int argc;
    char *argv[];
{
    int c_in[2];
    int c_out[2];
    
    if (io[0] == -1) {
        pipe(c_in);
    }
    
    if (io[1] == -1) {
        pipe(c_out);
    }
    
    int p = fork();

    if (p == -1) {
        perror("fork");
        return -1;
    }
    
    if (p == 0) {
        if (io[0] == -1) {
            close(c_in[1]);         // close child in write end.
            io[0] = c_in[0];
        }
        
        if (io[1] == -1) {
            close(c_out[0]);         // close child out read end.
            io[1] = c_out[1];
        }
        
        _exit(cb(io[0], io[1], argc, argv));
    }
    
    if (io[0] == -1) {
        close(c_in[0]);
        io[0] = c_in[1];    // stdin write end for parent.
    }
    
    if (io[1] == -1) {
        close(c_out[1]);
        io[1] = c_out[0];   // stdout read end for parent.
    }
    
    return p;
}

int spawn_process(io, argv)
    int io[];
    char *argv[];
{
    if (dup2(io[0], 0) == -1) {
      perror("dup2");
      return -1;
    }
    
    if (dup2(io[1], 1) == -1) {
      perror("dup2");
      return -1;
    }
    
    if (execvp(argv[0], argv) == -1) {
      perror("execvp");
    }

    return -1;
}

int main(argc, argv)
    int argc;
    char *argv[];
{
    if (argc < 3) {
        fprintf(stderr, "usage: pv <logfile> <subprocess> [arguments]\n");
        return 1;
    }
  
    const char *logfile = argv[1];

    pid_t p_stdout_pid, p_stdin_pid, child_pid;
    
    int io[2];
    
    {
        /* fork and create stdin monitor process */
        const char *p_stdin_argv[] =  {"IN ", logfile};
        int p_stdin_io[2] = {fileno(stdin), -1};
        
        if ((p_stdin_pid = fork_proc(&monitor_process, p_stdin_io, 2, p_stdin_argv)) == -1) 
        {
            perror("fork_proc");
            return 1;
        }

        io[0] = p_stdin_io[1];
    }
    
    {
        /* fork and create stdout monitor process */
        const char *p_stdout_argv[] = {"OUT", logfile};
        int p_stdout_io[2] = {-1, fileno(stdout)};
        
        if ((p_stdout_pid = fork_proc(&monitor_process, p_stdout_io, 2, p_stdout_argv)) == -1)
        {
            perror("fork_proc");
            return 1;
        }
        
        io[1] = p_stdout_io[0];
    }
  
    {
        /* check and make sure that the processes are not prematurely dead */
        
        if (waitpid(p_stdin_pid, NULL, WNOHANG) != 0) {
            fprintf(stderr, "fork: unable to start monitor child\n");
            return 1;
        }
        
        if (waitpid(p_stdout_pid, NULL, WNOHANG) != 0) {
            fprintf(stderr, "fork: unable to start monitor child\n");
            return 1;
        }
    }
    
    if ((child_pid = fork()) == -1) {
        perror("fork");
        return 1;
    }
  
    if (child_pid == 0) {
        spawn_process(io, &argv[2]);
        // this should not happen
        _exit(1);
    }
    
    {
        int i;
        int status;
        
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
    }
    
    return 0;
}
