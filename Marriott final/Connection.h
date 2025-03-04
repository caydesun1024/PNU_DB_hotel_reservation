#ifndef CONNECTION_H
#define CONNECTION_H

#include <libpq-fe.h>

PGconn* ConnectToDatabase();

void DisconnectFromDatabase(PGconn* conn);

#endif // CONNECTION_H
