#include <sys/types.h>
#include "ucspi-proxy.h"

const char* filter_name = "ucspi-proxy";

void filter_init(int argc, char** argv)
{
  if(argc > 0)
    usage("Too many arguments.");
  add_filter(CLIENT_IN, write_server, 0);
  add_filter(SERVER_IN, write_client, 0);
}

void filter_deinit(void)
{
}
