#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <signal.h>
#include <fcntl.h>

#define SOCK_PATH "/tmp/sdmened.sock"
#define PID_PATH "/tmp/sdmened.pid"

static int alive(void) {
  FILE *pf = fopen(PID_PATH, "r");
  if (!pf) return 0;
  int pid;
  if (fscanf(pf, "%d", &pid) != 1) { fclose(pf); return 0; }
  fclose(pf);
  if (kill(pid, 0) == 0) return 1;
  unlink(SOCK_PATH);
  unlink(PID_PATH);
  return 0;
}

int main(int argc, char **argv) {
  if (!alive()) {
    char *dir = NULL;
    char self[4096];
    ssize_t len = readlink("/proc/self/exe", self, sizeof(self) - 1);
    if (len > 0) {
      self[len] = '\0';
      char *slash = strrchr(self, '/');
      if (slash) {
        *slash = '\0';
        dir = self;
      }
    }
    pid_t pid = fork();
    if (pid == 0) {
      setsid();
      int dn = open("/dev/null", O_RDWR);
      dup2(dn, 0); dup2(dn, 1); dup2(dn, 2);
      if (dn > 2) close(dn);
      if (dir) {
        char path[4096];
        snprintf(path, sizeof(path), "%s/sdmened", dir);
        execl(path, "sdmened", NULL);
      }
      execlp("sdmened", "sdmened", NULL);
      _exit(1);
    }
    if (pid > 0) {
      for (int i = 0; i < 200; i++) {
        usleep(10000);
        if (alive()) break;
      }
    }
    if (!alive()) return 1;
  }

  struct sockaddr_un addr;
  addr.sun_family = AF_UNIX;
  strcpy(addr.sun_path, SOCK_PATH);
  int fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0) return 1;
  if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) { close(fd); return 1; }
  signal(SIGPIPE, SIG_IGN);
  char result[4096];
  int n = read(fd, result, sizeof(result) - 1);
  close(fd);
  if (n > 0) {
    result[n] = '\0';
    if (result[0]) {
      puts(result);
      return 0;
    }
  }
  return 1;
}
