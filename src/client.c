#include <raylib.h>

#define ENET_IMPLEMENTATION
#include "enet.h"

#include <stdlib.h>
#include <assert.h>
#include <math.h>

#include "log.h"
#include "common.h"
#include "game.h"
#include "v2.h"
#include "draw.h"
#include "packet.h"
#include "memory/arena.h"

#define UPDATE_LOG_BUFFER_SIZE 512
#define INPUT_BUFFER_LENGTH 512
#define OUTPUT_BUFFER_SIZE 2048

const u64 initial_server_network_tick_offset = 5;

typedef struct Frame {
    uint64_t delta_desired;
    uint64_t delta;
    uint64_t simulation_tick;
    uint64_t network_tick;
    float dt;
} Frame;

static inline Frame frame_init(uint64_t fps) {
    return (Frame) {
        .delta_desired = NANOSECONDS(1) / fps,
        .dt = 1.0f / ((f32) fps)
    };
}

typedef struct Adjustment {
    int8_t   amount;
    uint8_t  iteration;
    int32_t  total;
} Adjustment;

static inline bool is_behind(Adjustment adjustment) {
    return adjustment.amount < 0;
}

static inline bool is_ahead(Adjustment adjustment) {
    return adjustment.amount > 0;
}

typedef struct Network {
    ENetHost *host;
    ENetPeer *peer;
} Network;

static int network_init(Network *net, const char *ip, long port) {
    if (enet_initialize() != 0) {
        log_error("An error occurred while initializing ENet");
        return 1;
    }

    net->host = enet_host_create(NULL, 1, 1, 0, 0);
    if (net->host == NULL) {
        log_error("Failed to initialize ENet");
        return 1;
    }
    ENetAddress address = {0};
    enet_address_set_host(&address, ip);
    address.port = port;

    net->peer = enet_host_connect(net->host, &address, 2, 0);
    if (net->peer == NULL) {
        log_error("No available peers for initializing an ENet connection");
        return 1;
    }

    // Wait 500ms for the connection to succeed
    ENetEvent event = {0};
    if (enet_host_service(net->host, &event, 500) == 0 || event.type != ENET_EVENT_TYPE_CONNECT){
        // Either the time is up and the connection did not succeed,
        // or a disconnect event was received.
        log_error("Connection to %s:%ld failed", ip, port);
        enet_peer_reset(net->peer);
        return 1;
    }

    return 0;
}

static void network_shutdown(Network *net) {
    enet_peer_disconnect(net->peer, 0);
    // Try to disconnect peacefully, letting the server know we've
    // disconnected. Wait 500ms for disconnection to be verified.
    bool disconnected = false;
    ENetEvent event = {0};
    while (enet_host_service(net->host, &event, 500) > 0) {
        switch (event.type) {
        case ENET_EVENT_TYPE_RECEIVE:
            enet_packet_destroy(event.packet);
            break;
        case ENET_EVENT_TYPE_DISCONNECT:
            disconnected = true;
            break;
        default:
            break;
        }
    }
    if (!disconnected) {
        enet_peer_reset(net->peer);
    }
    enet_host_destroy(net->host);
    enet_deinitialize();

    net->host = NULL;
    net->peer = NULL;
}

typedef struct PeerAuthBuffer {
    ServerPacketPeerAuth data[UPDATE_LOG_BUFFER_SIZE];
    u64 bottom;
    u64 used;
} PeerAuthBuffer;

typedef struct Peer {
    bool connected;
    Player *player;
    PeerAuthBuffer auth_buffer;
} Peer;

void handle_input(Player *player, Input *input) {
    if (IsKeyDown(KEY_W))
        input->active[INPUT_MOVE_UP] = true;
    if (IsKeyDown(KEY_A))
        input->active[INPUT_MOVE_LEFT] = true;
    if (IsKeyDown(KEY_S))
        input->active[INPUT_MOVE_DOWN] = true;
    if (IsKeyDown(KEY_D))
        input->active[INPUT_MOVE_RIGHT] = true;
    if (IsKeyDown(KEY_LEFT_SHIFT))
        input->active[INPUT_MOVE_DODGE] = true;
    if (IsMouseButtonDown(MOUSE_BUTTON_LEFT))
        input->active[INPUT_SHOOT] = true;
    if (IsKeyDown(KEY_Q))
        input->active[INPUT_QUIT] = true;

    Vector2 vv = screen_to_world((Vector2) {GetMouseX(), GetMouseY()});
    v2 v = {vv.x, vv.y};

    input->aim = v2sub(v, player->pos);
    if (v2iszero(input->aim))
        input->aim = (v2) {1.0f, 0.0f};
}

static inline void new_packet(ByteBuffer *output_buffer) {
    ClientBatchHeader *batch = (void *) output_buffer->base;
    assert(batch->num_packets < UINT16_MAX);
    ++batch->num_packets;
}

int main(int argc, char **argv) {
    //
    // Parse command line args
    //
    if (argc != 3) {
        log_error("usage: rs-client [ip] [port]");
        return 1;
    }
    const char *ip = argv[1];
    const long port = strtol(argv[2], NULL, 10);
    if (errno != 0) {
        log_error("Invalid port: %s", strerror(errno));
        return 1;
    }

    Network net = {0};
    network_init(&net, ip, port);

    //
    // raylib init
    //
    SetTraceLogLevel(LOG_WARNING);
    InitWindow(200, 160, "rs");
    HideCursor();

    Adjustment adjustment = {0};
    Frame frame = frame_init(60);

    ENetEvent event = {0};

    Game game = {
        .map = map,
    };

    Player *player = &game.players[0];
    player->occupied = true;

    f32 t = 0.0f;

    u8 input_count = 0;
    Input input_buffer[INPUT_BUFFER_LENGTH] = {0};

    Peer peers[MAX_CLIENTS] = {0};
    u8 num_peers = 0;
    u64 main_peer_index = 0;

    ByteBuffer output_buffer = byte_buffer_alloc(OUTPUT_BUFFER_SIZE);
    {
        ClientBatchHeader batch = {0};
        APPEND(&output_buffer, &batch);
    }


    while (!WindowShouldClose()) {
        //
        // Begin frame
        //
        const uint64_t frame_start = time_current();

        //
        // Networking
        //
        bool run_network_tick = frame.simulation_tick % NET_PER_SIM_TICKS == 0;
        bool sleep_this_frame = true;
        if (run_network_tick) {
            // Adjustment when ahead of server
            if (is_ahead(adjustment)) {
                for (int i = 0; i < NET_PER_SIM_TICKS; ++i) {
                    time_nanosleep(frame.delta_desired);
                }
                // NOTE(anjo): I don't remember why this is here.
                //             But it doesn't work without it.
                if (++adjustment.amount == 0) {
                    frame.network_tick++;
                    frame.simulation_tick += NET_PER_SIM_TICKS;
                }
                continue;
            } else if (is_behind(adjustment)) {
                sleep_this_frame = false;
                --adjustment.amount;
            }

            while (enet_host_service(net.host, &event, 0) > 0) {
                switch (event.type) {
                case ENET_EVENT_TYPE_RECEIVE: {
                    const u8 *p = event.packet->data;
                    ServerBatchHeader *batch = extract_struct(&p, ServerBatchHeader);


                    // If the server batch header contains adjustment information, extract it
                    if (batch->adjustment_amount != 0 && adjustment.iteration == batch->adjustment_iteration) {
                        adjustment.amount = batch->adjustment_amount;
                        adjustment.total += adjustment.amount;
                        ++adjustment.iteration;
                    }

                    // Process the batch of packets
                    for (u16 packet = 0; packet < batch->num_packets; ++packet) {
                        ServerPacketHeader *header = extract_struct(&p, ServerPacketHeader);
                        switch (header->type) {
                        case SERVER_PACKET_CONNECTED: {
                            ServerPacketConnected *packet = extract_struct(&p, ServerPacketConnected);
                            frame.network_tick = packet->network_tick + initial_server_network_tick_offset;
                            frame.simulation_tick = frame.network_tick * NET_PER_SIM_TICKS;
                            assert(packet->peer_index >= 0 && packet->peer_index < MAX_CLIENTS);
                            main_peer_index = packet->peer_index;
                            peers[main_peer_index].connected = true;

                            Player *player = NULL;
                            for (u32 i = 0; i < ARRLEN(game.players); ++i) {
                                player = &game.players[i];
                                if (!player->occupied)
                                    break;
                            }

                            assert(player != NULL);
                            *player = packet->player;

                            peers[main_peer_index].player = player;
                            ++num_peers;
                        } break;
                        case SERVER_PACKET_PEER_CONNECTED: {
                            ServerPacketPeerConnected *packet = extract_struct(&p, ServerPacketPeerConnected);

                            const u8 peer_index = packet->peer_index;

                            Player *player = NULL;
                            for (u32 i = 0; i < ARRLEN(game.players); ++i) {
                                player = &game.players[i];
                                if (!player->occupied)
                                    break;
                            }

                            assert(player != NULL);
                            *player = packet->player;

                            peers[peer_index].player = player;
                            ++num_peers;
                        } break;
                        case SERVER_PACKET_AUTH: {
                            ServerPacketAuth *packet = extract_struct(&p, ServerPacketAuth);
                            assert(packet->simulation_tick <= frame.simulation_tick);
                            const u64 diff = frame.simulation_tick - packet->simulation_tick;
                            assert(diff < INPUT_BUFFER_LENGTH);

                            Player *player = peers[main_peer_index].player;

                            Game old_game = game;
                            Player old_player = packet->player;

                            u8 old_index = (input_count + INPUT_BUFFER_LENGTH - diff) % INPUT_BUFFER_LENGTH;
                            for (; old_index != input_count; old_index = (old_index + 1) % INPUT_BUFFER_LENGTH) {
                                Input *old_input = &input_buffer[old_index];
                                game_update(&old_game, &old_player, old_input, frame.dt, true);
                            }

                            if (!v2equal(player->pos, old_player.pos)) {
                                log_error("  Server disagreed! {%f, %f} vs {%f, %f}", player->pos.x, player->pos.y, old_player.pos.x, old_player.pos.y);
                                *player = packet->player;
                            }
                        } break;
                        case SERVER_PACKET_PEER_AUTH: {
                            ServerPacketPeerAuth *packet = extract_struct(&p, ServerPacketPeerAuth);
                            const u8 peer_index = packet->peer_index;
                            assert(packet->peer_index >= 0 && packet->peer_index < MAX_CLIENTS);
                            Peer *peer = &peers[peer_index];
                            *peer->player = packet->player;
                        } break;
                        case SERVER_PACKET_PEER_DISCONNECTED: {
                            ServerPacketPeerDisconnected *packet = extract_struct(&p, ServerPacketPeerDisconnected);
                            const u8 peer_index = packet->peer_index;
                            assert(packet->peer_index >= 0 && packet->peer_index < MAX_CLIENTS);
                            Peer *peer = &peers[peer_index];
                            memset(peer->player, 0, sizeof(Player));
                            --num_peers;
                        } break;
                        default:
                            log_error("Received unknown packet type %d", header->type);
                        }
                    }

                    enet_packet_destroy(event.packet);
                } break;
                case ENET_EVENT_TYPE_DISCONNECT_TIMEOUT:
                case ENET_EVENT_TYPE_DISCONNECT: {
                    log_info("Server disconnected");
                } break;
                case ENET_EVENT_TYPE_CONNECT:
                case ENET_EVENT_TYPE_NONE:
                    break;
                }
            }
        }

        BeginDrawing();
        ClearBackground(RAYWHITE);

        //
        // Game update
        //

        draw_game(&game, t);

        Input *input = &input_buffer[input_count];
        Player *player = peers[main_peer_index].player;
        if (player != NULL) {
            input_count = (input_count + 1) % INPUT_BUFFER_LENGTH;
            memset(input->active, INPUT_NULL, sizeof(input->active));
            handle_input(player, input);

            ClientPacketHeader header = {
                .type = CLIENT_PACKET_UPDATE,
                .simulation_tick = frame.simulation_tick,
            };

            ClientPacketUpdate update = {
                .input = *input,
            };

            new_packet(&output_buffer);
            APPEND(&output_buffer, &header);
            APPEND(&output_buffer, &update);

            game_update(&game, player, input, frame.dt, false);
        }

        const float fps = 1.0f / ((float) frame.delta / (float) NANOSECONDS(1));
        if (!isinf(fps)) {
            DrawText(TextFormat("fps: %.0f", fps), 10, 30, 20, GRAY);
        }

        EndDrawing();

        if (run_network_tick) {
            const size_t size = (intptr_t) output_buffer.top - (intptr_t) output_buffer.base;
            if (size > sizeof(ClientBatchHeader)) {
                ClientBatchHeader *batch = (void *) output_buffer.base;
                batch->network_tick = frame.network_tick;
                batch->adjustment_iteration = adjustment.iteration;
                ENetPacket *packet = enet_packet_create(output_buffer.base, size, ENET_PACKET_FLAG_RELIABLE);
                enet_peer_send(net.peer, 0, packet);
                output_buffer.top = output_buffer.base;

                {
                    ClientBatchHeader batch = {0};
                    APPEND(&output_buffer, &batch);
                }
            }
        }

        //
        // End frame
        //
        if (sleep_this_frame) {
            const uint64_t frame_end = time_current();
            assert(frame_end > frame_start);
            frame.delta = frame_end - frame_start;
            if (frame.delta < frame.delta_desired) {
                time_nanosleep(frame.delta_desired - frame.delta);
            }
        }

        if (run_network_tick) {
            ++frame.network_tick;
        }
        ++frame.simulation_tick;
        t += frame.dt;
    }

    //
    // raylib shutdown
    //
    CloseWindow();

    network_shutdown(&net);

    return 0;
}
