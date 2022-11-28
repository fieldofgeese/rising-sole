#include <raylib.h>

#define ENET_IMPLEMENTATION
#include "enet.h"

#include <stdlib.h>
#include <assert.h>

#include "log.h"
#include "common.h"
#include "packet.h"

#define PACKET_LOG_SIZE 2048
#define OUTPUT_BUFFER_SIZE 32000
#define INPUT_BUFFER_LENGTH 16
#define UPDATE_LOG_BUFFER_SIZE 512
#define VALID_TICK_WINDOW 2

typedef struct UpdateLogEntry {
    u64 client_sim_tick;
    u64 server_net_tick;
    ClientPacketUpdate input_update;
} UpdateLogEntry;

typedef struct UpdateLogBuffer {
    UpdateLogEntry data[UPDATE_LOG_BUFFER_SIZE];
    u64 bottom;
    u64 used;
} UpdateLogBuffer;

typedef struct Peer {
    bool connected;
    bool update_processed;
    Player *player;
    UpdateLogBuffer update_log;
    ByteBuffer output_buffer;
    ENetPeer *enet_peer;
} Peer;

static inline void new_packet(Peer *p) {
    ServerBatchHeader *batch = (void *) p->output_buffer.base;
    assert(batch->num_packets < UINT16_MAX);
    ++batch->num_packets;
}

static inline void peer_disconnect(Peer *p) {
    p->player->occupied = false;
    memset(p, 0, sizeof(Peer));
    byte_buffer_free(&p->output_buffer);
}

int main(int argc, char **argv) {
    // Parse command line args
    if (argc != 2) {
        log_error("usage: rs-server [port]");
        return 1;
    }
    const long port = strtol(argv[1], NULL, 10);
    if (errno != 0) {
        log_error("Invalid port: %s", strerror(errno));
        return 1;
    }

    // enet init
    if (enet_initialize() != 0) {
        log_error("An error occurred while initializing ENet");
        return 1;
    }
    ENetAddress address = {
        .host = ENET_HOST_ANY,
        .port = port,
    };
    ENetHost *server = enet_host_create(&address, MAX_CLIENTS, 1, 0, 0);
    if (server == NULL) {
        log_error("Failed to initialize ENet");
        return 1;
    }

    const uint64_t frame_desired = NANOSECONDS(1) / FPS;
    uint64_t frame_delta = 0;
    uint64_t simulation_tick = 0;
    uint64_t network_tick = 0;

    const f32 dt = 1.0f / FPS;

    ENetEvent event = {0};

    Peer peers[MAX_CLIENTS] = {0};
    u8 num_peers = 0;

    Game game = {
        .map = map,
    };

    while (true) {
        //
        // Begin frame
        //
        const uint64_t frame_start = time_current();

        //
        // Networking
        //
        bool run_network_tick = simulation_tick % NET_PER_SIM_TICKS == 0;
        if (run_network_tick) {
            while (enet_host_service(server, &event, 0) > 0) {
                switch (event.type) {
                case ENET_EVENT_TYPE_CONNECT: {
                    char ip[64] = {0};
                    assert(enet_address_get_host_ip_new(&event.peer->address, ip, ARRLEN(ip)) == 0);
                    log_info("client connected: %s:%u", ip, event.peer->address.port);

                    assert(num_peers < MAX_CLIENTS);
                    u8 peer_index = 0;
                    for (; peer_index < MAX_CLIENTS; ++peer_index)
                        if (!peers[peer_index].connected)
                            break;
                    ++num_peers;
                    event.peer->data = malloc(sizeof(u8));
                    *(u8 *)event.peer->data = peer_index;

                    peers[peer_index].connected = true;

                    Player *player = NULL;
                    for (u32 i = 0; i < MAX_CLIENTS; ++i) {
                        player = &game.players[i];
                        if (!player->occupied)
                            break;
                    }

                    assert(player != NULL);
                    player->occupied = true;
                    player->pos = (v2) {0, 0};
                    player->hue = 20.0f;
                    player->health = 100.0f;
                    peers[peer_index].player = player;

                    peers[peer_index].enet_peer = event.peer;
                    peers[peer_index].output_buffer = byte_buffer_alloc(OUTPUT_BUFFER_SIZE);

                    ServerBatchHeader batch = {
                        .num_packets = 0,
                    };
                    APPEND(&peers[peer_index].output_buffer, &batch);

                    // Send greeting for peer
                    {
                        ServerPacketHeader header = {
                            .type = SERVER_PACKET_CONNECTED,
                        };

                        ServerPacketConnected connected = {
                            .player = *player,
                            .network_tick = network_tick,
                            .peer_index = peer_index,
                        };

                        new_packet(&peers[peer_index]);
                        APPEND(&peers[peer_index].output_buffer, &header);
                        APPEND(&peers[peer_index].output_buffer, &connected);
                    }

                    // Send greeting to all other peers
                    {
                        ServerPacketHeader header = {
                            .type = SERVER_PACKET_PEER_CONNECTED,
                        };

                        ServerPacketPeerConnected connected = {
                            .player = *player,
                            .peer_index = peer_index,
                        };

                        for (u8 i = 0; i < MAX_CLIENTS; ++i) {
                            if (!peers[i].connected || i == peer_index)
                                continue;
                            new_packet(&peers[i]);
                            APPEND(&peers[i].output_buffer, &header);
                            APPEND(&peers[i].output_buffer, &connected);
                        }
                    }

                    // Send greeting to this peer about all other peers already connected
                    {
                        ServerPacketHeader header = {
                            .type = SERVER_PACKET_PEER_CONNECTED,
                        };

                        for (u8 i = 0; i < MAX_CLIENTS; ++i) {
                            if (!peers[i].connected || i == peer_index)
                                continue;
                            ServerPacketPeerConnected connected = {
                                .player = *peers[i].player,
                                .peer_index = i,
                            };

                            new_packet(&peers[peer_index]);
                            APPEND(&peers[peer_index].output_buffer, &header);
                            APPEND(&peers[peer_index].output_buffer, &connected);
                        }
                    }
                    break;
                } break;
                case ENET_EVENT_TYPE_RECEIVE: {
                    ByteBuffer input_buffer = byte_buffer_init(event.packet->data, event.packet->dataLength);
                    ClientBatchHeader *batch;
                    POP(&input_buffer, &batch);

                    const u8 peer_index = *(u8 *) event.peer->data;
                    assert(peer_index >= 0 && peer_index < MAX_CLIENTS);
                    assert(peers[peer_index].connected);

                    i8 adjustment = 0;
                    i64 diff = (i64) network_tick + (VALID_TICK_WINDOW-1) - (i64) batch->network_tick;
                    if (diff < INT8_MIN || diff > INT8_MAX) {
                        log_error("network tick diff outside range of adjustment variable!\n");
                        // TODO(anjo): what do?
                        break;
                    }
                    if (diff < -(VALID_TICK_WINDOW-1) || diff > 0) {
                        // Need adjustment
                        adjustment = (i8) diff;
                    }
                    if (batch->network_tick >= network_tick) {
                        if (diff < -(VALID_TICK_WINDOW-1)) {
                            log_info("Allowing packet, too late: net_tick %lu, should be >= %lu", batch->network_tick, network_tick);
                        }
                    } else {
                        ServerPacketHeader header = {
                            .type = SERVER_PACKET_DROPPED,
                        };

                        log_info("Dropping packet, too early: net_tick %lu, should be >= %lu", batch->network_tick, network_tick);

                        new_packet(&peers[peer_index]);
                        APPEND(&peers[peer_index].output_buffer, &header);
                        break;
                    }

                    {
                        ServerBatchHeader *server_batch = (void *) peers[peer_index].output_buffer.base;
                        server_batch->adjustment_amount = adjustment;
                        server_batch->adjustment_iteration = batch->adjustment_iteration;
                    }

                    for (u16 packet = 0; packet < batch->num_packets; ++packet) {
                        ClientPacketHeader *header;
                        POP(&input_buffer, &header);

                        switch (header->type) {
                        case CLIENT_PACKET_UPDATE: {
                            ClientPacketUpdate *input_update;
                            POP(&input_buffer, &input_update);

                            Peer *peer = &peers[peer_index];
                            UpdateLogEntry entry = {
                                .client_sim_tick = header->simulation_tick,
                                .server_net_tick = network_tick,
                                .input_update = *input_update,
                            };
                            CIRCULAR_BUFFER_APPEND(&peer->update_log, entry);
                        } break;
                        default:
                            log_error("Received unknown packet type %d", header->type);
                        }
                    }
                } break;
                case ENET_EVENT_TYPE_DISCONNECT: {
                    u8 peer_index = *(u8 *) event.peer->data;
                    Peer *p = &peers[peer_index];
                    log_info("%d disconnected", peer_index);

                    {
                        ServerPacketHeader header = {
                            .type = SERVER_PACKET_PEER_DISCONNECTED,
                        };

                        ServerPacketPeerDisconnected disc = {
                            .peer_index = peer_index,
                        };

                        for (u8 i = 0; i < MAX_CLIENTS; ++i) {
                            if (!peers[i].connected || i == peer_index)
                                continue;
                            new_packet(&peers[i]);
                            APPEND(&peers[i].output_buffer, &header);
                            APPEND(&peers[i].output_buffer, &disc);
                        }
                    }

                    peer_disconnect(p);
                    --num_peers;
                    free(event.peer->data);
                    event.peer->data = NULL;
                } break;
                case ENET_EVENT_TYPE_DISCONNECT_TIMEOUT: {
                    u8 peer_index = *(u8 *) event.peer->data;
                    Peer *p = &peers[peer_index];
                    log_info("%d disconnected due to timeout.", peer_index);

                    {
                        ServerPacketHeader header = {
                            .type = SERVER_PACKET_PEER_DISCONNECTED,
                        };

                        ServerPacketPeerDisconnected disc = {
                            .peer_index = peer_index,
                        };

                        for (u8 i = 0; i < MAX_CLIENTS; ++i) {
                            if (!peers[i].connected || i == peer_index)
                                continue;
                            new_packet(&peers[i]);
                            APPEND(&peers[i].output_buffer, &header);
                            APPEND(&peers[i].output_buffer, &disc);
                        }
                    }

                    peer_disconnect(p);
                    --num_peers;
                    free(event.peer->data);
                    event.peer->data = NULL;
                } break;
                case ENET_EVENT_TYPE_NONE:
                default:
                    break;
                }
            }
        }

        for (u8 i = 0; i < MAX_CLIENTS; ++i) {
            if (!peers[i].connected)
                continue;
            Peer *peer = &peers[i];
peer_update_log_label:
            if (peer->update_log.used > 0) {
                UpdateLogEntry *entry = &peer->update_log.data[peer->update_log.bottom];
                if (entry->client_sim_tick < simulation_tick) {
                    log_info("%lu, %lu", entry->client_sim_tick, simulation_tick);
                }

                if (entry->client_sim_tick < simulation_tick) {
                    // TODO(anjo): remove goto, I'm lazy
                    //             we somehow ended up with a packet from the past???
                    log_info("Something fucky is amiss!");
                    CIRCULAR_BUFFER_POP(&peer->update_log);
                    goto peer_update_log_label;
                }

                assert(entry->client_sim_tick >= simulation_tick);
                if (entry->client_sim_tick == simulation_tick) {
                    game_update(&game, peer->player, &entry->input_update.input, dt, false);
                    peer->update_processed = true;

                    // Send AUTH packet to peer
                    {
                        ServerPacketHeader header = {
                            .type = SERVER_PACKET_AUTH,
                        };

                        ServerPacketAuth auth = {
                            .simulation_tick = entry->client_sim_tick,
                            .player = *peer->player,
                        };

                        new_packet(peer);
                        APPEND(&peer->output_buffer, &header);
                        APPEND(&peer->output_buffer, &auth);
                    }

                    // Send PEER_AUTH packet to all other peers
                    // TODO(anjo): We are not attaching any adjustment data here
                    {
                        ServerPacketHeader header = {
                            .type = SERVER_PACKET_PEER_AUTH,
                        };

                        ServerPacketPeerAuth peer_auth = {
                            .simulation_tick = entry->client_sim_tick,
                            .player = *peer->player,
                            .peer_index = i,
                        };

                        for (u8 j = 0; j < MAX_CLIENTS; ++j) {
                            if (!peers[j].connected || i == j)
                                continue;
                            new_packet(&peers[j]);
                            APPEND(&peers[j].output_buffer, &header);
                            APPEND(&peers[j].output_buffer, &peer_auth);
                        }
                    }

                    CIRCULAR_BUFFER_POP(&peer->update_log);
                }
            }
        }

        if (run_network_tick) {
            for (u8 i = 0; i < MAX_CLIENTS; ++i) {
                if (!peers[i].connected)
                    continue;
                const size_t size = (intptr_t) peers[i].output_buffer.top - (intptr_t) peers[i].output_buffer.base;
                if (size > sizeof(ServerBatchHeader)) {
                    ENetPacket *packet = enet_packet_create(peers[i].output_buffer.base, size, ENET_PACKET_FLAG_RELIABLE);
                    enet_peer_send(peers[i].enet_peer, 0, packet);
                    peers[i].output_buffer.top = peers[i].output_buffer.base;

                    ServerBatchHeader batch = {
                        .num_packets = 0,
                    };
                    APPEND(&peers[i].output_buffer, &batch);
                }
            }
        }

        for (u8 i = 0; i < MAX_CLIENTS; ++i) {
            if (!peers[i].connected)
                continue;
            if (peers[i].update_processed) {
                peers[i].update_processed = false;
                continue;
            }
            Player *player = peers[i].player;
            Input input = {0};
            game_update(&game, player, &input, dt, false);
        }

        //
        // End frame
        //
        const uint64_t frame_end = time_current();
        assert(frame_end > frame_start);
        frame_delta = frame_end - frame_start;
        if (frame_delta < frame_desired) {
            time_nanosleep(frame_desired - frame_delta);
        }
        if (run_network_tick) {
            ++network_tick;
        }
        ++simulation_tick;
    }

    // ENet shutdown
    enet_host_destroy(server);
    enet_deinitialize();

    return 0;
}
