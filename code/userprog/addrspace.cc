/*!
//      Routines to manage address spaces (executing user programs).
//
//      In order to run a user program, you must:
//
//      1. link with the -N -T 0 option
//      2. run coff2noff to convert the object file to Nachos format
//              (Nachos object code format is essentially just a simpler
//              version of the UNIX executable object code format)
//      3. load the NOFF file into the Nachos file system
//              (if you haven't implemented the file system yet, you
//              don't need to do this last step)
 */
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation
// of liability and disclaimer of warranty provisions.

#include "copyright.h"
#include "system.h"
#include "addrspace.h"
#include "noff.h"
#include "openfile.h"
#include "filesys.h"

#include <strings.h>        /* for bzero */

//----------------------------------------------------------------------
// SwapHeader
//      Do little endian to big endian conversion on the bytes in the
//      object file header, in case the file was generated on a little
//      endian machine, and we're now running on a big endian machine.
//----------------------------------------------------------------------

static void
SwapHeader (NoffHeader * noffH)
{
    noffH->noffMagic = WordToHost (noffH->noffMagic);
    noffH->code.size = WordToHost (noffH->code.size);
    noffH->code.virtualAddr = WordToHost (noffH->code.virtualAddr);
    noffH->code.inFileAddr = WordToHost (noffH->code.inFileAddr);
    noffH->initData.size = WordToHost (noffH->initData.size);
    noffH->initData.virtualAddr = WordToHost (noffH->initData.virtualAddr);
    noffH->initData.inFileAddr = WordToHost (noffH->initData.inFileAddr);
    noffH->uninitData.size = WordToHost (noffH->uninitData.size);
    noffH->uninitData.virtualAddr =
    WordToHost (noffH->uninitData.virtualAddr);
    noffH->uninitData.inFileAddr = WordToHost (noffH->uninitData.inFileAddr);
}

/*!
 * ReadAtVirtual
 * \param executable executable
 * \param into virtual address MIPS pointer
 * \param numBytes size to read
 * \param position offset
 * \param *pageTable table used for writing
 * \param numPages page
 * \return size actually read
 */
static void ReadAtVirtual(OpenFile *executable, int virtualaddr,
    int numBytes, int position,
    TranslationEntry *pageTable, unsigned numPages){
    char* buf = new char[numBytes];
    numBytes = executable->ReadAt(buf, numBytes, position);

    
    
    TranslationEntry *TempPageTable = machine->pageTable;
    unsigned int TempTableSize = machine->pageTableSize;
    
    machine->pageTable = pageTable;
    machine->pageTableSize = numPages;
    
    for (int i = 0; i < numBytes; i++)
        machine->WriteMem(virtualaddr + i, 1, buf[i]);
        
    machine->pageTable = TempPageTable;
    machine->pageTableSize = TempTableSize;

    delete [] buf;
}

SpaceId AddrSpace::_LAST_PID = 0;
List* AddrSpace::_SPACE_LIST = new List();
Lock* AddrSpace::_ADDR_SPACE_LOCK = new Lock("AddrSpace");

//----------------------------------------------------------------------
// AddrSpace::AddrSpace
//      Create an address space to run a user program.
//      Load the program from a file "executable", and set everything
//      up so that we can start executing user instructions.
//
//      Assumes that the object code file is in NOFF format.
//
//      First, set up the translation from program memory to physical
//      memory.  For now, this is really simple (1:1), since we are
//      only uniprogramming, and we have a single unsegmented page table
//
//      "executable" is the file containing the object code to load into memory
//----------------------------------------------------------------------

AddrSpace::AddrSpace (OpenFile * executable):
    lastTID(0), lastFD(0), mThreadList(new List), mThreadsWaiting(new List)
{
    if (!(executable->header()->permission() & FileHeader::Exec) || !(executable->header()->permission() & FileHeader::Read)){
        delete mThreadList;
        delete mThreadsWaiting;
        throw new PermissionException();
    }
    AddrSpace::_ADDR_SPACE_LOCK->Acquire();
    mPid = ++AddrSpace::_LAST_PID;
    
    mFdTable = new fd_bundle_t[MAX_OPEN_FILE];
    memset(mFdTable, 0, sizeof(fd_bundle_t) * MAX_OPEN_FILE);
    fd_lock = new Lock("AddrSpace FD Lock");
    
    addrspace_bundle_t* a_bundle = new addrspace_bundle_t;

    a_bundle->object = this;
    a_bundle->pid = mPid;
    a_bundle->result_code = 0;
    a_bundle->ref_cnt = 1;

    AddrSpace::_SPACE_LIST->Append(a_bundle);

    ListElement* e = AddrSpace::_SPACE_LIST->getFirst();

    do {
        if (((addrspace_bundle_t*)e->item)->object) DEBUG('c', "ListProcess: current is %d\n", ((addrspace_bundle_t*)e->item)->object->pid());
    } while ((e = e->next));
    AddrSpace::_ADDR_SPACE_LOCK->Release();
    
    memset(stackUsage, -1, sizeof(int) * MAX_THREADS);

    NoffHeader noffH;
    unsigned int i, size;

    executable->ReadAt ((char *) &noffH, sizeof (noffH), 0);
    if ((noffH.noffMagic != NOFFMAGIC) &&
    (WordToHost (noffH.noffMagic) == NOFFMAGIC))
    SwapHeader (&noffH);
    
    if (noffH.noffMagic != NOFFMAGIC){
        delete mThreadList;
        delete mThreadsWaiting;
		a_bundle->object = nullptr;
        DEC_REF(pid());
        throw new ExecutableException();
	}

    // how big is address space?
    //~ size = noffH.code.size + noffH.initData.size + noffH.uninitData.size + UserStackSize * MAX_THREADS;    // we need to increase the size
    // to leave room for the stack

    unsigned int ro_code = divRoundUp(noffH.code.size + noffH.initData.size, PageSize),
        rw_code = divRoundDown(noffH.uninitData.virtualAddr, PageSize);

    numPages = divRoundUp(noffH.code.size + noffH.initData.size + noffH.uninitData.size, PageSize);
    size = numPages * PageSize;

    ASSERT (numPages < frameprovider->NumAvailFrame());    // check we're not trying
    // to run anything too big --
    // at least until we have
    // virtual memory

    DEBUG ('a', "Initializing address space, num pages %d, size %d\n",
       numPages, size);

    // first, set up the translation
    pageTable = new TranslationEntry[ADDRSPACE_PAGES_SIZE];
    numPages = ADDRSPACE_PAGES_SIZE;

    for (i = 0; i < ADDRSPACE_PAGES_SIZE; i++)
        pageTable[i].virtualPage(i);

    for (i = 0; i < (noffH.uninitData.size ? divRoundUp(noffH.uninitData.virtualAddr + noffH.uninitData.size, PageSize)
                                        : ro_code); i++){
        pageTable[i].setValid();
        pageTable[i].physicalPage(frameprovider->GetEmptyFrame());
    }
    mBrk = divRoundUp(size - 1, PageSize) * PageSize;

// then, copy in the code and data segments into memory
    if (noffH.code.size > 0) {
        DEBUG ('a', "Initializing code segment, at 0x%x, size %d\n",
        noffH.code.virtualAddr, noffH.code.size);

        ReadAtVirtual(executable, noffH.code.virtualAddr, noffH.code.size,
                noffH.code.inFileAddr, pageTable, numPages);
    }
    if (noffH.initData.size > 0) {
        DEBUG ('a', "Initializing data segment, at 0x%x, size %d\n",
        noffH.initData.virtualAddr, noffH.initData.size);

        ReadAtVirtual(executable, noffH.initData.virtualAddr, noffH.initData.size,
                noffH.initData.inFileAddr, pageTable, numPages);
    }
    
    DEBUG('p', "The read only code goes unitl %d, and the read write start at %d\n", ro_code, rw_code);
    
    for (i = 0; i < (noffH.uninitData.size ? rw_code : ro_code); i++)
        pageTable[i].setReadOnly();

}

//----------------------------------------------------------------------
// AddrSpace::~AddrSpace
//      Dealloate an address space.  Nothing for now!
//----------------------------------------------------------------------

AddrSpace::~AddrSpace ()
{
  // LB: Missing [] for delete
  // delete pageTable;
    ASSERT(countThread() == 0);

    for (unsigned int i = 0; i < ADDRSPACE_PAGES_SIZE; i++)
        if (pageTable[i].valid())
            frameprovider->ReleaseFrame(pageTable[i].physicalPage());

    DEBUG('a', "Deleting pageTable after releasing frame.\n");
    delete [] pageTable;
                
    AddrSpace* spaceThread = currentThread->space; //spoffing the space to explicitely say whom is closing
    currentThread->space = this;
    for (int i = 0; i < MAX_OPEN_FILE; i++){
        if (mFdTable[i].object){
            DEBUG('F', "Process %d has forgot to close a descriptor. Closing now...\n", mPid);
            if (mFdTable[i].type == FileDescriptor){
                fileSystem->Close((OpenFile*)mFdTable[i].object);
                delete [] mFdTable[i].pathname;
            } else
                delete ((Connection*)mFdTable[i].object);
            break;
        }
    }
    currentThread->space = spaceThread;
    
    delete [] mFdTable;
    delete fd_lock;

    AddrSpace::_ADDR_SPACE_LOCK->Acquire();
    DEBUG('a', "Removing process %d of the registered process list\n", pid());

    ListElement* e = _SPACE_LIST->getFirst();
    while (this != ((addrspace_bundle_t*)e->item)->object && (e = e->next)) {}

    ((addrspace_bundle_t*)e->item)->object = nullptr;

    AddrSpace::_ADDR_SPACE_LOCK->Release();
    DEC_REF(pid());
    AddrSpace::_ADDR_SPACE_LOCK->Acquire();

    // Waking up waiting threads from other process
    DEBUG ('t', "%d thread(s) were waiting this space to finish, notifying...\n", mThreadsWaiting->size());
    Thread* t;
    while ((t = (Thread*)mThreadsWaiting->Remove())){
        DEBUG ('t', "Waking up thread %s#%d from process %d...\n", t->getName(), t->tid(), t->space->pid());
        scheduler->ReadyToRun(t);
    }

    DEBUG('a', "Deleting list of waiting threads after waking them up.\n");
    delete mThreadsWaiting;

    DEBUG('a', "Deleting list of threads as no one is here anymore.\n");
    delete mThreadList;

    AddrSpace::_ADDR_SPACE_LOCK->Release();
  // End of modification
}

/*!
 * \param the pid of the space
 * \return the corresponding bundle
 */
addrspace_bundle_t* AddrSpace::INC_REF(SpaceId pid){
    AddrSpace::_ADDR_SPACE_LOCK->Acquire();
    ListElement* e = _SPACE_LIST->getFirst();
    while (pid != ((addrspace_bundle_t*)e->item)->pid && (e = e->next)) {}

    if (!e || !e->item){
        AddrSpace::_ADDR_SPACE_LOCK->Release();
        return nullptr;
    }

    ((addrspace_bundle_t*)e->item)->ref_cnt++;
    AddrSpace::_ADDR_SPACE_LOCK->Release();
    return (addrspace_bundle_t*)e->item;
}

/*!
 * \param the pid of the space
 */
void AddrSpace::DEC_REF(SpaceId pid){
    _ADDR_SPACE_LOCK->Acquire();

    ListElement* e = _SPACE_LIST->getFirst();
    while (pid != ((addrspace_bundle_t*)e->item)->pid && (e = e->next)) {}

    if (!e->item){
        _ADDR_SPACE_LOCK->Release();
        return;
    }

    addrspace_bundle_t* a_bundle = (addrspace_bundle_t*)e->item;
    a_bundle->ref_cnt--;

    if (a_bundle->ref_cnt <= 0){
        char found = _SPACE_LIST->Remove(a_bundle);
        DEBUG('a', "AddrSpace bundle #%d has %sbeen deleted\n", pid, (found ? "": "NOT "));
        ASSERT(!a_bundle->object);
        ASSERT(found == true);
        delete a_bundle;
    }
    _ADDR_SPACE_LOCK->Release();

}

//----------------------------------------------------------------------
// AddrSpace::InitRegisters
//      Set the initial values for the user-level register set.
//
//      We write these directly into the "machine" registers, so
//      that we can immediately jump to user code.  Note that these
//      will be saved/restored into the currentThread->userRegisters
//      when this thread is context switched out.
//----------------------------------------------------------------------

void
AddrSpace::InitRegisters ()
{
    int i;

    for (i = 0; i < NumTotalRegs; i++)
        machine->WriteRegister (i, 0);

    // Initial program counter -- must be location of "Start"
    machine->WriteRegister (PCReg, 0);

    // Need to also tell MIPS where next instruction is, because
    // of branch delay possibility
    machine->WriteRegister (NextPCReg, 4);

    ASSERT(MAX_THREADS >= countThread());
    // Set the stack register to the end of the address space, where we
    // allocated the stack; but subtract off a bit, to make sure we don't
    // accidentally reference off the end!

	int s = 0;
	while (s < MAX_THREADS && stackUsage[s] != (int)currentThread->tid() ){s++;}
	
	ASSERT(s < MAX_THREADS);
	
    machine->WriteRegister (StackReg, (ADDRSPACE_PAGES_SIZE * PageSize) - (UserStackSize * s) - 16);
    DEBUG ('a', "Initializing stack register to %d for thread #%d\n",
       (ADDRSPACE_PAGES_SIZE * PageSize) - (UserStackSize * s) - 16, currentThread->tid());
}

//----------------------------------------------------------------------
// AddrSpace::SaveState
//      On a context switch, save any machine state, specific
//      to this address space, that needs saving.
//
//      For now, nothing!
//----------------------------------------------------------------------

void AddrSpace::SaveState (){
}

/*!
 * Add a Thread to the address space
 *
 * \param t freshly created thread
 *
 */
void AddrSpace::appendThread (Thread* t){
    unsigned int stackNbPages = divRoundUp(UserStackSize, PageSize);

	int s = 0;
	while (s < MAX_THREADS && stackUsage[s] != -1 ){s++;}
		
    if (s < MAX_THREADS && mBrk <= (unsigned int)((numPages * PageSize) - (UserStackSize * (s + 1))) && frameprovider->NumAvailFrame() >= stackNbPages){
        t->space = this;
        t->setTID(lastTID++);

        thread_bundle_t* t_bundle = new thread_bundle_t;

        t_bundle->object = t;
        t_bundle->result_code = 0;
        t_bundle->tid = t->tid();
        
        int stack_first_page = numPages - (stackNbPages * (s + 1));

        mThreadList->Append(t_bundle);
        inc_ref_thread(t->tid());

        DEBUG ('a', "New thread mapped in the space as ID #%d\n", t->tid());
        DEBUG ('p', "New thread#%d use page %d to %d as stack on the whole %d (%d)\n", t->tid(), stack_first_page, stack_first_page + stackNbPages, numPages, s);

        for (unsigned int i = 0; i < stackNbPages; i++){
            if (pageTable[stack_first_page + i].valid()) continue;
            pageTable[stack_first_page + i].setValid();
            pageTable[stack_first_page + i].physicalPage(frameprovider->GetEmptyFrame());
        }
        stackUsage[s] = t->tid();
        DEBUG ('t', "Stack allocated for thread ID #%d\n", t->tid());
    } else if (countThread() == MAX_THREADS){
        DEBUG ('t', "Maximun threads number reached\n");
        t->space = NULL;
    } else{
        DEBUG ('t', "No more page free to hold a thread stack.\n");
        t->space = NULL;
    }
}
/*!
 * Remove a Thread to the address space, and notify the waiting Threads
 *
 * \param t freshly created thread
 *
 * */
void AddrSpace::removeThread(Thread* t, int result_code){
    unsigned int stackNbPages = divRoundUp(UserStackSize, PageSize);
    
    ListElement* e = mThreadList->getFirst();
    while (t != ((thread_bundle_t*)e->item)->object && (e = e->next)) {}

    DEBUG('t', "Thread #%d has %sbeen found\n", t->tid(), (e->item ? "": "NOT "));
    if (!e->item)
        return;

    thread_bundle_t* t_bundle = (thread_bundle_t*)e->item;

    t_bundle->object = nullptr;
    t_bundle->result_code = result_code;
    
	int s = 0;
	while (s < MAX_THREADS && stackUsage[s] != (int)currentThread->tid() ){s++;}
	
	ASSERT(s < MAX_THREADS);
	
	int stack_first_page = numPages - (stackNbPages * (s + 1));
    DEBUG ('p', "Dying thread#%d relase stack pages from %d to %d\n", t->tid(), stack_first_page, stack_first_page + stackNbPages);

    for (unsigned int i = 0; i < stackNbPages; i++){
        pageTable[stack_first_page + i].clearValid();
        frameprovider->ReleaseFrame(pageTable[stack_first_page + i].physicalPage());
    }
	stackUsage[s] = -1;
    
    if (countThread() == 0){ //No more thread, this result code is the space result code
        DEBUG('t', "Thread #%d is the last one, its result code %d will be the process one\n", t->tid(), result_code);
        _ADDR_SPACE_LOCK->Acquire();

        e = _SPACE_LIST->getFirst();
        while (this != ((addrspace_bundle_t*)e->item)->object && (e = e->next)) {}

        ((addrspace_bundle_t*)e->item)->result_code = result_code;

        _ADDR_SPACE_LOCK->Release();
    }


    dec_ref_thread(t->tid());
}

/*!
 * Append a Thread from another space, and notify the waiting Threads when it will be deleted
 *
 * \param t the waiting thread
 *
 * */
void AddrSpace::appendToJoin(Thread*t) {
    DEBUG ('t', "Thread %s#%d from process %d is joining the process...\n", t->getName(), t->tid(), t->space->pid());
    mThreadsWaiting->Append(t);
}

unsigned int AddrSpace::countThread() const {
    int cnt = 0;
    ListElement* e = mThreadList->getFirst();
    while (e){
        if (((thread_bundle_t*)e->item)->object)
            cnt++;
        e = e->next;
    }
    return cnt;
}

unsigned int AddrSpace::ADDR_SPACE_COUNT() {
    _ADDR_SPACE_LOCK->Acquire();

    int cnt = 0;
    ListElement* e = _SPACE_LIST->getFirst();
    while (e){
        if (((addrspace_bundle_t*)e->item)->object)
            cnt++;
        e = e->next;
    }

    _ADDR_SPACE_LOCK->Release();

    return cnt;
}

/*!
 * Block the current thread related to this space, until the given AddrSpace is deleted
 *
 * \param pid The space ID
 *
 * */
int AddrSpace::join(SpaceId s_pid, int result_code_pnt) {
#ifdef USER_PROGRAM
    ASSERT(pid() != s_pid);
    ASSERT(currentThread->space == this);

    addrspace_bundle_t* a_bundle = INC_REF(s_pid);
    if (!a_bundle){
        DEBUG('c', "JoiningProcess: %d does not exist\n", s_pid);
        return -1;
    }

    AddrSpace* process_to_join = a_bundle->object;
    if (process_to_join){
        process_to_join->appendToJoin(currentThread); // Will be elected when the thread will finish

        IntStatus oldLevel = interrupt->SetLevel (IntOff);    // disable interrupts
        currentThread->Sleep ();
        (void) interrupt->SetLevel (oldLevel);    // re-enable interrupts
    }

    DEBUG('c', "Process %d has joined with result code %d: writting it at %d\n", s_pid, a_bundle->result_code, result_code_pnt);
    if (result_code_pnt)
        machine->WriteMem(result_code_pnt, 4, a_bundle->result_code);
    DEC_REF(s_pid);
#endif
    return 0;
}

thread_bundle_t* AddrSpace::inc_ref_thread(tid_t tid){
    ListElement* e = mThreadList->getFirst();
    while (tid != ((thread_bundle_t*)e->item)->tid && (e = e->next)) {}

    if (!e->item)
        return nullptr;

    ((thread_bundle_t*)e->item)->ref_cnt++;
    return (thread_bundle_t*)e->item;
}

void AddrSpace::dec_ref_thread(tid_t tid){
    ListElement* e = mThreadList->getFirst();
    while (tid != ((thread_bundle_t*)e->item)->tid && (e = e->next)) {}

    if (!e->item)
        return;

    thread_bundle_t* t_bundle = (thread_bundle_t*)e->item;
    t_bundle->ref_cnt--;

    if (t_bundle->ref_cnt <= 0){
        char found = mThreadList->Remove(t_bundle);
        DEBUG('t', "Thread bundle #%d has %sbeen deleted\n", t_bundle->tid, (found ? "": "NOT "));
        ASSERT(found == true);
        delete t_bundle;
    }
}

fd_bundle_t* AddrSpace::get_fd(int fd){
    fd_lock->Acquire();
    fd_bundle_t* bundle = nullptr;
    for (int i = 0; i < MAX_OPEN_FILE; i++){
        if (mFdTable[i].fd == fd){
            bundle =  mFdTable + i;
            break;
        }
    }
    fd_lock->Release();
    return bundle;
}

int AddrSpace::store_fd(fd_bundle_t*bundle){
    fd_lock->Acquire();
    int i;
    for (i = 0; i < MAX_OPEN_FILE; i++){
        if (mFdTable[i].object == nullptr){
            memcpy(mFdTable + i, bundle, sizeof(fd_bundle_t));
            delete bundle;
            mFdTable[i].fd = ++lastFD;
            break;
        }
    }
    DEBUG('f', "Space#%d has allocated %p at %d\n", mPid, mFdTable[i].object, mFdTable[i].fd);
    fd_lock->Release();
    return i < MAX_OPEN_FILE ? mFdTable[i].fd : 0;
}

void AddrSpace::del_fd(int fd){
    fd_lock->Acquire();
    for (int i = 0; i < MAX_OPEN_FILE; i++){
        if (mFdTable[i].fd == fd){
            if (mFdTable[i].type == FileDescriptor)
                delete [] mFdTable[i].pathname;
            memset(mFdTable + i, 0, sizeof(fd_bundle_t));
            break;
        }            
    }
    fd_lock->Release();
}



/*!
 * Get a thread by is ID on the space
 *
 * \param tid the thread id
 * \return the thread matching with this id
 *
 * */
Thread* AddrSpace::getThread(unsigned int tid) {
    ListElement* e = mThreadList->getFirst();
    while (tid != ((thread_bundle_t*)e->item)->tid && (e = e->next)) {}
    return (e ? ((thread_bundle_t*)e->item)->object : nullptr);
}


/*!
 * Allocate new page to a process
 *
 * \param the number of page required
 * \return the address of the previous break value or zero if can't shift it
 *
 * */
int AddrSpace::Sbrk(int n){
    unsigned int stackNbPages = divRoundUp(UserStackSize, PageSize);
    
    int s;
    for (s = MAX_THREADS - 1; s >= 0 && stackUsage[s] == -1; s--){}
    
	unsigned int stackStart = numPages - (stackNbPages * (s + 1));
    DEBUG ('p', "Last stack goes from page %d to %d\n", stackStart, stackStart + stackNbPages);
    
    
    unsigned int start_page = n < 0 ? divRoundUp(mBrk, PageSize) + n : divRoundUp(mBrk, PageSize);
    unsigned int end_page = n < 0 ? divRoundUp(mBrk, PageSize) : divRoundUp(mBrk, PageSize) + n;

    if (end_page > stackStart && n > 0){
        DEBUG('p', "Trying to allocate new pages, but none available in the address space.\n");
        return 0;
    }

    if ((int)frameprovider->NumAvailFrame() < n){
        DEBUG('p', "Trying to allocate new pages, but none available in the physical memory.\n");
        return 0;
    }

    ASSERT(divRoundDown(mBrk, PageSize) == divRoundUp(mBrk, PageSize));
    
    if (start_page < 0) start_page = 0;
    if (start_page >= end_page) 
        return mBrk;

    for (unsigned int i = start_page; i < (unsigned int)end_page; i++){
        if (n < 0){
            pageTable[i].clearValid();
            frameprovider->ReleaseFrame(pageTable[i].physicalPage());
            DEBUG('P', "Address from %u(%d) to %u(%d) are now unavailable.\n", (char*)(pageTable[i].virtualPage() * PageSize), i, (char*)(pageTable[i].virtualPage() * PageSize + PageSize - 1), i + 1, n);
        } else {
            pageTable[i].setValid();
            pageTable[i].physicalPage(frameprovider->GetEmptyFrame());
            DEBUG('P', "Address from %u(%d) to %u(%d) are now available.\n", (char*)(pageTable[i].virtualPage() * PageSize), i, (char*)(pageTable[i].virtualPage() * PageSize + PageSize - 1), i + 1, n);
        }
    }
    int oldBrk = mBrk;
    DEBUG('p', "Brk has been moved from %u to %u (%d page).\n", (char*)(start_page * PageSize), (char*)(end_page * PageSize), n);
    mBrk = n >= 0 ? end_page * PageSize : start_page * PageSize;
    return oldBrk;
}

//----------------------------------------------------------------------
// AddrSpace::RestoreState
//      On a context switch, restore the machine state so that
//      this address space can run.
//
//      For now, tell the machine where to find the page table.
//----------------------------------------------------------------------

void
AddrSpace::RestoreState ()
{
    machine->pageTable = pageTable;
    machine->pageTableSize = numPages;
}
