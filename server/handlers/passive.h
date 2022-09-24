#pragma once

/* sets a passive data port on the server the client will have to connect to before it wishes to get any data. this will
 * cause the server to send a reply with the new port number (in ASCII encouding) to the client */
int passive(void *arg);