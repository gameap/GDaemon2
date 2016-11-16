#include "../dl.h"
#include "db.h"

#include <iostream>
#pragma comment( lib, "netapi32.lib" )

// virtual int query(const char * query, db_elems *results)
// {
    // std::string qstr = str_replace("{db_prefix}", db_prefix, query);
// }

int load_db(Db **db, char *driver)
{
    char lib[16];

    #ifdef WIN32
        sprintf(lib, "db/%s.dll", driver);
    #else
        sprintf(lib, "%s/db/%s.so", GDADEMON_LIB_PATH, driver);
    #endif

    DLHANDLE handle = DLOPEN(lib);

    if (!handle) {
        std::cerr << "Cannot load library: " << DLERROR() << '\n';
        return -1;
    }

    DLERROR();

    create_t* create_db = (create_t*) DLSYM(handle, "create");
	#ifndef WIN32
		const char* dlsym_error = DLERROR();
		if (dlsym_error) {
			std::cerr << "Cannot load symbol create: " << dlsym_error << '\n';
			return -1;
		}
	#endif

    destroy_t* destroy_db = (destroy_t*) DLSYM(handle, "destroy");
	#ifndef WIN32
		dlsym_error = DLERROR();
		if (dlsym_error) {
			std::cerr << "Cannot load symbol destroy: " << dlsym_error << '\n';
			return -1;
		}
	#endif

    *db = create_db();

    return 0;
}
