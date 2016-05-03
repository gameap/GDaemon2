#ifndef DB_H
#define DB_H

#include <vector>
#include <map>

#define MAX_ROWS 32
#define MAX_ROW_CONTENT 4096

struct db_row {
    std::map <std::string,std::string> row;
};

struct db_elems {
    int num_rows;
    int num_fields;
    std::vector<db_row> rows;
};

class Db {
public:
    db_elems results;
    
    virtual int connect(const char *host, const char *user, const char *passwd, const char *db, unsigned int port){ return -1; };
    virtual std::string real_escape_string(const char * str){ return ""; };
    virtual int query(const char * query){ return -1; };
    virtual void close(){ };
};

typedef Db* create_t();
typedef void destroy_t(Db*);

int load_db(Db **db, char *driver);

extern Db *db;

#endif
