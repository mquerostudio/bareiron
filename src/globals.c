#include <stdio.h>
#include <stdint.h>
#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
#else
  #include <arpa/inet.h>
#endif
#include <unistd.h>

#include "globals.h"


const BareironPlatform *platform = NULL;

ssize_t recv_count;
uint8_t recv_buffer[MAX_RECV_BUF_LEN] = {0};

uint32_t world_seed = INITIAL_WORLD_SEED;
uint32_t rng_seed = INITIAL_RNG_SEED;

uint16_t world_time = 0;
uint32_t server_ticks = 0;

char motd[] = { "KodeCraft" };
uint8_t motd_len = sizeof(motd) - 1;

#ifdef SEND_BRAND
  char brand[] = { "kodecraft" };
  uint8_t brand_len = sizeof(brand) - 1;
#endif

uint16_t client_count;

BlockChange *block_changes = NULL;
uint32_t block_changes_cap = 0;
int block_changes_count = 0;

PlayerData player_data[MAX_PLAYERS];
int player_data_count = 0;

MobData mob_data[MAX_MOBS];
