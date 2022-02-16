#ifndef __PROGTEST__

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <climits>
#include <cfloat>
#include <cassert>
#include <cmath>
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <numeric>
#include <string>
#include <utility>
#include <vector>
#include <array>
#include <iterator>
#include <set>
#include <list>
#include <map>
#include <unordered_set>
#include <unordered_map>
#include <queue>
#include <stack>
#include <deque>
#include <memory>
#include <functional>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <pthread.h>
#include <semaphore.h>
#include "progtest_solver.h"
#include "sample_tester.h"

using namespace std;
#endif /* __PROGTEST__ */
//----------------------------------------------------------------------------------------------------------------------
// #define DEBUG_PRINT // uncomment to enable debug prints

/** CCargoPlanner class */
class CCargoPlanner;

/** Sales thread function. */
void salesThread(int tid, CCargoPlanner * cargoPlanner);

/** Work thread function. */
void workThread(int tid, CCargoPlanner * cargoPlanner);

/** Sale struct for sales thread. */
struct sale_t {
    shared_ptr<CShip>   m_ship;
    bool                m_end;
    sale_t(shared_ptr<CShip> ship, bool end):m_ship(std::move(ship)), m_end(end){}
    ~sale_t() = default;
};

/** Work struct for worker thread. */
struct work_t {
    int                         m_tid;
    shared_ptr<vector<CCargo>>  m_cargo;
    shared_ptr<CShip>           m_ship;
    bool                        m_end;
    work_t(int tid, shared_ptr<vector<CCargo>> cargo, shared_ptr<CShip> ship, bool end):m_tid(tid),m_cargo(std::move(cargo)), m_ship(std::move(ship)), m_end(end){}
    ~work_t() = default;
};

class CCargoPlanner {
public: // ew public member variables
    int                             m_numOfSalesThreads;
    int                             m_numOfWorkThreads;
    mutex                           m_runningMtx;
    int                             m_runningSales;
    int                             m_runningWorkers;
    vector<shared_ptr<CCustomer>>   v_customers;
    deque<shared_ptr<sale_t>>       q_sales;
    deque<shared_ptr<work_t>>       q_work;
    vector<thread>                  m_salesThreadsV;
    vector<thread>                  m_workThreadsV;
    mutex                           m_saleMtx;
    mutex                           m_workMtx;
    condition_variable              cv_emptyShipQ;
    condition_variable              cv_emptyWorkQ;
public:
    CCargoPlanner();
    ~CCargoPlanner();
    void Customer(const ACustomer& customer);
    void Start(int sales, int workers);
    void Ship(AShip ship);
    void Stop();
    static int SeqSolver(const vector<CCargo> &cargo, int maxWeight, int maxVolume, vector<CCargo> &load);
public:
    virtual void InsertSale(const shared_ptr<sale_t> & sale);
    virtual shared_ptr<sale_t> RemoveSale(int tid);
public:
    virtual void InsertWork(const shared_ptr<work_t> & work);
    virtual shared_ptr<work_t> RemoveWork(int tid);
};

//// CCargo class methods definition ////-------------------------------------------------------------------------------
CCargoPlanner::CCargoPlanner():m_numOfSalesThreads(0), m_numOfWorkThreads(0), m_runningSales(0), m_runningWorkers(0){}

CCargoPlanner::~CCargoPlanner() = default;

void CCargoPlanner::Customer(const ACustomer& customer) {
    v_customers.push_back(customer);
}
void CCargoPlanner::Start(int sales, int workers) {
    m_numOfSalesThreads = sales;
    m_numOfWorkThreads = workers;
    m_runningSales = sales;
    m_runningWorkers = workers;
    for (int i = 0; i < m_numOfSalesThreads; ++i) {
        m_salesThreadsV.emplace_back(salesThread, i, this );
    }
    for (int i = 0; i < m_numOfWorkThreads; ++i) {
        m_workThreadsV.emplace_back(workThread, i, this );
    }
}

void CCargoPlanner::Ship(AShip ship) {
    sale_t sale(std::move(ship), false);
    InsertSale(make_shared<sale_t>(sale));
}

void CCargoPlanner::Stop() {
    // notify all sales that the ships input has ended
    #ifdef DEBUG_PRINT
    printf("**************************** STOP REACHED ****************************\n");
    #endif /* DEBUG_PRINT */
    // insert end messages for running sales
    for (int i = 0; i < m_runningSales; i++) {
        sale_t sale_end(nullptr,true);
        InsertSale(make_shared<sale_t>(sale_end));
    }
    // wait until all threads complete their work
    for (auto & t : m_salesThreadsV)
        t.join();
    for (auto & t : m_workThreadsV)
        t.join();
}

int CCargoPlanner::SeqSolver(const vector<CCargo> &cargo, int maxWeight, int maxVolume, vector<CCargo> &load) {
    return ProgtestSolver(cargo, maxWeight, maxVolume, load);
}

void CCargoPlanner::InsertSale(const shared_ptr<sale_t> & sale){
    unique_lock<mutex> ul (m_saleMtx);
    q_sales.push_back(sale);
    #ifdef DEBUG_PRINT
    if (!sale->m_end)
        printf("Ship producer m:  item [main, %s, F] was inserted\n", sale->m_ship->Destination().c_str());
    else
        printf("Ship producer m:  item [main, n, T] was inserted\n");
    #endif /* DEBUG_PRINT */
    cv_emptyShipQ.notify_one();
}
shared_ptr<sale_t> CCargoPlanner::RemoveSale(int tid){
    shared_ptr<sale_t> sale;
    unique_lock<mutex> ul (m_saleMtx);
    cv_emptyShipQ.wait(ul, [ this ] () { return ( ! q_sales.empty() ); } );
    sale = q_sales.front();
    q_sales.pop_front();
    #ifdef DEBUG_PRINT
    if (!sale->m_end)
        printf("Ship consumer %d:  item [m, %s, F] was removed\n", tid, sale->m_ship->Destination().c_str());
    else
        printf("Ship consumer %d:  item [m, n, T] was removed\n", tid);
    #endif /* DEBUG_PRINT */
    return sale;
}

void CCargoPlanner::InsertWork(const shared_ptr<work_t> & work) {
    unique_lock<mutex> ul (m_workMtx);
    q_work.push_back(work);
    #ifdef DEBUG_PRINT
    if (!work->m_end)
        printf("Work producer %d:  item [%d, F] was inserted\n", work->m_tid, work->m_tid);
    else
        printf("Ship producer %d:  item [%d, T] was inserted\n", work->m_tid, work->m_tid);
    #endif /* DEBUG_PRINT */
    cv_emptyWorkQ.notify_one();
}

shared_ptr<work_t> CCargoPlanner::RemoveWork(int tid) {
    shared_ptr<work_t> work;
    unique_lock<mutex> ul (m_workMtx);
    cv_emptyWorkQ.wait(ul, [ this ] () { return ( ! q_work.empty() ); } );
    work = q_work.front();
    q_work.pop_front();
    #ifdef DEBUG_PRINT
    if (!work->m_end)
        printf("Work consumer %d:  item [%d, F] was inserted\n", tid, work->m_tid);
    else
        printf("Ship consumer %d:  item [%d, T] was inserted\n", tid, work->m_tid);
    #endif /* DEBUG_PRINT */
    return work;
}

//// Sales and work threads functions ////------------------------------------------------------------------------------
void salesThread(int tid, CCargoPlanner * cargoPlanner){
    #ifdef DEBUG_PRINT
    printf("Sales thread %d start.\n", tid);
    #endif /* DEBUG_PRINT */
    while (true) {
        shared_ptr<sale_t> sale = cargoPlanner->RemoveSale(tid);
        // if the sale is an indicator to end this thread break the loop and end this thread
        if (sale->m_end)
            break;
        // else do sale
        vector<CCargo> allCargoToLoad;
        vector<CCargo> temporaryCargo;
        for (auto & c : cargoPlanner->v_customers) {
            c->Quote(sale->m_ship->Destination(), temporaryCargo);
            allCargoToLoad.insert(allCargoToLoad.end(),temporaryCargo.begin(), temporaryCargo.end());
            temporaryCargo.clear();
        }
        work_t work(tid, make_shared<vector<CCargo>>(allCargoToLoad), sale->m_ship, false);
        cargoPlanner->InsertWork(make_shared<work_t>(work));
    }
    // producer exit sequence
    int var;
    unique_lock<mutex> uniqueLock(cargoPlanner->m_runningMtx);
    cargoPlanner->m_runningSales--;
    var = cargoPlanner->m_runningSales;
    uniqueLock.unlock();
    // if this thread is the last running sales thread, create ending messages for the rest of the running workers.
    if (var == 0){
        for (int i = 0; i < cargoPlanner->m_runningWorkers; ++i) {
            work_t work_end(tid, nullptr, nullptr, true);
            cargoPlanner->InsertWork(make_shared<work_t>(work_end));
        }
    }
    #ifdef DEBUG_PRINT
    printf("Sales thread %d end.\n", tid);
    #endif /* DEBUG_PRINT */
}

void workThread(int tid, CCargoPlanner * cargoPlanner){
    #ifdef DEBUG_PRINT
    printf("Work thread %d start.\n", tid);
    #endif /* DEBUG_PRINT */
    while (true) {
        shared_ptr<work_t> work = cargoPlanner->RemoveWork(tid);
        // if the thread has received a message to end
        if (work->m_end)
            break;
        // CCargoPlanner::SeqSolver(const vector<CCargo> &cargo, int maxWeight, int maxVolume, vector<CCargo> &load)
        vector<CCargo> load;
        cargoPlanner->SeqSolver(*(work->m_cargo), work->m_ship->MaxWeight(), work->m_ship->MaxVolume(), load);
        work->m_ship->Load(load);
    }
    #ifdef DEBUG_PRINT
    printf("work thread %d end.\n", tid);
    #endif /* DEBUG_PRINT */
}
////--------------------------------------------------------------------------------------------------------------------
#ifndef __PROGTEST__

int main(void) {
    CCargoPlanner test;
    vector<AShipTest> ships;
    vector<ACustomerTest> customers{make_shared<CCustomerTest>(), make_shared<CCustomerTest>()};

    ships.push_back(g_TestExtra[0].PrepareTest("New York", customers));
    ships.push_back(g_TestExtra[1].PrepareTest("Barcelona", customers));
    ships.push_back(g_TestExtra[2].PrepareTest("Kobe", customers));
    ships.push_back(g_TestExtra[8].PrepareTest("Perth", customers));
    // add more ships here

    for (auto x : customers)
        test.Customer(x);

    test.Start(3, 2);

    for (auto x : ships)
        test.Ship(x);

    test.Stop();

    for (auto x : ships)
        cout << x->Destination() << ": " << (x->Validate() ? "ok" : "fail") << endl;
    return 0;
}

#endif /* __PROGTEST__ */
