#pragma once

/* changes the working directory to the directory_path sepecified. the working directory will be changed to
 * session::context::session_root_dir/directory_path. '..' is not allowed */
int change_directory(void *arg);
