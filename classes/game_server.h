#ifndef GAME_SERVER_H
#define GAME_SERVER_H

#include <string> 
#include <vector> 
#include <boost/process.hpp>

#include <boost/filesystem.hpp>
#include <boost/format.hpp>

#include "dedicated_server.h"
#include "typedefs.h"

#define INST_NO_SOURCE 0
#define INST_FROM_LOCREP 1
#define INST_FROM_REMREP 2
#define INST_FROM_STEAM 3

#define INST_FILE 1
#define INST_DIR 2

#if defined(BOOST_POSIX_API)
    #define PROC_SHELL "sh"
    #define SHELL_PREF "-c"
#elif defined(BOOST_WINDOWS_API)
    #define PROC_SHELL "cmd"
    #define SHELL_PREF "/c"
#endif 

namespace GameAP {

class GameServer {
private:
    std::string work_dir;
    std::string screen_name;

    ulong server_id;
    std::string ip;
    uint server_port;
    uint query_port;
    uint rcon_port;

    std::string user;
    std::map<std::string, std::string> aliases;
    
    std::string start_command;
    std::string stop_command;

    std::string game_localrep;
    std::string game_remrep;
    std::string gt_localrep;
    std::string gt_remrep;

    boost::filesystem::path work_path;

    std::string cmd_output;

    int _unpack_archive(boost::filesystem::path const & archive);
    
    bool _copy_dir(
        boost::filesystem::path const & source,
        boost::filesystem::path const & destination
    );

    int _exec(std::string cmd);
    boost::process::child __exec(std::string cmd, boost::process::pipe &out);
    
    void _append_cmd_output(std::string line);
    
public:
    GameServer(ulong mserver_id);
    
    ~GameServer() {
        std::cout << "Game Server Destruct" << std::endl;
    }
    
    int install_game_server();
    int update_server();
    int delete_server();
    int move_game_server();

    void replace_shortcodes(std::string &cmd);
    int start_server();
    int stop_server();

    int get_game_server_load();

    size_t get_cmd_output(std::string * output, size_t position);
};

/* End namespace GameAP */
}

#endif
