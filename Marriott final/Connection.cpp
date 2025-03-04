#include <iostream>
#include <libpq-fe.h>
using namespace std;

PGconn* ConnectToDatabase() {

    const char* conninfo = "dbname=Marriott user=dbdb2002 password=dbdb!2002 host=localhost port=5432";
    PGconn* conn = PQconnectdb(conninfo);

    if (PQstatus(conn) != CONNECTION_OK) {
        cerr << "[Error] Connection to database failed: " << PQerrorMessage(conn) << endl;
        PQfinish(conn);
        throw runtime_error("Database connection failed.");
    }
    cout << "Connection to database successful!" << endl;
    return conn;
}

void DisconnectFromDatabase(PGconn* conn) {
    PQfinish(conn);
    cout << "Connection to database closed." << endl;
}
