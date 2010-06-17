#define _XOPEN_SOURCE 600
#include <errno.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ERR_USAGE 128
#define ERR_FILE 129
#define ERR_SUBSHELL 130

#define BUFSIZE 1024
#define BLOCKSIZE 4096

#define MAX(x,y) (x > y ? x : y)
#define MIN(x,y) (x > y ? y : x)

char buffer[BUFSIZE];

char* progname;

void usage(char *prog) {
  fprintf(stderr, "Usage: %s [-t] file command\n", prog);
  exit(ERR_USAGE);
}

pid_t shellcmd(char* command, int fds[2]) {
  pid_t pid;
  int ins[2], outs[2];
  pipe(ins);
  pipe(outs);
  fds[1] = ins[1];
  fds[0] = outs[0];

  if ((pid = fork()) == 0) {
    char* cmd_argv[4];
    close(ins[1]);
    close(outs[0]);
    dup2(ins[0], 0);
    dup2(outs[1], 1);

    /* Find the shell to use.  We assume -c can be used to run a
       command. */
    cmd_argv[0] = getenv("SHELL");
    if (!cmd_argv[0] || !*cmd_argv[0]) {
      cmd_argv[0] = "/bin/sh";
    }
    cmd_argv[1] = "-c";
    cmd_argv[2] = command;
    cmd_argv[3] = 0;

    execvp(cmd_argv[0], cmd_argv);
    
    /* execvp failed */
    fprintf(stderr, "%s: %s: %s\n", progname, cmd_argv[0], strerror(errno));
    exit(ERR_SUBSHELL);
  }
  close(ins[0]);
  close(outs[1]);
  return pid;
}

/* The command may write to its stdout faster than it reads from its
   stdin.  Hence, we define a simple in-memory buffering scheme to
   store the surplus until we've consumed more of the file.  */

typedef struct writeblock {
  char block[BLOCKSIZE];
  struct writeblock *next;
} writeblock;

writeblock *firstblock, *lastblock;
off_t rpoint = 0, wpoint = 0;

/* Number of bytes left to be read in the block buffer. */
size_t ready() {
  if (firstblock == lastblock) {
    return wpoint - rpoint;
  } else {
    return BLOCKSIZE - rpoint;
  }
}

/* Free number of bytes in the block being read. */
size_t available() {
  return BLOCKSIZE - wpoint;
}

void write_to_blocks(const char *buf, size_t nbyte) {
  if (nbyte > available()) {
    size_t remainder = nbyte - available(lastblock);
    writeblock *block = malloc(sizeof(writeblock));
    block->next  = NULL;
    memcpy(lastblock->block+wpoint, buf, nbyte-remainder);
    wpoint = 0;
    lastblock->next = block;
    lastblock = block;
    write_to_blocks(buf+(nbyte-remainder), remainder);
  } else {
    memcpy(lastblock->block+wpoint, buf, nbyte);
    wpoint += nbyte;
  }
}

ssize_t read_from_blocks(char *buf, size_t max) {
  if (max < ready()) {
    memcpy(buf, firstblock->block+rpoint, max);
    rpoint += max;
    return max;
  } else {
    size_t read = ready();
    size_t remainder = max-read;
    memcpy(buf, firstblock->block+rpoint, read);
    rpoint += read;
    if (firstblock != lastblock && rpoint == BLOCKSIZE) {
      writeblock *tmp = firstblock;
      firstblock = firstblock->next;
      rpoint = 0;
      free(tmp);
      return read + read_from_blocks(buf+read, remainder);
    } else {
      return read;
    }
  }
}

off_t readpos = 0;
ssize_t freeroom = 0;

/* Write as much to the file as possible, the rest to the in-memory
   buffer. */
void write_to_file(int fd, char* buf, ssize_t nbytes) {
  ssize_t ret = 0;
  do {
    freeroom -= ret;
    buf += ret;
    nbytes -= ret;
    ret = write(fd, buf, MIN(freeroom, nbytes));
  } while (freeroom > 0 &&
           nbytes > 0
           && (ret > -1 || errno == EINTR));
  write_to_blocks(buf, nbytes);
}

void mainproc(int fd, int out, int in) {
  unsigned char isopen = 1;
  off_t bufpos = 0;
  ssize_t datasize = 0, ret;

  fd_set rfds, wfds;
    
  for (;;) {
    if (isopen && bufpos == datasize) {
      errno = 0;
      if ((ret = pread(fd, buffer, BUFSIZE, readpos)) > 0) {
        bufpos = 0;
        datasize = ret;
        readpos += ret;
        freeroom += ret;
      } else if (ret == 0 && errno != EINTR) {
        close(out);
        isopen = 0;
      }
    }

    FD_ZERO(&wfds);
    FD_SET(out, &wfds);
    FD_ZERO(&rfds);
    FD_SET(in, &rfds);

    pselect(MAX(in,out)+1, &rfds, &wfds, NULL, NULL, NULL);

    if (FD_ISSET(in, &rfds)) {
      char buf[BUFSIZE];
      ssize_t ret;
        
      if ((ret = read(in, buf, BUFSIZE)) > 0) {
        write_to_file(fd, buf, ret);
      } else if (ret == 0 && errno != EINTR) {
        break;
      }
    }
    if (FD_ISSET(out, &wfds)) {
      bufpos += write(out, buffer+bufpos, datasize - bufpos);
    }
  }
}

int main(int argc, char **argv) {
  char *file, *command;
  int fd;
  int truncate = 0;

  progname = argv[0];

  if (argc == 4) {
    if (strcmp(argv[1],"-t") != 0) {
      usage(progname);
    }
    truncate = 1;
  } else if (argc != 3) {
    usage(progname);
  }

  file = argv[1], command = argv[2];

  firstblock = lastblock = malloc(sizeof(writeblock));
  firstblock->next = NULL;

  if ((fd = open(file, O_RDWR)) != -1) {
    int pipes[2], status;
    struct sigaction act;
    ssize_t ret, got, written;
    pid_t child;

    /* We will most likely get a SIGPIPE when the subcommand
       terminates, so ignore it. */
    act.sa_handler = SIG_IGN;
    sigaction(SIGPIPE, &act, NULL);

    if ((child = shellcmd(command, pipes)) == -1) {
      perror(progname);
      exit(ERR_SUBSHELL);
    }

    /* Read/write as much as we can. */
    mainproc(fd, pipes[1], pipes[0]);

    /* Write any surplus in the block buffer to the file. */
    ret = 0;
    written = 0;
    while ((got = read_from_blocks(buffer, BUFSIZE))) {
      do {
        written += ret;
        ret = write(fd, buffer+written, got-written);
      } while (got != written && (ret > -1 || errno == EINTR));
    }

    /* Don't truncate the file unless the command exited properly, or
       the user passed -t. */
    waitpid(child, &status, 0);
    if (WEXITSTATUS(status) == 0 || truncate) {
      ftruncate(fd, lseek(fd, 0, SEEK_CUR));
    }
    exit(WEXITSTATUS(status));
  } else {
    fprintf(stderr, "%s: %s: %s\n", progname, file, strerror(errno));
    exit(ERR_FILE);
  }
}
