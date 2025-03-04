#include <iostream>
#include <libpq-fe.h>
#include "Connection.h"
#include <vector>
#include <cstring>

using namespace std;
int g_id;
string date;
class Client {
protected:
    PGconn* conn;
public:
    Client(PGconn* connection) : conn(connection) {}
    virtual void printSelection() {}
    virtual void Selection(char selection) {}
    virtual ~Client() {}
private:
    virtual void Query(char selectedQuery) {}
    virtual void Transaction(char selectedQuery) {}
};

class ManagerClient : public Client {
public:
    ManagerClient(PGconn *connection) : Client(connection) {}

    void printSelection() override {
        cout << "Manager Client\n"
                "a. rooms_occupied: view the currently occupied rooms\n"
                "b. housekeeping: list house-keeping assignments\n"
                "c. check_in: check-in a guest\n"
                "d. check_out: check-out a guest\n"
                "e. mark_serviced: record that a room has been serviced for the day\n"
                "Choose Your Query : " << endl;
    }

    void Selection(char selection) override {
        if (selection >= 'a' && selection <= 'b') {
            Query(selection);
        } else if (selection >= 'c' && selection <= 'e') {
            Transaction(selection);
        }
    }

private:
    void Query(char selectedQuery) override {
        switch (selectedQuery) {
            case 'a': {
                const char *Query = "SELECT r_number, type_name FROM room WHERE available = false;"; // available을 기준으로 fetch해옴
                PGresult *res = PQexec(conn, Query); // Query 실행 후 결과를 저장
                if (PQresultStatus(res) != PGRES_TUPLES_OK) { // 매 쿼리마다 반복되는 에러 구문, 에러 발생 시 메세지 출력 및 clear
                    cerr << "Query failed: " << PQerrorMessage(conn) << endl;
                    PQclear(res);
                    return;
                }
                cout << "[Room Occupied]" << endl;
                for (int i = 0; i < PQntuples(res); ++i) { // getvalue로 각 field의 정보를 출력
                    cout << "Room ID: " << PQgetvalue(res, i, 0)
                         << ", Type: " << PQgetvalue(res, i, 1) << endl;
                }
                break;
            }
            case 'b': {
                const char *Query = "SELECT h.h_name, hk.r_number, e.e_name, hk.date \n"
                                    "FROM housekeeping hk \n"
                                    "JOIN hotel h ON hk.h_id = h.h_id \n"
                                    "JOIN employee e ON hk.e_id = e.e_id"; // 하우스 키핑을 위해 hotel, employee를 join 시킴
                PGresult *res = PQexec(conn, Query);
                if (PQresultStatus(res) != PGRES_TUPLES_OK) {
                    cerr << "Query failed: " << PQerrorMessage(conn) << endl;
                    PQclear(res);
                    return;
                }
                cout << "[Housekeeping]" << endl;
                for (int i = 0; i < PQntuples(res); ++i) {
                    cout << "Hotel Name: " << PQgetvalue(res, i, 0)
                         << ", Room Number: " << PQgetvalue(res, i, 1)
                         << ", Employee: " << PQgetvalue(res, i, 2)
                         << ", Date: " << PQgetvalue(res, i, 3) << endl;
                }
                PQclear(res);
                break;
            }
            default: {
                cerr << "Invalid Selection" << selectedQuery << "Please choose a valid option a~e.";
                break;
            }
        }
    };

    void Transaction(char selectedQuery) override {
        PGresult *res;
        res = PQexec(conn, "BEGIN TRANSACTION ISOLATION LEVEL SERIALIZABLE;"); // serializable하게 시작(transaction의 경우)
        if (PQresultStatus(res) != PGRES_COMMAND_OK) { // Command냐 Tuple 등을 받아오느냐에 따라 비교문이 차이남
            cerr << "Failed to start transaction: " << PQerrorMessage(conn) << endl;
            PQclear(res);
            return;
        }
        PQclear(res);

        try { // 내부 에러문이 전자들과 다름. rollback을 위해서 이다
            switch (selectedQuery) {
                case 'c': {
                    int reservation_index;

                    cout << "[Today's Check-in List]" << endl;
                    string Query = "SELECT reservation_date, r_number, h_id FROM reservation WHERE reservation_date = " + date + ";";
                    res = PQexec(conn, Query.c_str());
                    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
                        cerr << "Failed to fetch today's check-in list: " << PQerrorMessage(conn) << endl;
                        PQclear(res);
                        throw runtime_error("Failed to fetch check-in list"); // rollback 하기 위해 error를 던짐
                    }

                    vector<int> reservation_ids;
                    for (int i = 0; i < PQntuples(res); ++i) {
                        cout << i + 1 << ". Reservation Date: " << PQgetvalue(res, i, 0)
                             << ", Room Number: " << PQgetvalue(res, i, 1)
                             << ", Hotel ID: " << PQgetvalue(res, i, 2) << endl;
                        reservation_ids.push_back(i); // 편한 선택을 위해 벡터 사용
                    }
                    PQclear(res);

                    cout << "Enter the reservation number to check in: ";
                    cin >> reservation_index;
                    reservation_index -= 1; // 0-based index로 사용

                    if (reservation_index < 0 || reservation_index >= reservation_ids.size()) { // 벡터의 범위를 넘어가면
                        cerr << "Invalid reservation number." << endl;
                        throw runtime_error("Invalid reservation selection");
                    }

                    string fetchQuery = "SELECT r_number, h_id FROM reservation WHERE reservation_date = " + date +
                                                   " LIMIT 1 OFFSET " + to_string(reservation_index) + ";"; // LIMIT, OFFSET을 통해 index 번째의 정보 fetch(vector의 index를 활용하기 위해서)
                    res = PQexec(conn, fetchQuery.c_str());
                    if (PQntuples(res) == 0) {
                        cerr << "Check-in failed: No reservation found for the selected index." << endl;
                        PQclear(res);
                        throw runtime_error("No reservation found");
                    }

                    int r_number = atoi(PQgetvalue(res, 0, 0));
                    int h_id = atoi(PQgetvalue(res, 0, 1));
                    PQclear(res);

                    string updateQuery = "UPDATE room SET available = false WHERE r_number = " + to_string(r_number) +
                                         " AND h_id = " + to_string(h_id) + ";";
                    res = PQexec(conn, updateQuery.c_str());
                    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
                        cerr << "Failed to update room availability: " << PQerrorMessage(conn) << endl;
                        PQclear(res);
                        throw runtime_error("Update failed");
                    }
                    PQclear(res);

                    break;
                }
                case 'd': {
                    int r_number, h_id;

                    cout << "Enter Hotel ID: ";
                    cin >> h_id;
                    cout << "Enter Room Number: ";
                    cin >> r_number;

                    string roomCheckQuery = "SELECT available FROM room WHERE r_number = " + to_string(r_number) +
                                            " AND h_id = " + to_string(h_id) + ";"; // 방 상태 확인(available)
                    res = PQexec(conn, roomCheckQuery.c_str());
                    if (PQntuples(res) == 0 || strcmp(PQgetvalue(res, 0, 0), "true") == 0) { // 방이 available 하다면(check-in이 안됨)
                        cerr << "Check-out failed: Room is not currently occupied." << endl;
                        PQclear(res);
                        throw runtime_error("Room not occupied");
                    }
                    PQclear(res);

                    string validateQuery = "SELECT reservation_date, stay_duration, cost "
                                           "FROM reservation "
                                           "WHERE r_number = " + to_string(r_number) +
                                           " AND h_id = " + to_string(h_id) +
                                           " AND reservation_date <= " + date +
                                           " AND reservation_date + INTERVAL '1 day' * stay_duration > " + date + ";";
                    // 예약 확인 및 투숙 기간 가져오기, INTERVAL을 사용해 특정 일 수 이후를 지정가능
                    res = PQexec(conn, validateQuery.c_str());
                    if (PQntuples(res) == 0) {
                        cerr << "Check-out failed: No active reservation found for the given date." << endl;
                        PQclear(res);
                        throw runtime_error("Validation failed");
                    }

                    string reservation_date = PQgetvalue(res, 0, 0);
                    int stay_duration = atoi(PQgetvalue(res, 0, 1));
                    double total_cost = atof(PQgetvalue(res, 0, 2));
                    PQclear(res);

                    string calculateQuery = "SELECT (DATE " + date + " - DATE '" + reservation_date + "');";
                    // 원 체크아웃 날짜 까지의 날 계산
                    res = PQexec(conn, calculateQuery.c_str());
                    int days_used = atoi(PQgetvalue(res, 0, 0));
                    PQclear(res);

                    // 환불 계산
                    double refund = 0;
                    if (days_used < stay_duration) {
                        int remaining_days = stay_duration - days_used;
                        double daily_rate = total_cost / stay_duration;
                        refund = daily_rate * remaining_days;
                    }
                    cout << "Refund: $" << refund << endl;

                    // 방 상태 업데이트
                    string updateQuery = "UPDATE room SET available = true WHERE r_number = " + to_string(r_number) +
                                         " AND h_id = " + to_string(h_id) + ";";
                    res = PQexec(conn, updateQuery.c_str());
                    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
                        cerr << "Failed to update room availability: " << PQerrorMessage(conn) << endl;
                        PQclear(res);
                        throw runtime_error("Update failed");
                    }
                    PQclear(res);
                    int e_id;
                    cout << "(Housekeeping)Employee id : ";
                    cin >> e_id;
                    // 청소 작업 추가(자동으로 하우스 키핑 할당)
                    string housekeepingQuery = "INSERT INTO housekeeping (h_id, r_number, date,e_id) VALUES (" +
                                               to_string(h_id) + ", " + to_string(r_number) + ", " + date + ", " +
                            to_string(e_id) + ");";
                    res = PQexec(conn, housekeepingQuery.c_str());
                    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
                        cerr << "Failed to record housekeeping: " << PQerrorMessage(conn) << endl;
                        PQclear(res);
                        throw runtime_error("Housekeeping record failed");
                    }
                    PQclear(res);

                    break;
                }

                case 'e': {
                    int r_number, h_id, e_id;

                    cout << "Enter Hotel ID: ";
                    cin >> h_id;
                    cout << "Enter Room Number: ";
                    cin >> r_number;
                    cout << "Enter Employee ID: ";
                    cin >> e_id;

                    string checkQuery = "SELECT * FROM housekeeping "
                                        "WHERE r_number = " + to_string(r_number) +
                                        " AND h_id = " + to_string(h_id) +
                                        " AND date = " + date + ";";
                    res = PQexec(conn, checkQuery.c_str());
                    if (PQntuples(res) > 0) { // 이미 서비스 된 (결과값이 있다면) 방이면 에러
                        cerr << "Error: Room " << r_number << " at Hotel " << h_id
                             << " has already been serviced today." << endl;
                        PQclear(res);
                        throw runtime_error("Already serviced");
                    }
                    PQclear(res);


                    string insertQuery = "INSERT INTO housekeeping (h_id, r_number, e_id, date) VALUES (" +
                                         to_string(h_id) + ", " + to_string(r_number) + ", " + to_string(e_id) + ", " +
                                         date + ");";
                    res = PQexec(conn, insertQuery.c_str());
                    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
                        cerr << "Failed to insert housekeeping record: " << PQerrorMessage(conn) << endl;
                        PQclear(res);
                        throw runtime_error("Insert failed");
                    }
                    PQclear(res);

                    break;
                }
                default:
                    throw runtime_error("Invalid selection");
            }

            res = PQexec(conn, "COMMIT;");
            if (PQresultStatus(res) != PGRES_COMMAND_OK) {
                cerr << "Failed to commit transaction: " << PQerrorMessage(conn) << endl;
                PQclear(res);
                return;
            }
            PQclear(res);

        } catch (const runtime_error &e) {
            cerr << e.what() << ": Rolling back transaction." << endl;

            // 전체 트랜잭션 롤백(savepoint 없음)
            res = PQexec(conn, "ROLLBACK;");
            if (PQresultStatus(res) != PGRES_COMMAND_OK) {
                cerr << "Failed to rollback transaction: " << PQerrorMessage(conn) << endl;
            }
            PQclear(res);
        }
    }
};

class CustomerClient : public Client {
public:
    CustomerClient(PGconn *connection) : Client(connection) {}

    void printSelection() override {
        cout << "Customer Client [ID : " << g_id << "]\n"
                                                    "a. rooms_available: view the room types and costs that are still available\n"
                                                    "b. cost_at_checkout: calculate the total cost for the guest at checkout time\n"
                                                    "c. my_reservations: list future reservations for the guest\n"
                                                    "d. reserve: make a reservation\n"
                                                    "e. cancel: cancel a reservation\n"
                                                    "(Addition)f. Hotels: hotel. view hotel and hotel brand\n"
                                                    "Choose Your Query" << endl;
    }

    void Selection(char selection) override {
        if ((selection >= 'a' && selection <= 'c') || (selection == 'f')) {
            Query(selection);
        } else if (selection >= 'd' && selection <= 'e') {
            Transaction(selection);
        }
    }

private:
    void Query(char selectedQuery) override {
        switch (selectedQuery) {
            case 'a': {
                const char *Query = "SELECT h.h_id, r.r_number, r.type_name, r.price, r.capacity "
                                    "FROM room r "
                                    "JOIN hotel h ON r.h_id = h.h_id "
                                    "WHERE r.available = true "
                                    "ORDER BY h.h_id, r.type_name, r.price;";
                PGresult *res = PQexec(conn, Query);
                if (PQresultStatus(res) != PGRES_TUPLES_OK) {
                    cerr << "Query failed: " << PQerrorMessage(conn) << endl;
                    PQclear(res);
                    return;
                }

                cout << "[Available Rooms]" << endl;
                for (int i = 0; i < PQntuples(res); ++i) {
                    cout << "Hotel ID: " << PQgetvalue(res, i, 0)
                         << ", Room Number: " << PQgetvalue(res, i, 1)
                         << ", Type: " << PQgetvalue(res, i, 2)
                         << ", Price: $" << PQgetvalue(res, i, 3)
                         << ", Capacity: " << PQgetvalue(res, i, 4) << endl;
                }

                PQclear(res);
                break;
            }

            case 'b': {
                // Check-in된 예약은 해당 방이 더 이상 available하지 않은 것으로 간주
                string query = "SELECT reservation_date, stay_duration, cost "
                               "FROM reservation r "
                               "JOIN room rm ON r.r_number = rm.r_number AND r.h_id = rm.h_id "
                               "WHERE r.g_id = " + to_string(g_id) +
                               " AND r.reservation_date <= " + date +
                               " AND rm.available = false;";
                PGresult* res = PQexec(conn, query.c_str());
                if (PQresultStatus(res) != PGRES_TUPLES_OK) {
                    cerr << "Query failed: " << PQerrorMessage(conn) << endl;
                    PQclear(res);
                    return;
                }

                cout << "[Total Cost at Checkout]" << endl;
                double total_cost = 0;
                for (int i = 0; i < PQntuples(res); ++i) {
                    string reservation_date = PQgetvalue(res, i, 0);
                    int stay_duration = atoi(PQgetvalue(res, i, 1));
                    double cost = atof(PQgetvalue(res, i, 2));
                    cout << "Reservation Date: " << reservation_date
                         << ", Stay Duration: " << stay_duration
                         << ", Cost: $" << cost << endl;
                    total_cost += cost; // 전체 cost 계산
                }
                cout << "Guest ID: " << g_id << ", Total Cost: $" << total_cost << endl;
                PQclear(res);
                break;
            }


            case 'c': {

                string Query =
                        "SELECT reservation_date, r_number FROM reservation WHERE g_id = " + to_string(g_id) +
                        " AND reservation_date >= " + date + " ORDER BY reservation_date;"; // 현 날짜 이후의 reservation(future)만 fetch
                PGresult *res = PQexec(conn, Query.c_str());

                if (PQresultStatus(res) != PGRES_TUPLES_OK) {
                    cerr << "Query failed: " << PQerrorMessage(conn) << endl;
                    PQclear(res);
                    return;
                }

                cout << "[Future Reservations]" << endl;
                for (int i = 0; i < PQntuples(res); ++i) {
                    cout << "Reservation Date: " << PQgetvalue(res, i, 0)
                         << ", Room Number: " << PQgetvalue(res, i, 1) << endl;
                }

                PQclear(res);
                break;
            }
            case 'f': {
                const char *Query = "SELECT b.name, h.h_name "
                                    "FROM brand b "
                                    "JOIN hotel h ON b.b_id = h.b_id "
                                    "ORDER BY b.name, h.h_name;";
                PGresult *res = PQexec(conn, Query);
                if (PQresultStatus(res) != PGRES_TUPLES_OK) {
                    cerr << "Query failed: " << PQerrorMessage(conn) << endl;
                    PQclear(res);
                    return;
                }
                cout << "[Hotels]" << endl;
                for (int i = 0; i < PQntuples(res); ++i) {
                    cout << "Brand Name: " << PQgetvalue(res, i, 0)
                         << ", Hotel Name: " << PQgetvalue(res, i, 1) << endl;
                }
                PQclear(res);
                break;
            }
        }
    }

    void Transaction(char selectedQuery) override {
        PGresult *res;
        res = PQexec(conn, "BEGIN TRANSACTION ISOLATION LEVEL SERIALIZABLE;");
        if (PQresultStatus(res) != PGRES_COMMAND_OK) {
            cerr << "Failed transaction: " << PQerrorMessage(conn) << endl;
            PQclear(res);
            return;
        }
        PQclear(res);

        try {
            switch (selectedQuery) {
                case 'd': {
                    int h_id, r_number, voucher, stay_duration;
                    string reservation_date;

                    cout << "Enter Hotel ID: ";
                    cin >> h_id;
                    cout << "Enter Room Number: ";
                    cin >> r_number;
                    cout << "Enter Reservation Date (YYYY-MM-DD): ";
                    cin >> reservation_date;
                    cout << "Enter Stay Duration (in nights): ";
                    cin >> stay_duration;

                    // 예약 중복 확인
                    string checkOverlapQuery = "SELECT COUNT(*) FROM reservation "
                                               "WHERE r_number = " + to_string(r_number) +
                                               " AND h_id = " + to_string(h_id) +
                                               " AND reservation_date <= DATE '" + reservation_date + "' + INTERVAL '" +
                                               to_string(stay_duration) + " days' "
                                                                          "AND reservation_date + INTERVAL '1 day' * stay_duration >= DATE '" + reservation_date + "';";
                    res = PQexec(conn, checkOverlapQuery.c_str());
                    if (atoi(PQgetvalue(res, 0, 0)) > 0) { // 예약이 존재(count 사용)
                        cerr << "Reservation failed: Room is already booked for the requested dates." << endl;
                        PQclear(res);
                        throw runtime_error("Reservation conflict");
                    }
                    PQclear(res);

                    string roomDetailsQuery =
                            "SELECT price, capacity FROM room WHERE r_number = " + to_string(r_number) +
                            " AND h_id = " + to_string(h_id) + ";";
                    res = PQexec(conn, roomDetailsQuery.c_str());
                    if (PQntuples(res) == 0) {
                        cerr << "Reservation failed: Room details not found." << endl;
                        PQclear(res);
                        throw runtime_error("Room details not found");
                    }
                    double cost_per_night = atof(PQgetvalue(res, 0, 0));
                    int room_capacity = atoi(PQgetvalue(res, 0, 1));
                    double total_cost = cost_per_night * stay_duration;
                    PQclear(res);

                    cout << "Enter Voucher Percentage (0-100): ";
                    cin >> voucher;
                    double discount = total_cost * (voucher / 100.0); // 바우처 적용, discount 값
                    double final_cost = total_cost - discount;

                    cout << "Original Cost: $" << total_cost << ", Final Cost: $" << final_cost << endl;

                    int total_guests = 1; // 기본적으로 예약자 1명 포함
                    string companionCountQuery = "SELECT COUNT(*) FROM companion WHERE g_id = " + to_string(g_id) + ";"; //companion 수
                    res = PQexec(conn, companionCountQuery.c_str());
                    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
                        cerr << "Failed to fetch companion count: " << PQerrorMessage(conn) << endl;
                        PQclear(res);
                        throw runtime_error("Failed to fetch companion count");
                    }
                    int companion_count = atoi(PQgetvalue(res, 0, 0));
                    total_guests += companion_count;
                    PQclear(res);

                    // Room capacity 확인
                    if (total_guests > room_capacity) {
                        cerr << "Reservation failed: Total guests (" << total_guests << ") exceed room capacity (" << room_capacity << ")." << endl;
                        throw runtime_error("Exceed room capacity");
                    }

                    string insertQuery =
                            "INSERT INTO reservation (reservation_date, h_id, g_id, r_number, number, stay_duration, cost, voucher) "
                            "VALUES ('" + reservation_date + "', " + to_string(h_id) + ", " + to_string(g_id) + ", "
                            + to_string(r_number) + ", " + to_string(total_guests) + ", " + to_string(stay_duration) + ", "
                            + to_string(final_cost) + ", " + to_string(voucher) + ");";
                    res = PQexec(conn, insertQuery.c_str());
                    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
                        cerr << "Failed to create reservation: " << PQerrorMessage(conn) << endl;
                        PQclear(res);
                        throw runtime_error("Insert reservation failed");
                    }
                    PQclear(res);

                    break;
                }


                case 'e': {
                    int reservation_index;

                    string showReservationsQuery = "SELECT reservation_date, r_number, cost "
                                                   "FROM reservation "
                                                   "WHERE g_id = " + to_string(g_id) + ";";
                    res = PQexec(conn, showReservationsQuery.c_str());
                    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
                        cerr << "Failed to fetch reservations: " << PQerrorMessage(conn) << endl;
                        PQclear(res);
                        throw runtime_error("Failed to fetch reservations");
                    }

                    cout << "[Your Reservations]" << endl;
                    vector<string> reservation_dates;
                    vector<string> room_numbers;

                    for (int i = 0; i < PQntuples(res); ++i) {
                        cout << i + 1 << ". Date: " << PQgetvalue(res, i, 0)
                             << ", Room Number: " << PQgetvalue(res, i, 1)
                             << ", Cost: $" << PQgetvalue(res, i, 2) << endl;
                        reservation_dates.push_back(PQgetvalue(res, i, 0)); // 예약 날짜 저장
                        room_numbers.push_back(PQgetvalue(res, i, 1));      // 방 번호 저장, 전자와 동일하게 벡터 활용
                    }
                    PQclear(res);

                    cout << "Enter the reservation number to cancel: ";
                    cin >> reservation_index;
                    reservation_index -= 1;

                    if (reservation_index < 0 || reservation_index >= reservation_dates.size()) {
                        cerr << "Invalid reservation number." << endl;
                        throw runtime_error("Invalid reservation selection");
                    }

                    string selected_date = reservation_dates[reservation_index];
                    string selected_room = room_numbers[reservation_index];

                    if ("'" + selected_date + "'" == date) {
                        cerr << "Cancellation failed: Cannot cancel a reservation on the same day." << endl; // 동일 날짜 처리
                        throw runtime_error("Same-day cancellation not allowed");
                    }

                    string deleteQuery = "DELETE FROM reservation "
                                         "WHERE g_id = " + to_string(g_id) +
                                         " AND reservation_date = '" + selected_date + "'" +
                                         " AND r_number = " + selected_room + ";";
                    res = PQexec(conn, deleteQuery.c_str());
                    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
                        cerr << "Failed to cancel reservation: " << PQerrorMessage(conn) << endl;
                        PQclear(res);
                        throw runtime_error("Delete reservation failed");
                    }
                    PQclear(res);

                    cout << "Reservation successfully canceled." << endl;

                }

            }

            res = PQexec(conn, "COMMIT;");
            if (PQresultStatus(res) != PGRES_COMMAND_OK) {
                cerr << "Failed to commit transaction: " << PQerrorMessage(conn) << endl;
                PQclear(res);
                return;
            }
            PQclear(res);

        } catch (const runtime_error &e) {
            cerr << e.what() << ": Rolling back transaction." << endl;
            res = PQexec(conn, "ROLLBACK;"); // 전체 트랜잭션을 롤백
            if (PQresultStatus(res) != PGRES_COMMAND_OK) {
                cerr << "Failed to rollback transaction: " << PQerrorMessage(conn) << endl;
            }
            PQclear(res);
        }

    }
};


int main() {
    PGconn *conn = nullptr;
    try {
        conn = ConnectToDatabase();
    } catch (const runtime_error &e) {
        cerr << e.what() << endl;
        return 1;
    }
    cout << "Enter the date (YYYY-MM-DD): ";
    cin >> date;
    date = "'" + date + "'";
    string login;
    cout << "LOGIN : ";
    cin >> login;
    if (login == "manager") {
        while (true) {
            ManagerClient managerClient(conn);
            managerClient.printSelection();
            char selectedQuery;
            cin >> selectedQuery;
            managerClient.Selection(selectedQuery);
        }

    } else if (login == "customer") {
        string select;

        bool is_login = false;
        while (!is_login) {

            cout << "login or register : ";
            cin >> select;
            if (select == "login") {
                cout << "Enter your g_id :";
                cin >> g_id;
                is_login = true;
            } else if (select == "register") {
                string name;
                cout << "Enter your name : ";
                cin >> name;
                string email;
                cout << "Enter your E-mail : ";
                cin >> email;
                int companions;
                string insertQuery =
                        "INSERT INTO guest (g_name, email) VALUES ('" + name + "', '" + email +
                        "') RETURNING g_id;";

                PGresult *res = PQexec(conn, insertQuery.c_str());
                if (PQresultStatus(res) != PGRES_TUPLES_OK) {
                    cerr << "Failed to insert customer: " << PQerrorMessage(conn) << endl;
                    PQclear(res);
                    return 1;
                }
                cout << "Enter number of your Companions. If you don't have any, type 0";
                g_id = atoi(PQgetvalue(res, 0, 0));
                PQclear(res);
                cin >> companions;
                for (int i = 0; i < companions; i++) {
                    string c_name;
                    string relationship;
                    cout << "Enter Companion's name :";
                    cin >> c_name;
                    cout << "Enter Companion's relationship :";
                    cin >> relationship;
                    insertQuery =
                            "INSERT INTO companion (g_id, name, relationship) VALUES ('" + to_string(g_id) +
                            "', '" + name + "', '" + relationship + "');";
                    PGresult *res = PQexec(conn, insertQuery.c_str());
                    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
                        cerr << "Failed to insert companion: " << PQerrorMessage(conn) << endl;
                        PQclear(res);
                        return 1;
                    }
                    PQclear(res);
                }

                is_login = true;

            } else { cout << "Please enter valid selection " << endl; }
        }

        while (true) {

            CustomerClient customer(conn);
            customer.printSelection();
            char selectedQuery;
            cin >> selectedQuery;
            customer.Selection(selectedQuery);
        }
    } else {
        cout << "Login failed. Try again.";
        return 0;
    }


}