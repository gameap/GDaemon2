#ifndef GDAEMON_SERVER_H
#define GDAEMON_SERVER_H

#include <map>

#include "models/game.h"
#include "models/game_mod.h"

namespace GameAP {
    struct Server {
        unsigned int id;
        bool enabled;
        bool installed;
        bool blocked;
        std::string name;
        std::string uuid;
        std::string uuid_short;
        Game game;
        GameMod game_mod;
        std::string ip;
        unsigned short connect_port;
        unsigned short query_port;
        unsigned short rcon_port;
        std::string dir;
        std::string user;

        std::string start_command;
        std::string stop_command;
        std::string force_stop_command;
        std::string restart_command;

        bool process_active;
        std::time_t last_process_check;

        std::map<std::string, std::string> vars;
    };
}

#endif //GDAEMON_SERVER_H
