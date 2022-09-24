#pragma once

/* changes the data port number the server will connect() to. the request should looks like PORT ip:port (which is
 * different from the ftp sepcs), all in ASCII ecouding */
int port(void *arg);