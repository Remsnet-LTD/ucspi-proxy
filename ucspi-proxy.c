#include <sys/types.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>
#include <str/str.h>
#include "ucspi-proxy.h"

const int msg_show_pid = 1;

static ssize_t bytes_client = 0;
static ssize_t bytes_server = 0;
bool opt_verbose = false;
static unsigned opt_timeout = 30;
pid_t pid;

int SERVER_FD = -1;

struct filter_node
{
  int fd;
  filter_fn filter;
  eof_fn at_eof;
  
  struct filter_node* next;
};

struct filter_node* filters = 0;

bool add_filter(int fd, filter_fn filter, eof_fn at_eof)
{
  struct filter_node* newnode = malloc(sizeof *filters);
  if(!newnode)
    return false;
  newnode->fd = fd;
  newnode->filter = filter;
  newnode->at_eof = at_eof;
  newnode->next = 0;
  if(!filters)
    filters = newnode;
  else {
    struct filter_node* ptr = filters;
    while(ptr->next)
      ptr = ptr->next;
    ptr->next = newnode;
  }
  return true;
}

bool del_filter(int fd)
{
  struct filter_node* prev = 0;
  struct filter_node* curr = filters;
  while(curr) {
    if(curr->fd == fd) {
      if(prev)
	prev->next = curr->next;
      else
	filters = curr->next;
      free(curr);
      return true;
    }
  }
  return false;
}

static void handle_fd(struct filter_node* filter)
{
  char buf[BUFSIZE+1];
  ssize_t rd = read(filter->fd, buf, BUFSIZE);
  if(rd == -1) {
    if (errno == EAGAIN || errno == EINTR)
      return;
    MSG1("Error encountered on FD %d", filter->fd);
    exit(1);
  }
  if(rd == 0) {
    MSG1("EOF on FD %d", filter->fd);
    if(filter->at_eof)
      filter->at_eof();
    else
      exit(0);
  }
  else {
    buf[rd] = 0; /* Add an extra NUL for string searches in filter */
    filter->filter(buf, rd);
  }
}

void write_client(const char* data, ssize_t size)
{
  while(size > 0) {
    ssize_t wr = write(CLIENT_OUT, data, size);
    switch(wr) {
    case 0:
      MSG0("Short write to client");
      exit(1);
    case -1:
      MSG0("Write to client failed");
      exit(1);
    default:
      data += wr;
      size -= wr;
    }
  }
  bytes_client += size;
}

void writes_client(const char* data)
{
  write_client(data, strlen(data));
}

void write_server(const char* data, ssize_t size)
{
  if(write(SERVER_FD, data, size) != size) {
    MSG0("Short write to server");
    exit(1);
  }
  bytes_server += size;
}

void writes_server(const char* data)
{
  write_server(data, strlen(data));
}

static void exitfn(void)
{
  MSG2("client %d server %d", bytes_client, bytes_server);
  filter_deinit();
}

void usage(const char* message)
{
  if(message)
    fprintf(stderr, "%s: %s\n", program, message);
  fprintf(stderr, "usage: %s [-v] [-t timeout] host port %s\n",
	  program, filter_usage);
  exit(1);
}

static void connfail(void)
{
  str buf = {0,0,0};
  str_copy4s(&buf,
	     filter_connfail_prefix,
	     "Connection to server failed: ",
	     strerror(errno),
	     filter_connfail_suffix);
  write_client(buf.s, buf.len);
  exit(0);
}

static void parse_args(int argc, char* argv[])
{
  int opt;
  unsigned tmp;
  char* end;
  while((opt = getopt(argc, argv, "vt:")) != EOF) {
    switch(opt) {
    case 'v':
      opt_verbose = true;
      break;
    case 't':
      tmp = strtoul(optarg, &end, 10);
      if (tmp == 0 || *end != 0)
	usage("Invalid timeout");
      opt_timeout = tmp;
      break;
    default:
      usage("Unknown option.");
      break;
    }
  }
  if (argc - optind < 2)
    usage("Missing host and port");
  if ((SERVER_FD = tcp_connect(argv[optind], argv[optind+1],
			       opt_timeout)) == -1)
    connfail();
  optind += 2;
  filter_init(argc-optind, argv+optind);
}

int main(int argc, char* argv[])
{
  fd_set fds;
  signal(SIGALRM, SIG_IGN);
  signal(SIGHUP, SIG_IGN);
  signal(SIGPIPE, SIG_IGN);
  parse_args(argc, argv);
  atexit(exitfn);
  pid = getpid();
  for(;;) {
    struct filter_node* filter;
    int maxfd = -1;
    FD_ZERO(&fds);
    for(filter = filters; filter; filter = filter->next) {
      int fd = filter->fd;
      FD_SET(fd, &fds);
      if(fd > maxfd)
	maxfd = fd;
    }
    while(select(maxfd+1, &fds, 0, 0, 0) == -1) {
      if(errno != EINTR)
	usage("select failed!");
    }
    for(filter = filters; filter; filter = filter->next)
      if(FD_ISSET(filter->fd, &fds)) {
	handle_fd(filter);
	break;
      }
  }
}
