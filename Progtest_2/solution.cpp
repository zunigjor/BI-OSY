/**
 * @author: Jorge Zuniga (zunigjor)
 * @brief My solution for BI-OSY Progtest no.2: Memory manager
 */
#ifndef __PROGTEST__

#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <pthread.h>
#include <semaphore.h>
#include "common.h"

using namespace std;
#endif /* __PROGTEST__ */
// #define DEBUG_PRINT // uncomment to enable debug prints
////----------------------------------------------------------------------------------------------------------CPageStack
static class CPageStack{
private:
    uint32_t m_maxSize; // maximum stack size
    uint32_t m_top;     // index of the top of the stack
    uint32_t * m_arr;   // data array
    pthread_mutex_t m_stackMutex; // stack mutex
public:
    void deleteStack(){delete[] m_arr;}
    void init(uint32_t totalPages){
        m_maxSize = totalPages;
        m_top = totalPages - 1;
        m_arr = new uint32_t [m_maxSize];
        pthread_mutex_init(&m_stackMutex, nullptr);
        for (uint32_t i = 0; i < m_maxSize; ++i)
            m_arr[i] = m_maxSize - (i+1);
    }
    void lock(){pthread_mutex_lock(&m_stackMutex);}
    void unlock(){pthread_mutex_unlock(&m_stackMutex);}
    uint32_t size(){return m_top + 1;}
    void push(uint32_t x){ if (m_top + 1 < m_maxSize) m_arr[++m_top] = x; }
    uint32_t pop(){ if (m_top >= 0) return m_arr[m_top--]; }
} pageStack;
////-------------------------------------------------------------------------------------------------------------Globals
uint32_t runningProcess = 0;
pthread_mutex_t runningMtx = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t runningCond = PTHREAD_COND_INITIALIZER;
////--------------------------------------------------------------------------------------------------------------CMyCPU
class CMyCPU : public CCPU {
private:
    uint32_t m_CurrentPagesUsed = 0; // celkovy pocet stranek k dispozici (L1 a L2 se nezapocitava)
    uint32_t m_L2PagesUsed = 0; // pocet stranek v root tabulce
    uint32_t * m_RootPageAddr = nullptr; // pointer na root tabulku
private:
    struct thread_args{
        void (* entryPoint)(CCPU *, void *);
        CCPU * ccpu;
        void * processArgs;
        thread_args(void (* ep)(CCPU *, void *),CCPU * cpu, void * pArgs ):entryPoint(ep),ccpu(cpu),processArgs(pArgs){}
    };
    static void * thread_args_wrapper(void * voidArgs){
        #ifdef DEBUG_PRINT
        printf("Thread start.\n");
        #endif /*DEBUG_PRINT*/
        pthread_mutex_lock(&runningMtx);
        runningProcess++;
        pthread_mutex_unlock(&runningMtx);
        auto *args = (thread_args *) voidArgs;
        args->entryPoint(args->ccpu, args->processArgs);
        delete args->ccpu;
        delete args;
        pthread_mutex_lock(&runningMtx);
        uint32_t x = --runningProcess;
        pthread_mutex_unlock(&runningMtx);
        if (x == 0) pthread_cond_signal(&runningCond);
        return nullptr;
    }
public:
    /**
     * Konstruktor inicializuje ??lensk?? prom??nn?? podle parametr??.
     * Druh??m parametrem je r??mec str??nky, kde je um??st??n adres???? str??nek nejvy?????? ??rovn??
     * (toto nastaven?? str??nkov??n?? bude pou??ito pro p??epo??et adres v tomto simulovan??m CPU).
     * @param memStart "opravdov??" ukazatel na po????tek bloku pam??ti, kter?? byl simulaci p??ed??n p??i vol??n?? MemMgr.
     * @param pageTableRootIndex
     */
    CMyCPU(uint8_t *memStart, uint32_t pageTableRootIndex):CCPU(memStart, pageTableRootIndex){
        m_RootPageAddr = (uint32_t *) (m_MemStart + (m_PageTableRoot & ADDR_MASK));
        memset(m_RootPageAddr, 0,  PAGE_SIZE);
    }
    virtual ~CMyCPU(){
        pageStack.lock();
        pageStack.push(m_PageTableRoot >> OFFSET_BITS);
        pageStack.unlock();
    }
    /**
     * Metoda GetMemLimit zjist??, kolik str??nek m?? alokovan??ch proces, pro kter?? je pou????v??na tato instance CCPU.
     */
    virtual uint32_t GetMemLimit(void) const{ return m_CurrentPagesUsed; }
    /**
     * Metoda SetMemLimit nastav?? pam????ov?? limit (v po??tu str??nek) pro tento proces.
     * Metoda m????e b??t pou??ita jak pro zv??t??en??, tak pro zmen??en?? pam????ov??ho prostoru simulovan??ho procesu.
     * @param pages Nov?? pam????ov?? limit (v po??tu str??nek).
     * * @return true pro ??sp??ch, false pro ne??sp??ch (nap??. nen?? dostatek pam??ti pro alokaci).
     */
    virtual bool SetMemLimit(uint32_t pages){
        #ifdef DEBUG_PRINT
        printf("\n");
        printf("Current: %d -> ", m_CurrentPagesUsed);
        printf("Set to: %d\n", pages);
        #endif /*DEBUG_PRINT*/
        pageStack.lock();
        if (m_CurrentPagesUsed < pages){
            bool res = addPages(pages);
            pageStack.unlock();
            return res;
        } else if (m_CurrentPagesUsed > pages){
            bool res = removePages(pages);
            pageStack.unlock();
            return res;
        }
        pageStack.unlock();
        return true;
    }
    bool addPages(uint32_t pages){
        uint32_t l2pages = int(pages / PAGE_DIR_ENTRIES) + (1 * (pages % PAGE_DIR_ENTRIES != 0)); // zjistim kolik L2 tabulek potrebuju
        uint32_t pagesToAdd = pages - m_CurrentPagesUsed; // kolik stranek musim pridat
        uint32_t l2Filled = m_CurrentPagesUsed % PAGE_DIR_ENTRIES;
        if (pagesToAdd + (l2pages - m_L2PagesUsed) > pageStack.size()){
            return false;
        }
        #ifdef DEBUG_PRINT
        printf("need: %u, toAdd: %u\n", l2pages, pagesToAdd);
        #endif /*DEBUG_PRINT*/
        uint32_t l1Size = m_CurrentPagesUsed / PAGE_DIR_ENTRIES;
        bool toFill = m_RootPageAddr[l1Size] != 0;
        // naplnit root tabulku
        for (uint32_t i = m_L2PagesUsed; i < l2pages; ++i) {
            m_RootPageAddr[i] = (pageStack.pop() << OFFSET_BITS) | BIT_USER | BIT_WRITE | BIT_PRESENT;
            memset((uint32_t *) (m_MemStart + (m_RootPageAddr[i] & ADDR_MASK)), 0, PAGE_SIZE);
            m_L2PagesUsed++;
        }
        // posledni l2 tabulka neni plna
        if (toFill){
            uint32_t *level2 = (uint32_t *) (m_MemStart + (m_RootPageAddr[l1Size++] & ADDR_MASK));
            for (uint32_t  i = l2Filled; i < PAGE_DIR_ENTRIES && pagesToAdd > 0; ++i) {
                level2[i] = (pageStack.pop() << OFFSET_BITS) | BIT_USER | BIT_WRITE | BIT_PRESENT;
                #ifdef DEBUG_PRINT
                printf("root[%u][%u] = %u\n",l1Size-1,i , level2[i]>>12);
                #endif /*DEBUG_PRINT*/
                pagesToAdd--;
            }
        }
        // posledni l2 tabulka jiz je naplnena
        // plnim dalsi L2 tabulky az do po
        for (uint32_t i = l1Size; i < l2pages; ++i) {
            auto *level2 = (uint32_t *) (m_MemStart + (m_RootPageAddr[i] & ADDR_MASK));
            uint32_t j;
            for ( j = 0; j < PAGE_DIR_ENTRIES && j < pagesToAdd; ++j) {
                level2[j] = (pageStack.pop() << OFFSET_BITS) | BIT_USER | BIT_WRITE | BIT_PRESENT;
                #ifdef DEBUG_PRINT
                printf("root[%u][%u] = %u\n",i,j , level2[j]>>12);
                #endif /*DEBUG_PRINT*/
            }
            pagesToAdd -= j;
        }
        m_CurrentPagesUsed = pages;
        return true;
    }

    bool removePages(uint32_t pages){
        uint32_t pagesToRemove =  m_CurrentPagesUsed - pages; // kolik stranek musim ubrat
        #ifdef DEBUG_PRINT
        printf("toARemove: %d\n", pagesToRemove);
        #endif /*DEBUG_PRINT*/
        for (uint32_t i = m_L2PagesUsed - 1; pagesToRemove > 0 ; i--) {
            auto *level2 = (uint32_t *) (m_MemStart + (m_RootPageAddr[i] & ADDR_MASK));
            uint32_t pagesInLastL2 = m_CurrentPagesUsed % PAGE_DIR_ENTRIES;
            if (pagesInLastL2 == 0) pagesInLastL2 = PAGE_DIR_ENTRIES;
            uint32_t j;
            for (j = pagesInLastL2; j > 0 && pagesToRemove > 0; j--) {
                #ifdef DEBUG_PRINT
                printf("root[%u][%u] = %u -> ",i,j-1,level2[j-1]>>12);
                #endif /*DEBUG_PRINT*/
                pageStack.push(level2[j-1] >> OFFSET_BITS);
                level2[j-1] = 0;
                #ifdef DEBUG_PRINT
                printf("root[%u][%u] = %u\n",i,j-1,level2[j-1]>>12);
                #endif /*DEBUG_PRINT*/
                pagesToRemove--;
                m_CurrentPagesUsed--;
            }
            if (j == 0){
                pageStack.push(m_RootPageAddr[i] >> OFFSET_BITS);
                m_RootPageAddr[i] = 0;
                m_L2PagesUsed--;
            }
        }
        return true;
    }
    /**
     * Metoda NewProcess vytvo???? nov?? simulovan?? proces (vl??kno).
     * @param processArg Parametry simulovan??ho procesu.
     * @param entryPoint Adresa funkce spou??t??n?? v nov??m vl??kn??.
     * @param copyMem T??et?? parametr ud??v??, zda m?? b??t nov?? vznikl??mu "procesu" vytvo??en pr??zdn?? adresn?? prostor.
     * (hodnota false, GetMemLimit v nov??m procesu bude vracet 0)
     * nebo zda m?? z??skat pam????ov?? obsah jako kopii pam????ov??ho prostoru st??vaj??c??ho procesu (true).
     * @return ??sp??ch true, ne??sp??ch false.
     */
    virtual bool NewProcess(void *processArg, void (*entryPoint)(CCPU *, void *), bool copyMem){
        pageStack.lock();
        uint32_t rootTableAddress = ((pageStack.pop() << OFFSET_BITS) | BIT_USER | BIT_WRITE | BIT_PRESENT);

        CMyCPU * cpu = new CMyCPU(m_MemStart, rootTableAddress);

        if (copyMem){
            if (!cpu->addPages(m_CurrentPagesUsed)){
                delete cpu;
                pageStack.unlock();
                return false;
            }
            for (uint32_t i = 0; i < m_L2PagesUsed; i++){
                auto * level2old = (uint32_t *) (m_MemStart + (m_RootPageAddr[i] & ADDR_MASK));
                auto * level2new = (uint32_t *) (m_MemStart + (cpu->m_RootPageAddr[i] & ADDR_MASK));
                for (uint32_t j = 0; j < PAGE_DIR_ENTRIES; ++j) {
                    auto * entryNew = (uint32_t *) (m_MemStart + (level2new[j] & ADDR_MASK));
                    auto * entryOld = (uint32_t *) (m_MemStart + (level2old[j] & ADDR_MASK));
                    memcpy(entryNew, entryOld, PAGE_SIZE);
                }
            }
        }
        pageStack.unlock();

        pthread_t detachedThread;
        pthread_attr_t thrAttr;
        pthread_attr_init ( &thrAttr );
        pthread_attr_setdetachstate ( &thrAttr, PTHREAD_CREATE_DETACHED );
        auto * args = new thread_args(entryPoint, cpu, processArg);
        pthread_create(&detachedThread, &thrAttr, thread_args_wrapper, (void *)args);
        pthread_attr_destroy ( &thrAttr );
        return true;
    }
protected:
    /*
     if copy-on-write is implemented:
     virtual bool pageFaultHandler ( uint32_t address, bool write );
     */
};
////--------------------------------------------------------------------------------------------------------------MemMgr
/**
 * Funkce zinicializuje Va??e intern?? struktury pro spr??vu pam??ti, vytvo???? instanci simulovan??ho procesoru a spust?? p??edanou funkci.
 * Zat??m je??t?? nen?? pot??eba vytv????et nov?? vl??kna - init pob?????? v hlavn??m vl??knu.
 * Init samoz??ejm?? za??ne vytv????et dal???? simulovan?? procesy, pro vytvo??en?? je v rozhran?? CCPU p????slu??n?? vol??n??.
 * Toto vol??n?? bude obslou??eno Va???? implementac?? (vytvo??en?? dal???? instance CCPU, p??id??len?? pam??ti, vytvo??en?? vl??kna, ...).
 * Init samoz??ejm?? n??kdy skon???? (vol??n?? se vr??t?? z p??edan?? funkce), stejn?? tak skon???? i ostatn?? procesy.
 * Hlavn?? vl??kno zpracov??vaj??c?? proces init po??k?? na dokon??en?? v??ech "proces??", uklid?? V??mi alokovan?? prost??edky a vr??t?? se z MemMgr.
 * @param mem Ukazatel na po????tek spravovan?? pam??ti
 * @param totalPages Velikost pam??ti v po??tu str??nek
 * @param processArg
 * @param mainProcess Ukazatel na funkci, kter?? bude spu??t??na jako prvn?? "proces" - obdoba procesu init.
 */
void MemMgr(void *mem, uint32_t totalPages, void *processArg, void (*mainProcess)(CCPU *, void *)){
    #ifdef DEBUG_PRINT
    printf("Start\n");
    #endif /*DEBUG_PRINT*/
    pageStack.init(totalPages);
    uint32_t rootTableAddress = ((pageStack.pop() << CCPU::OFFSET_BITS) | CCPU::BIT_USER | CCPU::BIT_WRITE | CCPU::BIT_PRESENT);
    #ifdef DEBUG_PRINT
    printf("init root table idx: %d\n", rootTableAddress >> CCPU::OFFSET_BITS);
    printf("total pages: %d\n", totalPages);
    #endif /*DEBUG_PRINT*/
    CMyCPU * cpu = new CMyCPU((uint8_t*)mem, rootTableAddress);

    mainProcess(cpu, processArg);

    while (runningProcess != 0){
        pthread_mutex_lock(&runningMtx);
        pthread_cond_wait(&runningCond, &runningMtx);
        pthread_mutex_unlock(&runningMtx);
    }
    delete cpu;
    pageStack.deleteStack();
    #ifdef DEBUG_PRINT
    printf("End\n");
    #endif /*DEBUG_PRINT*/
}
