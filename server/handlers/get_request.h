#pragma once

// should added by main as soon as one of the fds it subscribed to is ready to POLLIN. it should recieve the eventfd as
// well as the thread pool itself. the hadler will recieve() a request and parse it, if the request require the use of a
// data port and there isn't one open - the handler will open it and notify main via eventfd. it'll then add the related
// task to the thread pool. otherwise (there's a data connection open) it'll just add the related task

/* recv() a request, parses it and adds the relative task to the thread pool. if the request requires a data connection
 * - creates one and notify the main thread via eventfd */
int get_request(void *arg);