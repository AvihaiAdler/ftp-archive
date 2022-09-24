#pragma once

/* returns the current working directory which is calculated from
 * session::context::session_root_dir/session::context::curr_dir. the reply will be sent over the control connection */
int print_working_directory(void *arg);