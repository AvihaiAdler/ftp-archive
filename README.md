A close multithreaded implementation of a ftp server [rfc959] (https://www.rfc-editor.org/rfc/rfc959). 

### design
The server supports both active mode and passive mode and uses a thread pool to manage tasks as FIFO. There is no login system. Every file uploaded to the server is visible to all users.
The server uses the file system of the hosted environment _however_ it expects a 'mounting point' i.e. a root directory where all uploaded files will be stored. The server won't 'see' past this directory and users can only access said directory and all of its sub directories in other words - so long as the root directory isn't `/` users won't be able to compromise the machine the server runs on. 

The server is linux specific due to its use of the `epoll` interface.

### functionality
The server supports the following commands, all of them can be found in the link above:

| command | description                                              |
| ------- | -------------------------------------------------------- |
| `PWD`   | print working directory                                  |
| `CWD`   | change working directory                                 |
| `MKD`   | make directory                                           |
| `RMD`   | remove directory                                         |
| `PORT`  | [rfc959] (https://www.rfc-editor.org/rfc/rfc959) page 28 |
| `PASV`  | [rfc959] (https://www.rfc-editor.org/rfc/rfc959) page 28 |
| `LIST`  | [rfc959] (https://www.rfc-editor.org/rfc/rfc959) page 32 |
| `DELE`  | delete a file                                            |
| `RETR`  | retrieve a file                                          |
| `STOR`  | store a file                                             |
| `QUIT`  | finish a session                                         |

all commands are case insensitive.

### setup
The server expects a properties file on startup. The properties file is a plain text file consists for key value pairs seperated by a `=`. The following pairs are supported:

| key                   | value                      | description                                                                                                                     | optional |
| --------------------- | -------------------------- | ------------------------------------------------------------------------------------------------------------------------------- | -------- |
| log_file              | path/to/log/file           | a path to a log file. if no such key specified - logs will be outputted to `stdout`                                             | yes      |
| threads_number        | a small unsigned integer   | the number of threads for the server to utilize. if no such key specified the server will use 20 threads                        | yes      |
| control_port          | a (unreserved) port number | the port on which the server will 'listen'                                                                                      | no       |
| data_port             | a (unreserved) port number | the port from which the server will send the data to its clients                                                                | no       |
| connection_queue_size | a small unsigned integer   | the max of idles unhandled connections after which attemps to connect to the server will fail                                   | yes      |
| root_directory        | path/to/root/directory     | the path to the server root directory. see the [design](https://github.com/AvihaiAdler/ftp#design) section for more information | no       |

an example of such file can look as follows:
```
threads_number = 1
control_port = 2020
data_port = 2121
connection_queue_size = 50
root_directory = /home/my_user_name/ftp
```

### building and compiling
The server is written in pure C, and uses CMake as its build tool, as such you need to have CMake installed (version 3.10 and above) and a working compiler (either gcc or clang. other compilers might work as well - however i didn't test them)

- clone the project
- invoke `cmake -S <source directory> -B <build directory> -G <generator>` (e.g. `cmake -S . -B build -G Ninja`. you can use other generators all well or don't use a generator at all. in such case GNU make will be used)
- invoke your generator `ninja -C <build directory>` or `make -C <build directory>` or any other generator you might used.
- the server executable will be placed under `<source directory>/bin` as `ftpd`

### todo
- [ ] integration test
- [ ] replace `sessions` ds from a vector to a tree