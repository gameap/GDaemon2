#include "consts.h"

#include "config.h"
#include "db/db.h"
#include "dedicated_server.h"

#include <boost/format.hpp>

#include "functions/restapi.h"

#ifdef _WIN32
    #include <windows.h>
    #include <stdio.h>
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <iphlpapi.h>

    #define MALLOC(x) HeapAlloc(GetProcessHeap(), 0, (x))
    #define FREE(x) HeapFree(GetProcessHeap(), 0, (x))

    #ifdef _MSC_VER
		#pragma comment(lib, "iphlpapi.lib")
	#endif
#endif // _WIN32

using namespace GameAP;
namespace fs = boost::filesystem;

DedicatedServer::DedicatedServer()
{
    Config& config = Config::getInstance();

    ds_id = config.ds_id;

    stats_update_period = config.stats_update_period;
    db_update_period    = config.stats_db_update_period;

    last_cpustat_time = 0;
    last_ifstat_time = 0;

    std::cout << "ds_id: " << ds_id << std::endl;
    std::cout << "stats_update_period: " << stats_update_period << std::endl;
    std::cout << "db_update_period: " << db_update_period << std::endl;

	// get cpu count
    cpu_count = boost::thread::hardware_concurrency();

	#ifdef _WIN32
        MEMORYSTATUSEX statex;
        statex.dwLength = sizeof (statex);
        GlobalMemoryStatusEx (&statex);

        ram_total = statex.ullTotalPhys/1024; // Kb

        // Check interfaces
        interfaces.push_back("all");
	#elif __linux__

        struct sysinfo sysi;

        ram_total = sysi.totalram/1024; // kB

        // Check interfaces
        for (std::vector<std::string>::iterator it = config.if_list.begin(); it != config.if_list.end(); ++it) {
            if (fs::is_directory(boost::str(boost::format("/sys/class/net/%s") % *it))) {
                interfaces.push_back(*it);
            }
        }
	#endif

    std::map<std::string, netstats> ifstats;
    get_net_load(ifstats);

	std::vector<float> cpu_percent;
    get_cpu_load(cpu_percent);

	fs::space_info spi;

    // check filesystem
    for (auto it = config.drives_list.begin(); it != config.drives_list.end(); ++it) {
        try {
            spi = fs::space(*it);
            drv_space[*it] = spi.capacity;
            std::cout << "space capacity [" << *it << "]: " << drv_space[*it] << std::endl;
            drives.push_back(*it);
        } catch (fs::filesystem_error &e) {
            std::cerr << "error get space: " << e.what() << std::endl;
        }
    }

    last_stats_update = time(nullptr);

    std::cout << "Getting Dedicated server init data" << std::endl;

    try {
        Json::Value jvalue = Gameap::Rest::get("/gdaemon_api/dedicated_servers/get_init_data/" + std::to_string(ds_id));

        // TODO: Check work path!

        work_path = jvalue["work_path"].asString();

        script_install = jvalue["script_install"].asString();
        script_reinstall = jvalue["script_reinstall"].asString();
        script_update = jvalue["script_update"].asString();
        script_start = jvalue["script_start"].asString();
        script_pause = jvalue["script_pause"].asString();
        script_stop = jvalue["script_stop"].asString();
        script_kill = jvalue["script_kill"].asString();
        script_restart = jvalue["script_restart"].asString();
        script_status = jvalue["script_status"].asString();
        script_get_console = jvalue["script_get_console"].asString();
        script_send_command = jvalue["script_send_command"].asString();
    } catch (Gameap::Rest::RestapiException &exception) {
        // Try later
        std::cerr << exception.what() << std::endl;
    }
}

// ---------------------------------------------------------------------

int DedicatedServer::stats_process()
{
    if (time(0) - last_stats_update < stats_update_period) {
        return -1;
    }

    ds_stats cur_stats;
    stats.reserve(1);
    cur_stats.cpu_load.reserve(cpu_count);

    cur_stats.time = time(nullptr);

    #ifdef _WIN32
        cur_stats.loa[0] = 0; cur_stats.loa[1] = 0; cur_stats.loa[2] = 0;

        // Get cpu load
        get_cpu_load(cur_stats.cpu_load);

        // Get ram load
        MEMORYSTATUSEX statex;
        statex.dwLength = sizeof (statex);
        GlobalMemoryStatusEx (&statex);

        cur_stats.ram_us = (statex.ullTotalPhys - statex.ullAvailPhys)/1024;
        cur_stats.ram_cache = 0;

        // Get if stat
        get_net_load(cur_stats.ifstats);
    #elif __linux__
        getloadavg(cur_stats.loa, 3);

        // Get cpu load
        get_cpu_load(cur_stats.cpu_load);

        // Cached ram
        char buf[1024];
        std::ifstream meminfo;
        meminfo.open("/proc/meminfo", std::ios::in);
        meminfo.read(buf, 1024);

        std::vector<std::string> split_lines;
        boost::split(split_lines, buf, boost::is_any_of("\n\r"));

        std::vector<std::string> split_spaces;

        // ulong cached_mem = 0;
        ushort itm_count = 0;
        uintmax_t ram_free = 0;
        for (std::vector<std::string>::iterator itl = split_lines.begin(); itl != split_lines.end(); ++itl) {
            boost::split(split_spaces, *itl, boost::is_any_of(" "));
            // if (split_spaces[0] != "Cached:") {
                // continue;
            // }

            if (split_spaces[0] == "MemTotal:") {
                for (std::vector<std::string>::iterator its = split_spaces.begin()+1; its != split_spaces.end(); ++its) {
                    if (*its == "") continue;

                    ram_total = strtoull((*its).c_str(), NULL, 10);
                    itm_count++;
                    break;
                }
            }
            
            if (split_spaces[0] == "MemFree:") {
                for (std::vector<std::string>::iterator its = split_spaces.begin()+1; its != split_spaces.end(); ++its) {
                    if (*its == "") continue;

                    ram_free = strtoull((*its).c_str(), NULL, 10);
                    itm_count++;
                    break;
                }
            }

            if (split_spaces[0] == "Cached:") {
                for (std::vector<std::string>::iterator its = split_spaces.begin()+1; its != split_spaces.end(); ++its) {
                    if (*its == "") continue;

                    cur_stats.ram_cache = strtoull((*its).c_str(), NULL, 10);
                    itm_count++;
                    break;
                }
            }

            if (itm_count >= 3) {
                break;
            }
        }

        cur_stats.ram_us = (ram_total - ram_free); // kB
        meminfo.close();

        // Get if stat
        get_net_load(cur_stats.ifstats);
	#endif

	// Get drive space
    fs::space_info spi;
    for (std::vector<std::string>::iterator it = drives.begin(); it != drives.end(); ++it) {
        spi = fs::space(*it);
        cur_stats.drv_us_space[*it] = spi.capacity-spi.free;
        cur_stats.drv_free_space[*it] = spi.free;
    }

	stats.insert(stats.end(), cur_stats);

    // std::cout
        // << "cur_stats.loa: " << cur_stats.loa[0] << " " << cur_stats.loa[1] << " " << cur_stats.loa[2] << std::endl
        // << "cur_stats.cpu_load: " << cur_stats.cpu_load << std::endl
        // << "cur_stats.ram_us: " << cur_stats.ram_us << std::endl
        // << "cur_stats.drv_us_space[\"/\"]: " << cur_stats.drv_us_space["/"] << std::endl
        // << "cur_stats.drv_free_space[\"/\"]: " << cur_stats.drv_free_space["/"] << std::endl
        // << "cur_stats.ifstats[lo].rxb: " << cur_stats.ifstats["lo"].rxb << std::endl
        // << "cur_stats.ifstats[lo].txb: " << cur_stats.ifstats["lo"].txb << std::endl
        // << "cur_stats.ifstats[lo].rxp: " << cur_stats.ifstats["lo"].txp << std::endl
        // << "cur_stats.ifstats[lo].txp: " << cur_stats.ifstats["lo"].txp << std::endl
    // << std::endl;

    last_stats_update = time(nullptr);

    return 0;
}

// ---------------------------------------------------------------------

int DedicatedServer::get_net_load(std::map<std::string, netstats> &ifstats)
{
    // Get current tx rx
    #ifdef _WIN32
        char bufread[1024];
    #elif __linux__
        char bufread[32];
    #endif
    std::map<std::string, netstats> current_ifstats;

    #ifdef _WIN32
        /*DWORD dwRetval;
        MIB_IPSTATS *pStats;

        pStats = (MIB_IPSTATS *) MALLOC(sizeof (MIB_IPSTATS));
        if (pStats == NULL) {
            std::cerr << "Allocate memory error (get_net_load)" << std::endl;
            return -1;
        }
        dwRetval = GetIpStatistics(pStats);
        if (dwRetval != NO_ERROR) {
            std::cerr << "GetIpStatistics error: " << dwRetval << std::endl;
            return -1;
        }

        std::cout << "pStats->dwInReceives: " << pStats->dwInReceives << std::endl;
        std::cout << "pStats->dwOutRequests: " << pStats->dwOutRequests << std::endl;
        */

        std::string netstat_result;
        FILE *netstat;
        netstat = popen("netstat -e", "r");

        while(fgets(bufread, sizeof(bufread), netstat)!=NULL){
            netstat_result += bufread;
        }
        pclose(netstat);

        std::vector<std::string> split_lines;
        boost::split(split_lines, netstat_result, boost::is_any_of("\n\r"));
        // std::cout << "split_lines size: " << split_lines.size() << std::endl;

        if (split_lines.size() != 11) {
            return -1;
        }

        std::vector<std::string> split_spaces;
        ushort i;

        boost::split(split_spaces, split_lines[4], boost::is_any_of(" "));

        i = 0;
        for (std::vector<std::string>::iterator its = split_spaces.begin()+1; its != split_spaces.end(); ++its) {
            if (*its == "") continue;
            if (i > 1) break;

            if (i == 0) current_ifstats["all"].rxb = strtoull((*its).c_str(), NULL, 10);
            else current_ifstats["all"].txb = strtoull((*its).c_str(), NULL, 10);
            i++;
        }

        boost::split(split_spaces, split_lines[5], boost::is_any_of(" "));

        i = 0;
        for (std::vector<std::string>::iterator its = split_spaces.begin()+2; its != split_spaces.end(); ++its) {
            if (*its == "") continue;
            if (i > 1) break;

            if (i == 0) current_ifstats["all"].rxp = strtoull((*its).c_str(), NULL, 10);
            else current_ifstats["all"].txp = strtoull((*its).c_str(), NULL, 10);
            i++;
        }

        boost::split(split_spaces, split_lines[6], boost::is_any_of(" "));

        i = 0;
        for (std::vector<std::string>::iterator its = split_spaces.begin()+2; its != split_spaces.end(); ++its) {
            if (*its == "") continue;
            if (i > 1) break;

            if (i == 0) current_ifstats["all"].rxp += strtoull((*its).c_str(), NULL, 10);
            else current_ifstats["all"].txp += strtoull((*its).c_str(), NULL, 10);
            i++;
        }

        // std::cout << "current_ifstats[\"all\"].rxb: " << current_ifstats["all"].rxb << std::endl;
        // std::cout << "current_ifstats[\"all\"].txb: " << current_ifstats["all"].txb << std::endl;
        // std::cout << "current_ifstats[\"all\"].rxp: " << current_ifstats["all"].rxp << std::endl;
        // std::cout << "current_ifstats[\"all\"].txp: " << current_ifstats["all"].txp << std::endl;
    #elif __linux__
        for (std::vector<std::string>::iterator it = interfaces.begin(); it != interfaces.end(); ++it) {
            std::ifstream netstats;
            netstats.open(boost::str(boost::format("/sys/class/net/%s/statistics/rx_bytes") % *it), std::ios::in);
            netstats.getline(bufread, 32);
            current_ifstats[*it].rxb = strtoull(bufread, NULL, 10);
            netstats.close();

            netstats.open(boost::str(boost::format("/sys/class/net/%s/statistics/tx_bytes") % *it), std::ios::in);
            netstats.getline(bufread, 32);
            current_ifstats[*it].txb = strtoull(bufread, NULL, 10);
            netstats.close();

            netstats.open(boost::str(boost::format("/sys/class/net/%s/statistics/rx_packets") % *it), std::ios::in);
            netstats.getline(bufread, 32);
            current_ifstats[*it].rxp = strtoull(bufread, NULL, 10);
            netstats.close();

            netstats.open(boost::str(boost::format("/sys/class/net/%s/statistics/tx_packets") % *it), std::ios::in);
            netstats.getline(bufread, 32);
            current_ifstats[*it].txp = strtoull(bufread, NULL, 10);
            netstats.close();
        }
	#endif

    time_t current_time = time(nullptr);

    if (last_ifstat_time != 0 && current_time > last_ifstat_time) {

        int time_diff = current_time - last_ifstat_time;

        for (std::vector<std::string>::iterator it = interfaces.begin(); it != interfaces.end(); ++it) {
            if (current_ifstats.count(*it) > 0  && last_ifstats.count(*it) > 0) {

                ifstats[*it].rxb = (current_ifstats[*it].rxb - last_ifstats[*it].rxb) / time_diff;
                ifstats[*it].txb = (current_ifstats[*it].txb - last_ifstats[*it].txb) / time_diff;
                ifstats[*it].rxp = (current_ifstats[*it].rxp - last_ifstats[*it].rxp) / time_diff;
                ifstats[*it].txp = (current_ifstats[*it].txp - last_ifstats[*it].txp) / time_diff;

                // std::cout << "last_ifstats[*it].rx: " << last_ifstats[*it].rxb << std::endl;
                // std::cout << "current_ifstats[*it].rxb: " << current_ifstats[*it].rxb << std::endl;
                // std::cout << "ifstats[*it].rxb: " << ifstats[*it].rxb << std::endl;
                // std::cout << "ifstats[*it].txb: " << ifstats[*it].txb << std::endl;
                // std::cout << "ifstats[*it].rxp: " << ifstats[*it].rxp << std::endl;
                // std::cout << "ifstats[*it].txp: " << ifstats[*it].txp << std::endl;
            }
        }
    }

    last_ifstats        = current_ifstats;
    last_ifstat_time    = time(nullptr);

    return 0;
}

// ---------------------------------------------------------------------

int DedicatedServer::get_ping(ushort &ping)
{
    ping = 0;
}

// ---------------------------------------------------------------------

int DedicatedServer::get_cpu_load(std::vector<float> &cpu_percent)
{
    std::ifstream cpustat;

    std::map<ushort, ulong> oldcpuload[4];
    std::map<ushort, ulong> cpuload[4];

    for (int i = 0; i < 4; i++) {
        oldcpuload[i] = last_cpustat[i];
        last_cpustat[i].clear();
    }

    ushort readsize = 128 * cpu_count;
    char buf[10240];

    time_t cpustat_time = time(nullptr);

    #ifdef _WIN32
        std::string wmic_result;
        FILE *wmic;
        wmic = popen("wmic cpu get LoadPercentage", "r");

         while(fgets(buf, sizeof(buf), wmic)!=NULL){
            wmic_result += buf;
        }
        pclose(wmic);

        std::vector<std::string> split_lines;
        boost::split(split_lines, wmic_result, boost::is_any_of("\n\r"));

        ushort cpuid = 0;
        for (std::vector<std::string>::iterator itl = split_lines.begin()+1; itl != split_lines.end(); ++itl) {
            if (cpuid+1 > cpu_count) break;
            if (*itl == "") continue;

            cpu_percent.push_back((float)atof((*itl).c_str()));
            cpuid++;
        }
    #elif __linux__
        cpustat.open("/proc/stat", std::ios::in);

        cpustat.read(buf, (std::streamsize)readsize);

        std::vector<std::string> split_lines;
        split_lines.reserve(cpu_count+8);

        std::vector<std::string> split_spaces;
        split_spaces.reserve(11);

        boost::split(split_lines, buf, boost::is_any_of("\n\r"));

        ushort cpuid = 0;
        for (std::vector<std::string>::iterator itl = split_lines.begin()+1; itl != split_lines.end(); ++itl) {
            if (cpuid > cpu_count) break;
            // Line must begin only "cpu  ..."
            if (itl[0][0] != 'c') break;

            // std::cout << *itl << std::endl;

            boost::split(split_spaces, *itl, boost::is_any_of("  "));
            cpuload[0][cpuid] = strtoul(&split_spaces[1][0], NULL, 10); // User
            cpuload[1][cpuid] = strtoul(&split_spaces[2][0], NULL, 10); // Nice
            cpuload[2][cpuid] = strtoul(&split_spaces[3][0], NULL, 10); // System
            cpuload[3][cpuid] = strtoul(&split_spaces[4][0], NULL, 10); // Idle

            cpuid++;
            split_spaces.clear();
        }

        std::map<ushort, ulong> loaddiff[4];
        std::vector<float> perc;
        perc.reserve(cpu_count);

        if (last_cpustat_time != 0) {

            float time_between = cpustat_time - last_cpustat_time;

            for (int i = 0; i < cpu_count; i++) {
                loaddiff[0][i] = cpuload[0][i] - oldcpuload[0][i]; // User
                loaddiff[1][i] = cpuload[1][i] - oldcpuload[1][i]; // Nice
                loaddiff[2][i] = cpuload[2][i] - oldcpuload[2][i]; // System
                loaddiff[3][i] = cpuload[3][i] - oldcpuload[3][i]; // Idle

                last_cpustat[0][i] = cpuload[0][i];
                last_cpustat[1][i] = cpuload[1][i];
                last_cpustat[2][i] = cpuload[2][i];
                last_cpustat[3][i] = cpuload[3][i];

                // std::cout << "cpuload[0][i]" << cpuload[0][i] << std::endl;
                // std::cout << "cpuload[1][i]" << cpuload[1][i] << std::endl;
                // std::cout << "cpuload[2][i]" << cpuload[2][i] << std::endl;
                // std::cout << "cpuload[3][i]" << cpuload[3][i] << std::endl;

                // std::cout << "oldcpuload[0][i]" << oldcpuload[0][i] << std::endl;
                // std::cout << "oldcpuload[1][i]" << oldcpuload[1][i] << std::endl;
                // std::cout << "oldcpuload[2][i]" << oldcpuload[2][i] << std::endl;
                // std::cout << "oldcpuload[3][i]" << oldcpuload[3][i] << std::endl;

                // std::cout << "loaddiff[0][i]" << loaddiff[0][i] << std::endl;
                // std::cout << "loaddiff[1][i]" << loaddiff[1][i] << std::endl;
                // std::cout << "loaddiff[2][i]" << loaddiff[2][i] << std::endl;
                // std::cout << "loaddiff[3][i]" << loaddiff[3][i] << std::endl;

                if ((loaddiff[0][i] != 0 || loaddiff[1][i] != 0 || loaddiff[2][i] != 0 || loaddiff[3][i] != 0) && time_between >= 1) {
                    try {
                        perc[i] = (
                            ((loaddiff[0][i] + loaddiff[1][i] + loaddiff[2][i]) * (time_between/100)) /
                            ((loaddiff[0][i] + loaddiff[1][i] + loaddiff[2][i] + loaddiff[3][i]) * (time_between/100))
                        ) * 100;
                    }
                    catch (std::logic_error &e) {
                        std::cout << "CPU Load calculation error: " << e.what() << std::endl;
                    }

                } else {
                    perc[i] = 0;
                }

                cpu_percent.push_back(perc[i]);
                // std::cout << "CPU #" << i << ": " << perc[i] << std::endl;
            }

            // cpu_percent = &perc;
            // std::cout << "perc[0]: " << perc[0] << std::endl;
            // std::cout << "cpu_percent[0]: " << (*cpu_percent)[0] << std::endl;

            last_cpustat_time = cpustat_time;
            return 0;
        } else {
            for (int i = 0; i < cpu_count; i++) {
                last_cpustat[0][i] = cpuload[0][i];
                last_cpustat[1][i] = cpuload[1][i];
                last_cpustat[2][i] = cpuload[2][i];
                last_cpustat[3][i] = cpuload[3][i];
            }

            last_cpustat_time = cpustat_time;
            return -1;
        }
	#endif

	return 0;
}

int DedicatedServer::update_db()
{
    if (time(0) - last_db_update < db_update_period) {
        return -1;
    }

    std::vector<std::vector<ds_stats>::iterator>insert_complete;

    for (std::vector<ds_stats>::iterator it = stats.begin();
        it != stats.end();
        ++it
    ) {
        ushort ping = 0;

        // Load average
        std::string loa = boost::str(boost::format("%.2f %.2f %.2f") % (*it).loa[0] % (*it).loa[1] % (*it).loa[2]);

        // Ram
        std::string ram = boost::str(boost::format("%1% %2% %3%") % (*it).ram_us % (*it).ram_cache % ram_total);

        // Cpu
        std::string cpu = "";
        for (int i = 0; i < (*it).cpu_load.size(); ++i) {
            // std::cout << "CPU #" << i << " " << (*it).cpu_load[i] << std::endl;
            cpu +=  boost::str(boost::format("%.2f") % (*it).cpu_load[i]) + " ";
        }

        // If stat
        std::string ifstat = "";
        for (std::map<std::string, netstats>::iterator itd = (*it).ifstats.begin();
            itd != (*it).ifstats.end();
            ++itd
        ) {
            ifstat +=  boost::str(boost::format("%1% %2% %3% %4% %5%")
                % (*itd).first
                % (*itd).second.rxb
                % (*itd).second.txb
                % (*itd).second.rxp
                % (*itd).second.txp
            );

            ifstat += "\n";
        }

        // Drive space
        std::string drvspace = "";
        for (std::map<std::string, uintmax_t>::iterator itd = (*it).drv_us_space.begin();
            itd != (*it).drv_us_space.end();
            ++itd
        ) {
            drvspace +=  boost::str(boost::format("%1% %2% %3%") % (*itd).first % (*itd).second % drv_space[(*itd).first]) + "\n";
        }

        try {
            Json::Value jdata;
            jdata["dedicated_server_id"] = std::to_string(ds_id);
            jdata["loa"] = loa;
            jdata["ram"] = ram;
            jdata["cpu"] = cpu;
            jdata["ifstat"] = ifstat;
            jdata["ping"] = ping;
            jdata["drvspace"] = drvspace;

            std::time_t time = (*it).time;
            std::tm * ptm = std::localtime(&time);
            char buffer[32];
            std::strftime(buffer, 32, "%F %T", ptm);

            jdata["time"] = buffer;

            Gameap::Rest::post("/gdaemon_api/ds_stats", jdata);
            insert_complete.push_back(it);

        } catch (Gameap::Rest::RestapiException &exception) {
            std::cerr << "Output updating error: "
                      << exception.what()
                      << std::endl;
        }
    }

    // Clear completed
    for (int i = 0; i < insert_complete.size(); ++i) {
        stats.erase(insert_complete[i]);
    }

    if (insert_complete.size() > 0) {
        insert_complete.clear();
        last_db_update = time(0);
    }
}

std::string DedicatedServer::get_script_cmd(ushort script)
{
    switch (script) {
        case DS_SCRIPT_START:
            return script_start;
            break;

        case DS_SCRIPT_STOP:
            return script_stop;
            break;

        case DS_SCRIPT_RESTART:
            return script_restart;
            break;

        case DS_SCRIPT_STATUS:
            return script_status;
            break;

        case DS_SCRIPT_GET_CONSOLE:
            return script_get_console;
            break;

        case DS_SCRIPT_SEND_CMD:
            return script_send_command;
            break;
    }
}

std::string DedicatedServer::get_work_path()
{
    return work_path;
}