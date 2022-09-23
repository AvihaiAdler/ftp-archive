#pragma once

/* returns a stream of file names found in session::context::session_root_dir/session::context::curr_dir/directory_path.
 * if directory_path is NULL (i.e. the client sent LIST followed by a null terminator, directory_path will be set to "."
 * [current directory]) */
int list(void *arg);