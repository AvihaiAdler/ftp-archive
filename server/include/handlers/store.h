#pragma once

/* requests a file to be stored. the file will be stored in
 * session::context::session_root_dir/session::context::curr_dir/file_path */
int store_file(void *arg);