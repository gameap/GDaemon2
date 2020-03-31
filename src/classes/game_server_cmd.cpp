#include "classes/game_server_cmd.h"

#include "functions/gstring.h"
#include "functions/gsystem.h"

#include "classes/game_server_installer.h"
#include "classes/dedicated_server.h"

#include "log.h"

using namespace GameAP;

void GameServerCmd::execute()
{
    switch (this->m_command) {
        case START:
            this->m_result = this->start();
            break;

        case STOP:
        case KILL:
            this->m_result = this->stop();
            break;

        case RESTART:
            this->m_result = this->restart();
            break;
        case UPDATE:
        case INSTALL:
            this->m_result = this->update();
            break;

        case REINSTALL:
            this->m_result =
                    this->remove() && this->update();
            break;

        case DELETE:
            this->m_result = this->remove();
            break;
    }

    this->m_complete = true;
}

bool GameServerCmd::is_complete() const
{
    return this->m_complete;
}

bool GameServerCmd::result() const
{
    return this->m_result;
}

void GameServerCmd::output(std::string *str_out)
{
    return this->m_output->get(str_out);
}

bool GameServerCmd::start()
{
    DedicatedServer& ds = DedicatedServer::getInstance();

    std::string command = str_replace(
            "{command}",
            this->m_server.start_command,
            ds.get_script_cmd(DS_SCRIPT_START)
    );

    this->replace_shortcodes(command);

    int result = this->cmd_exec(command);
    return (result == EXIT_SUCCESS_CODE);
}

bool GameServerCmd::status()
{
    DedicatedServer& ds = DedicatedServer::getInstance();
    std::string command  = ds.get_script_cmd(DS_SCRIPT_STATUS);
    this->replace_shortcodes(command);

    bool is_active = false;

    if (command.length() > 0) {
        int result = this->cmd_exec(command);
        is_active = (result == EXIT_SUCCESS_CODE);
    } else {
        fs::path work_path = ds.get_work_path();
        work_path /= this->m_server.dir;

        fs::path p(work_path);
        p /= "pid.txt";

        char bufread[32];

        std::ifstream pidfile;
        pidfile.open(p.string(), std::ios::in);

        if (pidfile.good()) {
            pidfile.getline(bufread, 32);
            pidfile.close();

            uint pid = static_cast<uint>(atol(bufread));

            if (pid != 0) {
                #ifdef __linux__
                    is_active = (kill(pid, 0) == 0);
                #elif _WIN32
                    HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, pid);
                    DWORD ret = WaitForSingleObject(process, 0);
                    CloseHandle(process);
                    is_active = (ret == WAIT_TIMEOUT);
                #endif
            }
        } else {
            is_active = false;
        }
    }

    return is_active;
}

bool GameServerCmd::stop()
{
    DedicatedServer& ds = DedicatedServer::getInstance();
    std::string command  = ds.get_script_cmd(DS_SCRIPT_STOP);

    this->replace_shortcodes(command);

    int result = this->cmd_exec(command);
    return result == EXIT_SUCCESS_CODE;
}

bool GameServerCmd::restart()
{
    return
        this->stop() && this->start();
}

bool GameServerCmd::update()
{
    if (this->status() && ! this->stop()) {
        // TODO: Info
        return false;
    }

    DedicatedServer& ds = DedicatedServer::getInstance();
    GameServerInstaller installer(this->m_output);

    installer.m_game_localrep        = this->m_server.game.local_repository;
    installer.m_game_remrep          = this->m_server.game.remote_repository;

    installer.m_mod_localrep         = this->m_server.game_mod.local_repository;
    installer.m_mod_remrep           = this->m_server.game_mod.remote_repository;

    installer.m_steam_app_id         = std::to_string(this->m_server.game.steam_app_id);
    installer.m_steam_app_set_config = this->m_server.game.steam_app_set_config;

    fs::path work_path = ds.get_work_path();
    work_path /= this->m_server.dir;

    installer.m_server_absolute_path = work_path;
    installer.m_user                 = this->m_server.user;

    int result = installer.install_server();
    return (result == EXIT_SUCCESS_CODE);
}

bool GameServerCmd::remove()
{
    if (!this->stop()) {
        return false;
    }

    DedicatedServer& ds = DedicatedServer::getInstance();
    std::string command  = ds.get_script_cmd(DS_SCRIPT_DELETE);

    if (command.length() > 0) {
        this->replace_shortcodes(command);
        int result = this->cmd_exec(command);
        return (result == EXIT_SUCCESS_CODE);
    } else {
        fs::path work_path = ds.get_work_path();
        work_path /= this->m_server.dir;

        try {
            GAMEAP_LOG_DEBUG << "Remove path: " << work_path;
            fs::remove_all(work_path);
        }
        catch (fs::filesystem_error &e) {
            // _error("Unable to remove: " + std::string(e.what()));
            return false;
        }
    }

    return true;
}

int GameServerCmd::cmd_exec(const std::string &command)
{
    this->m_output->append(fs::current_path().string() + "# " + command);

    int exit_code = exec(command, [this](std::string line) {
        this->m_output->append(line);
    });

    return exit_code;
}

void GameServerCmd::replace_shortcodes(std::string &command)
{
    DedicatedServer& dedicatedServer = DedicatedServer::getInstance();

    fs::path work_path = dedicatedServer.get_work_path();
    work_path /= this->m_server.dir;

    command = str_replace("{dir}", work_path.string(), command);

    command = str_replace("{uuid}", this->m_server.uuid, command);
    command = str_replace("{uuid_short}", this->m_server.uuid_short, command);

    command = str_replace("{host}", this->m_server.ip, command);
    command = str_replace("{ip}", this->m_server.ip, command);
    command = str_replace("{game}", this->m_server.game.start_code, command);

    command = str_replace("{id}", std::to_string(this->m_server_id), command);
    command = str_replace("{port}", std::to_string(this->m_server.connect_port), command);
    command = str_replace("{query_port}", std::to_string(this->m_server.query_port), command);
    command = str_replace("{rcon_port}", std::to_string(this->m_server.rcon_port), command);

    command = str_replace("{user}", this->m_server.user, command);

    for (const auto& var: this->m_server.vars) {
        command = str_replace("{" + var.first + "}", var.second, command);
    }
}