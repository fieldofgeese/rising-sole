#pragma once

#include "common.h"
#include "game.h"

typedef enum ServerPacketType {
    SERVER_PACKET_CONNECTED,
    SERVER_PACKET_PEER_CONNECTED,
    SERVER_PACKET_DROPPED,
    SERVER_PACKET_AUTH,
    SERVER_PACKET_PEER_AUTH,
    SERVER_PACKET_PEER_DISCONNECTED,
} ServerPacketType;

typedef enum ClientPacketType {
    CLIENT_PACKET_UPDATE,
} ClientPacketType;

typedef struct __attribute((packed)) ServerBatchHeader {
    u16 num_packets;
    i8 adjustment_amount;
    u8 adjustment_iteration;
} ServerBatchHeader;

typedef struct ServerPacketHeader {
    ServerPacketType type;
} ServerPacketHeader;

typedef struct ClientBatchHeader {
    u64 network_tick;
    u16 num_packets;
    u8 adjustment_iteration;
} ClientBatchHeader;

typedef struct ClientPacketHeader {
    ClientPacketType type;
    u64 simulation_tick;
} ClientPacketHeader;

typedef struct ServerPacketConnected {
    Player player;
    u64 network_tick;
    u8 peer_index;
} ServerPacketConnected;

typedef struct ServerPacketPeerConnected {
    Player player;
    u8 peer_index;
} ServerPacketPeerConnected;

typedef struct ServerPacketAuth {
    Player player;
    u64 simulation_tick;
} ServerPacketAuth;

typedef struct ServerPacketPeerAuth {
    Player player;
    u64 simulation_tick;
    u8 peer_index;
} ServerPacketPeerAuth;

typedef struct ServerPacketPeerDisconnected {
    u8 peer_index;
} ServerPacketPeerDisconnected;

typedef struct ClientPacketUpdate {
    Input input;
} ClientPacketUpdate;
