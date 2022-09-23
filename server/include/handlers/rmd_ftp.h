#pragma once

/* removes an empty directory. the directory path will be caluculated from
 * session::context::session_root_dir/session::context::curr_dir/directory_path */
int remove_directory(void *arg);