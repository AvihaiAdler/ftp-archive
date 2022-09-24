#pragma once

/* deletes a file. the file is should be in session::context::session_root_dir/session::context::curr_dir/file_path */
int delete_file(void *arg);