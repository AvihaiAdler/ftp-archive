#pragma once

/* requests a file to be sent. file path is calculated as
 * session::context::session_root_dir/session::context::curr_dir/file_path */
int retrieve_file(void *arg);