#pragma once

// magic numbers are the hashes of these strings taken from the hash commands in hash_table.c
enum command_hash {
  QUIT = 0x7c9d0608,
  RETR = 0x7c9d4fc2,
  STOR = 0x7c9e1b4d,
  APPE = 0x7c942b8b,
  DELE = 0x7c95a15f,
  LIST = 0x7c9a1661,
  PORT = 0x7c9c614a,
  PASV = 0x7c9c25df
};