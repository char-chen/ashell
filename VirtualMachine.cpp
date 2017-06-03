#include "VirtualMachine.h"
#include "Machine.h"
#include <list>
#include <algorithm>
#include <dlfcn.h>
#include <fcntl.h>
#include <iostream>
#include <map>
#include <stdint.h>
#include <queue>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include <cstring>
#include <sstream>

using namespace std;
#define BLOCK 0x7fffffff

struct chunk;
class MemPool;
const TVMMemoryPoolID VM_MEMORY_POOL_ID_SYSTEM = 0;

class PCB {
public:
	TVMThreadEntry entry;
	volatile TVMThreadState state; // potentially volatile
	void *param;
	SMachineContext smc;
	TVMMemorySize memsize;
	TVMThreadPriority prio;
	TVMThreadID pid;
	void *stack;
	volatile TVMTick timer;
	volatile int fileresult;

	PCB(TVMThreadEntry tentry, void *tparam, TVMMemorySize tmemsize,
		TVMThreadPriority tprio, TVMThreadID tid)
		: entry(tentry), param(tparam), memsize(tmemsize), prio(tprio), pid(tid)
	{
		VMMemoryPoolAllocate(0, memsize, &stack); //0 is the system id
		//stack = new char[memsize];
		state = VM_THREAD_STATE_DEAD;
		timer = 0;
		fileresult = BLOCK;
	}
};

class Mutex{
public:
	bool locked;
	TVMMutexID mutexID;
	PCB* owner;
	volatile TVMTick timer;
	Mutex()
	{
		locked = 0;
		timer = 0;
		owner = NULL;
	}
};

class ProcessList {
public:
	TVMMutexID mid;
	TVMThreadID id; // id assignment, to avoid chaos
	PCB *current;   // store the PCB pointers
	map<TVMThreadID, PCB *> threads;
	vector<queue<PCB *> > ready;
	vector<PCB *> sleepers;
	volatile int timecount;

	int tickms;
	vector<Mutex*> mutexs;
	PCB *idle;
	ProcessList()
	{
		id = 1; //main already take a space
		mid = 0;
		timecount = 0;
		//idle = new PCB(NULL, NULL, 0x100000, 0, -1); cannot init here
		ready.resize(4);
	}
	void addThread(PCB *task) // must use this method to addThread
	{
		task->pid = id;
		threads[id] = task; // insert pair
		id++;               // avoid chaos
	}


	void addMutex(Mutex* mu)
	{
		mu->mutexID = mid;
		mutexs.push_back(mu);
		mid++;
	}
};

ProcessList plist;

extern "C" {
	void VMUnloadModule(void);
	TVMMainEntry VMLoadModule(const char *module);
	TVMStatus VMFilePrint(int filedescriptor, const char *format, ...);

	void schedule() //schedule!!!!!!
	{
		PCB *temp = plist.current;

		if (temp->state == VM_THREAD_STATE_RUNNING) { //preeptive !!!!!
			temp->state = VM_THREAD_STATE_READY;
			plist.ready[temp->prio].push(temp);
		}

		for (int i = 3; i >= 0; i--)
			if (plist.ready[i].size() > 0) {
				plist.current = plist.ready[i].front();
				plist.ready[i].pop();
				if (plist.current->state == VM_THREAD_STATE_READY)
					break;
			}

		plist.current->state = VM_THREAD_STATE_RUNNING;
		MachineContextSwitch(&(temp->smc), &(plist.current->smc));
	}

	void callback(void *data) // timer decrement system
	{
		TMachineSignalState sig;
		MachineSuspendSignals(&sig);
		plist.timecount++;
		for (vector<PCB *>::iterator it = plist.sleepers.begin();
			it != plist.sleepers.end();) {
			(*it)->timer--;

			if ((*it)->timer == 0) {
				(*it)->state = VM_THREAD_STATE_READY;
				plist.ready[(*it)->prio].push((*it));       // push it to the ready queue.
				it = plist.sleepers.erase(it); // erase it
			}
			else
				it++;
		}
		schedule();
		MachineResumeSignals(&sig);
	}

	void idle(void *)
	{
		MachineEnableSignals();
		while (1);
	}

	TVMStatus VMThreadSleep(TVMTick tick)
	{
		TMachineSignalState sig;
		MachineSuspendSignals(&sig);

		if (tick == VM_TIMEOUT_IMMEDIATE) {
			MachineResumeSignals(&sig);
			return VM_STATUS_SUCCESS;
		}
		if (tick == VM_TIMEOUT_INFINITE) {
			MachineResumeSignals(&sig);
			return VM_STATUS_ERROR_INVALID_PARAMETER;
		}
		else if (tick == 0) {
			MachineResumeSignals(&sig);
			return VM_STATUS_SUCCESS;
		}

		plist.current->state = VM_THREAD_STATE_WAITING;
		plist.current->timer = tick;
		plist.sleepers.push_back(plist.current);
		schedule();
		MachineResumeSignals(&sig);
		return VM_STATUS_SUCCESS;
	}

	TVMStatus VMThreadID(TVMThreadIDRef threadref) {
		TMachineSignalState sigs;
		MachineSuspendSignals(&sigs);
		if (threadref == NULL) {
			MachineResumeSignals(&sigs);
			return VM_STATUS_ERROR_INVALID_PARAMETER;
		}
		*threadref = plist.current->pid;
		MachineResumeSignals(&sigs);
		return VM_STATUS_SUCCESS;
	}

	TVMStatus VMThreadState(TVMThreadID thread, TVMThreadStateRef state)
	{
		TMachineSignalState sig;
		MachineSuspendSignals(&sig);

		if (state == NULL) {
			MachineResumeSignals(&sig);
			return VM_STATUS_ERROR_INVALID_PARAMETER;
		}
		map<TVMThreadID, PCB *>::iterator it = plist.threads.find(thread);
		if (it != plist.threads.end()) {
			*state = it->second->state;
			MachineResumeSignals(&sig);
			return VM_STATUS_SUCCESS;
		}
		MachineResumeSignals(&sig);
		return VM_STATUS_ERROR_INVALID_ID;
	}

	void VMEntry(void *para) {
		MachineEnableSignals();
		TVMThreadEntry vm = ((PCB *)para)->entry;
		vm(((PCB *)para)->param);
		VMThreadTerminate(((PCB *)para)->pid);
	}

	TVMStatus VMThreadCreate(TVMThreadEntry entry, void *param,
		TVMMemorySize memsize, TVMThreadPriority prio,
		TVMThreadIDRef tid)
	{
		TMachineSignalState sig;
		MachineSuspendSignals(&sig);
		if (entry == NULL || tid == NULL) {
			MachineResumeSignals(&sig);
			return VM_STATUS_ERROR_INVALID_PARAMETER;
		}
		plist.addThread(new PCB(entry, param, memsize, prio, 0)); // addThread
		*tid = plist.id - 1; // should be the new added thread id
		MachineResumeSignals(&sig);
		return VM_STATUS_SUCCESS;
	}

	TVMStatus VMThreadActivate(TVMThreadID thread) {
		TMachineSignalState sig;
		MachineSuspendSignals(&sig);
		map<TVMThreadID, PCB *>::iterator it = plist.threads.find(thread);
		if (it != plist.threads.end()) {
			if (it->second->state != VM_THREAD_STATE_DEAD) {
				MachineResumeSignals(&sig);
				return VM_STATUS_ERROR_INVALID_STATE;
			}
			MachineContextCreate(&(it->second->smc), VMEntry, it->second,
				it->second->stack, it->second->memsize);
			it->second->state = VM_THREAD_STATE_READY;
			plist.ready[it->second->prio].push(it->second);

			if (it->second->prio > plist.current->prio) {
				schedule();
			}
			MachineResumeSignals(&sig);
			return VM_STATUS_SUCCESS;
		}
		MachineResumeSignals(&sig);
		return VM_STATUS_ERROR_INVALID_ID;
	}

	TVMStatus VMThreadDelete(TVMThreadID thread) {
		TMachineSignalState sig;
		MachineSuspendSignals(&sig);
		map<TVMThreadID, PCB *>::iterator it = plist.threads.find(thread);
		if (it != plist.threads.end()) {
			if (it->second->state != VM_THREAD_STATE_DEAD)
				return VM_STATUS_ERROR_INVALID_STATE;
			plist.threads.erase(it);
			MachineResumeSignals(&sig);
			return VM_STATUS_SUCCESS;
		}
		MachineResumeSignals(&sig);
		return VM_STATUS_ERROR_INVALID_ID;
	}

	TVMStatus VMThreadTerminate(TVMThreadID thread) {

		TMachineSignalState sig;
		MachineSuspendSignals(&sig);

		map<TVMThreadID, PCB *>::iterator it = plist.threads.find(thread);

		if (it != plist.threads.end()) {
			if (it->second->state == VM_THREAD_STATE_DEAD) {
				MachineResumeSignals(&sig);
				return VM_STATUS_ERROR_INVALID_STATE;
			}
			/////--MUTEX
			//cout << "xxx" << endl;
			for (unsigned int i = 0; i < plist.mutexs.size(); i++)
			{
				if (plist.mutexs[i] != NULL && plist.mutexs[i]->owner == it->second)
					VMMutexRelease(i);
			}
			/////
			it->second->state = VM_THREAD_STATE_DEAD;
			schedule();
			MachineResumeSignals(&sig);
			return VM_STATUS_SUCCESS;
		}
		MachineResumeSignals(&sig);
		return VM_STATUS_ERROR_INVALID_ID;
	}

	/////////////////////////////////////////
	//------------FILE SYSTEM--------------//

	void fileCallback(void *param, int result) {
		TMachineSignalState sig;
		MachineSuspendSignals(&sig);
		PCB *temp = (PCB *)param;
		temp->fileresult = result;
		temp->state = VM_THREAD_STATE_READY;
		plist.ready[temp->prio].push(temp);
		if (temp->prio > plist.current->prio) //fixed bug for preempt.so
			schedule();
		MachineResumeSignals(&sig);
	}

	/////////////////////////////////////////
	//------------MUTEX SYSTEM--------------//

	TVMStatus VMMutexCreate(TVMMutexIDRef mutexref) //just like thread mutex
	{
		TMachineSignalState sig;
		MachineSuspendSignals(&sig);
		if (mutexref == NULL) {
			MachineResumeSignals(&sig);
			return VM_STATUS_ERROR_INVALID_PARAMETER;
		}

		plist.addMutex(new Mutex());
		*mutexref = plist.mid - 1;
		MachineResumeSignals(&sig);
		return VM_STATUS_SUCCESS;
	}

	TVMStatus VMMutexDelete(TVMMutexID mutex)
	{
		TMachineSignalState sig;
		MachineSuspendSignals(&sig);
		if (mutex >= plist.mid || plist.mutexs[mutex] == NULL) {
			MachineResumeSignals(&sig);
			return VM_STATUS_ERROR_INVALID_ID;
		}
		else if (plist.mutexs[mutex]->owner != NULL) {
			MachineResumeSignals(&sig);
			return VM_STATUS_ERROR_INVALID_STATE;
		}
		delete plist.mutexs[mutex];
		plist.mutexs[mutex] = NULL;
		MachineResumeSignals(&sig);
		return VM_STATUS_SUCCESS;
	}

	TVMStatus VMMutexQuery(TVMMutexID mutex, TVMThreadIDRef ownerref) //what owner should be?
	{
		TMachineSignalState sig;
		MachineSuspendSignals(&sig);

		if (ownerref == NULL) {
			MachineResumeSignals(&sig);
			return VM_STATUS_ERROR_INVALID_PARAMETER;
		}
		if (mutex >= plist.mid || plist.mutexs[mutex] == NULL) {
			MachineResumeSignals(&sig);
			return VM_STATUS_ERROR_INVALID_ID;
		}

		if (plist.mutexs[mutex]->owner == NULL)
			*ownerref = VM_THREAD_ID_INVALID;
		else
			*ownerref = plist.mutexs[mutex]->owner->pid;
		MachineResumeSignals(&sig);
		return VM_STATUS_SUCCESS;
	}

	TVMStatus VMMutexAcquire(TVMMutexID mutex, TVMTick timeout)
	{
		TMachineSignalState sig;
		MachineSuspendSignals(&sig);
		if (mutex >= plist.mid || plist.mutexs[mutex] == NULL) {
			MachineResumeSignals(&sig);
			return VM_STATUS_ERROR_INVALID_ID;
		}
		if (plist.mutexs[mutex]->owner == NULL) { //not acquired
			plist.mutexs[mutex]->owner = plist.current;
			MachineResumeSignals(&sig);
			return VM_STATUS_SUCCESS;
		}
		else { //if is locked
			if (timeout == VM_TIMEOUT_IMMEDIATE) {
				MachineResumeSignals(&sig);
				return VM_STATUS_FAILURE;
			}

			if (timeout == VM_TIMEOUT_INFINITE)
				plist.mutexs[mutex]->timer = 0x7fffff;
			else
				plist.mutexs[mutex]->timer = timeout;


			while (plist.mutexs[mutex]->timer > 0)
			{
				if (plist.mutexs[mutex]->owner == NULL) //if succeedd
				{
					plist.mutexs[mutex]->timer = 0;
					plist.mutexs[mutex]->owner = plist.current;
					MachineResumeSignals(&sig);
					return VM_STATUS_SUCCESS;
				}
				else
				{
					plist.mutexs[mutex]->timer--;
					VMThreadSleep(1); //should be the way?
				}
			}
			if (plist.mutexs[mutex]->timer == 0 && plist.mutexs[mutex]->owner != NULL)
			{
				MachineResumeSignals(&sig);
				return VM_STATUS_FAILURE;
			}
			else {
				MachineResumeSignals(&sig);
				return VM_STATUS_SUCCESS;
			}

		}
	}

	TVMStatus VMMutexRelease(TVMMutexID mutex){
		TMachineSignalState sig;
		MachineSuspendSignals(&sig);
		if (mutex >= plist.mid || plist.mutexs[mutex] == NULL) {
			MachineResumeSignals(&sig);
			return VM_STATUS_ERROR_INVALID_ID;
		}

		if (plist.mutexs[mutex]->owner == NULL)
		{
			MachineResumeSignals(&sig);
			return VM_STATUS_ERROR_INVALID_STATE;
		}
		else {
			if (plist.mutexs[mutex]->owner != plist.current) {
				plist.mutexs[mutex]->owner = NULL;
				plist.mutexs[mutex]->timer = 0;
				MachineResumeSignals(&sig);
				return VM_STATUS_ERROR_INVALID_STATE;
			}
			else {
				plist.mutexs[mutex]->owner = NULL;
				plist.mutexs[mutex]->timer = 0;
				MachineResumeSignals(&sig);
				return VM_STATUS_SUCCESS;
			}
		}
	}

	////////preemptive
	TVMStatus VMTickMS(int *tickmsref)
	{
		TMachineSignalState sig;
		MachineSuspendSignals(&sig);
		if (tickmsref == NULL) {
			MachineResumeSignals(&sig);
			return VM_STATUS_ERROR_INVALID_PARAMETER;
		}
		*tickmsref = plist.tickms;
		MachineResumeSignals(&sig);
		return VM_STATUS_SUCCESS;
	}

	TVMStatus VMTickCount(TVMTickRef tickref)
	{
		TMachineSignalState sig;
		MachineSuspendSignals(&sig);
		if (tickref == NULL) {
			MachineResumeSignals(&sig);
			return VM_STATUS_ERROR_INVALID_PARAMETER;
		}
		*tickref = plist.timecount;
		MachineResumeSignals(&sig);
		return VM_STATUS_SUCCESS;
	}

	//////////////MEMORY SYSTEM////////////

	struct chunk
	{
		TVMMemorySize size;
		uint8_t *addr;
		chunk(TVMMemorySize tsize){
			size = tsize;
		}
	};

	class MemPool
	{
	public:
		TVMMemoryPoolID id;
		TVMMemorySize MemPoolsize;
		unsigned int left;
		list<chunk*> free;
		list<chunk*> alloc;
		uint8_t *mem;

		MemPool(TVMMemoryPoolID tid, TVMMemorySize tMemPoolsize, uint8_t *tmem) : id(tid), MemPoolsize(tMemPoolsize),
			mem(tmem)
		{
			left = tMemPoolsize; //assume
			chunk *temp = new chunk(MemPoolsize); //give all of them to free
			temp->addr = mem;
			free.push_back(temp);
		}
	};

	class MemControl
	{
	public:
		vector<MemPool*> pool;
		TVMMemoryPoolID assignID;

		MemControl()
		{
			assignID = VM_MEMORY_POOL_ID_SYSTEM;
		}

		void addMem(TVMMemorySize MemPoolsize, uint8_t* mem)
		{
			MemPool *temp = new MemPool(assignID, MemPoolsize, mem);
			pool.push_back(temp);
			assignID++;
		}
	};

	MemControl Memctr;

	TVMStatus VMMemoryPoolCreate(void *base, TVMMemorySize size,
		TVMMemoryPoolIDRef memory) {
		TMachineSignalState sig;
		MachineSuspendSignals(&sig);

		if ((base == NULL || memory == NULL) || size == 0) {
			MachineResumeSignals(&sig);
			return VM_STATUS_ERROR_INVALID_PARAMETER;
		}
		*memory = Memctr.assignID;
		Memctr.addMem(size, (uint8_t *)base);

		MachineResumeSignals(&sig);
		return VM_STATUS_SUCCESS;
	}

	TVMStatus VMMemoryPoolDelete(TVMMemoryPoolID memory) {
		TMachineSignalState sig;
		MachineSuspendSignals(&sig);

		if (Memctr.assignID < memory || Memctr.pool[memory] == NULL) {
			MachineResumeSignals(&sig);
			return VM_STATUS_ERROR_INVALID_PARAMETER;
		}
		if (!Memctr.pool[memory]->alloc.empty())
		{
			MachineResumeSignals(&sig);
			return VM_STATUS_ERROR_INVALID_STATE;
		}
		delete Memctr.pool[memory]; //don't erase it.
		Memctr.pool[memory] = NULL;
		MachineResumeSignals(&sig);
		return VM_STATUS_SUCCESS;
	}

	TVMStatus VMMemoryPoolQuery(TVMMemoryPoolID memory,
		TVMMemorySizeRef bytesleft) {
		TMachineSignalState sig;
		MachineSuspendSignals(&sig);

		if (Memctr.assignID < memory || Memctr.pool[memory] == NULL || bytesleft == NULL) {
			MachineResumeSignals(&sig);
			return VM_STATUS_ERROR_INVALID_PARAMETER;
		}

		*bytesleft = Memctr.pool[memory]->left;

		MachineResumeSignals(&sig);
		return VM_STATUS_SUCCESS;
	}

	TVMStatus VMMemoryPoolAllocate(TVMMemoryPoolID memory, TVMMemorySize size,
		void **pointer) {
		TMachineSignalState sig;
		MachineSuspendSignals(&sig);

		if (Memctr.assignID < memory || Memctr.pool[memory] == NULL || pointer == NULL || size == 0) {
			MachineResumeSignals(&sig);
			return VM_STATUS_ERROR_INVALID_PARAMETER;
		}

		if (Memctr.pool[memory]->left < size) {
			MachineResumeSignals(&sig);
			return VM_STATUS_ERROR_INSUFFICIENT_RESOURCES;
		}

		unsigned int len = (size + 63) / 64 * 64;
		chunk *temp = new chunk(len);


		//check free space and find a chunk that is in free list
		list<chunk*>::iterator it;
		for (it = Memctr.pool[memory]->free.begin(); it != Memctr.pool[memory]->free.end(); it++)
			if ((*it)->size >= len)
				break;

		if (it == Memctr.pool[memory]->free.end()) {
			MachineResumeSignals(&sig);
			return VM_STATUS_ERROR_INSUFFICIENT_RESOURCES;
		}

		Memctr.pool[memory]->left -= len;
		//update alloc list
		*pointer = (*it)->addr;
		temp->addr = (*it)->addr;
		Memctr.pool[memory]->alloc.push_back(temp);

		//update free list
		(*it)->size -= len;
		if ((*it)->size == 0)
			Memctr.pool[memory]->free.erase(it);
		else
			(*it)->addr += len; //become a smaller one

		MachineResumeSignals(&sig);
		return VM_STATUS_SUCCESS;
	}

	TVMStatus VMMemoryPoolDeallocate(TVMMemoryPoolID memory, void *pointer) {
		TMachineSignalState sig;
		MachineSuspendSignals(&sig);

		if (Memctr.assignID < memory || Memctr.pool[memory] == NULL || pointer == NULL) {
			MachineResumeSignals(&sig);
			return VM_STATUS_ERROR_INVALID_PARAMETER;
		}
		//find that pointer chunk
		list<chunk*>::iterator it;
		for (it = Memctr.pool[memory]->alloc.begin(); it != Memctr.pool[memory]->alloc.end(); it++)
			if ((*it)->addr == (uint8_t *)pointer)
				break;

		if (it == Memctr.pool[memory]->alloc.end()) { //cannnot find this pointer
			MachineResumeSignals(&sig);
			return VM_STATUS_ERROR_INVALID_PARAMETER;
		}
		//add back to free list
		chunk *temp = *it;
		Memctr.pool[memory]->left += temp->size;
		Memctr.pool[memory]->alloc.erase(it);
		//check merge
		//check
		//find closest mem chunk
		for (it = Memctr.pool[memory]->free.begin(); it != Memctr.pool[memory]->free.end();) {
			uint8_t *front = temp->size + temp->addr;
			uint8_t *back = temp->addr - (*it)->size;

			if ((*it)->addr == front) {
				temp->size += (*it)->size;
				Memctr.pool[memory]->free.erase(it++);
			}
			else if (it != Memctr.pool[memory]->free.end() && (*it)->addr == back) {
				temp->size += (*it)->size;
				temp->addr = (*it)->addr;
				Memctr.pool[memory]->free.erase(it++);
			}
			else {
				it++;
			}
		}
		Memctr.pool[memory]->free.push_back(temp); //I don't care where
		MachineResumeSignals(&sig);
		return VM_STATUS_SUCCESS;
	}

	//////////////FAT-SYSTEM//////////////
	TVMStatus VMDateTime(SVMDateTimeRef curdatetime);
	void FileRead(int filedescriptor, int start, void *data, int *length);
	void FileWrite(int filedescriptor, int start, void *data, int *length);

	typedef struct{
		uint16_t DHour : 5;
		uint16_t DMinute : 6;
		uint16_t DSecond : 5;
	} STime;

	typedef struct{
		uint16_t DYear : 7;	
		uint16_t DMonth: 4;
		uint16_t DDay : 5;
	} SDate;

	struct Entry
	{
		bool isDir;
		Entry *parent;
		SVMDirectoryEntry entry;
		uint16_t firstClus;
		vector<Entry*> subEntry;
		int permission;
		int offset;
		int dir;
		unsigned int location;
	};

	struct BPBDATA
	{
		uint16_t BytsPerSec; //two bytes
		uint8_t SecPerClus;
		uint16_t RsvdSecCnt;
		uint8_t NumFATs;
		uint16_t RootEntCnt;
		uint16_t FATSz;
		uint32_t TotSec;
	};

	class FATDISK
	{
	public:
		int rootDirSectors;
		int firstRootSector;
		int firstDataSector;
		int clusterCount;
		BPBDATA BPB;
		uint16_t *table;
		Entry *root;
		Entry *currentDir;

	};
	FATDISK FAT;
	int diskDes;
	vector<Entry *> openDir;
	vector<Entry *> openFile;

	TVMStatus VMFileClose(int filedescriptor)
	{
		TMachineSignalState sig;
		MachineSuspendSignals(&sig);
		Entry *read = openFile[filedescriptor];
		read->offset = 0;
		openFile[filedescriptor] = NULL;
		MachineResumeSignals(&sig);
		return VM_STATUS_SUCCESS;
	}

	void makeTime(SVMDateTime* temp, uint16_t time, uint16_t date)
	{
		temp->DSecond = time & 0x001F;
		temp->DMinute = (time >> 5) & 0x003F;
		temp->DHour = time >> 11;
		temp->DDay = date & 0x001F;
		temp->DMonth = (date >> 5) & 0x000F;
		temp->DYear = (date >> 9) + 1980;
	}

	void readDFS(Entry *root, unsigned int entryNum, int startPoint) 
	{
		TMachineSignalState sig;
		MachineSuspendSignals(&sig);
		int length = entryNum * 32;
		uint8_t *tmp = new uint8_t[entryNum * 32];

		FileRead(diskDes, startPoint, tmp, &length);

		for (unsigned int i = 0; i < entryNum * 32; i += 32) { //for each entry
			
			if (tmp[i] == 0x0)
				break;
			else if (tmp[i] == 0xE5)
				continue;
			else if (tmp[i + 11] != 15) { //it is SFN short name
				string name = "";
				string exten = "";
				int len = strlen((char *)(tmp + i)) > 8 ? 8 : strlen((char *)(tmp + i));
				name += string((char *)tmp + i, len);
				len = strlen((char *)(tmp + i)) > 3 ? 3 : strlen((char *)(tmp + i + 8));
				exten += string((char *)tmp + i + 8, len);
				name = name.substr(0, name.find_last_not_of(32) + 1);
				if (exten[0] != 32)
					name = name + "." + exten;
				
				Entry *newentry = new Entry;
				newentry->offset = 0;
				newentry->dir = 0;
				newentry->location = startPoint + i;

				int start = (int)i - 32;
				if (start >= 0) { //get LFN		
					string lname = "";
					if (tmp[start + 11] == 15) {
						 do { //until the end
							 char tname[26] = { 0 };
							int count = 0;
							for (int x = 1; x < 11; x++)
								if (isprint(tmp[start + x]))
									tname[count++] = tmp[start + x];
							for (int x = 14; x < 26; x++)
								if (isprint(tmp[start + x]))
									tname[count++] = tmp[start + x];
							
							for (int x = 28; x < 32; x++)
								if (isprint(tmp[start + x]))
									tname[count++] = tmp[start + x];
							
							lname = lname + string(tname, count);
							start -= 32;
						 } while (!(tmp[start+32] & 0x40));
					}
					
					memcpy(newentry->entry.DLongFileName, lname.c_str(), lname.length()); //SFN SFN SFN
					newentry->entry.DLongFileName[lname.length()] = 0;
					
				}
				name = name.substr(0, name.find_last_not_of(32) + 1);
				memcpy(newentry->entry.DShortFileName, name.data(), name.length()); //SFN SFN SFN
				newentry->entry.DShortFileName[name.length()] = 0;
				newentry->entry.DAttributes = tmp[i + 11];
				newentry->isDir = 0x10 & tmp[i + 11];
				newentry->firstClus = (tmp[i + 27] << 8) | tmp[i + 26];
				newentry->entry.DSize = (tmp[i + 31] << 24) | (tmp[i + 30] << 16) | (tmp[i + 29] << 8) | tmp[i + 28];

				///time
				newentry->entry.DCreate.DHundredth = tmp[13];
				newentry->entry.DAccess.DHundredth = 0;
				newentry->entry.DModify.DHundredth = 0;

				uint16_t date = (tmp[i + 17] << 8) | tmp[i + 16]; //create time
				uint16_t time = (tmp[i + 15] << 8) | tmp[i + 14];
				makeTime(&(newentry->entry.DCreate), time, date);

				
				date = (tmp[i + 19] << 8) | tmp[i + 18];
				makeTime(&(newentry->entry.DModify), time, date);

				time = (tmp[i + 23] << 8) | tmp[i + 22];
				date = (tmp[i + 25] << 8) | tmp[i + 24];
				makeTime(&(newentry->entry.DAccess), time, date);

				if (strcmp(newentry->entry.DShortFileName, "..") == 0)
					newentry->parent = root->parent;
				else if (strcmp(newentry->entry.DShortFileName, "..") == 0)
					newentry->parent = root;
				else
					newentry->parent = root;
				
				root->subEntry.push_back(newentry);
				if ((tmp[i + 11] & 0x10) != 0x0) { //don't support a folder contains larger than 32 entries
					if (newentry->firstClus == root->firstClus || newentry->firstClus == root->parent->firstClus)
						continue;
					
					int start = (FAT.firstDataSector + (newentry->firstClus - 2)) * FAT.BPB.BytsPerSec;
					readDFS(newentry, 32, start);
				}
			}
		}
		delete[] tmp;
		MachineResumeSignals(&sig);
	}

	void mountDisk(const char *mount)
	{
		//get bpb
		TMachineSignalState sig;
		MachineSuspendSignals(&sig);

		MachineFileOpen(mount, O_RDWR, 0644, fileCallback, plist.current);  //open fat image
		plist.current->state = VM_THREAD_STATE_WAITING;
		schedule();
		diskDes = plist.current->fileresult;

		uint8_t *shared; //one byte array
		VMMemoryPoolAllocate(1, 512, (void **)&shared); //512 bytes
		MachineFileRead(diskDes, (void*)shared, 512, fileCallback, plist.current);
		plist.current->state = VM_THREAD_STATE_WAITING;
		schedule();

		FAT.BPB.BytsPerSec = (shared[12] << 8) | shared[11]; //get two bytes
		FAT.BPB.SecPerClus = shared[13];
		FAT.BPB.RsvdSecCnt = (shared[15] << 8) | shared[14];
		FAT.BPB.NumFATs = shared[16];
		FAT.BPB.RootEntCnt = (shared[18] << 8) | shared[17];
		FAT.BPB.FATSz = (shared[23] << 8) | shared[22];
		FAT.BPB.TotSec = (shared[35] << 24) | (shared[34] << 16) | (shared[33] << 8) | shared[32];
		VMMemoryPoolDeallocate(1, shared);

		FAT.rootDirSectors = (FAT.BPB.RootEntCnt * 32) / FAT.BPB.BytsPerSec;
		FAT.firstRootSector = FAT.BPB.RsvdSecCnt + FAT.BPB.NumFATs * FAT.BPB.FATSz;
		FAT.firstDataSector = FAT.firstRootSector + FAT.rootDirSectors;
		FAT.clusterCount = (FAT.BPB.TotSec - FAT.firstDataSector) / FAT.BPB.SecPerClus;

		//get fat infomation
		FAT.table = new uint16_t[FAT.BPB.BytsPerSec*FAT.BPB.FATSz/2];
		int *length= new int; 
		*length = FAT.BPB.BytsPerSec*FAT.BPB.FATSz;
		FileRead(diskDes, FAT.BPB.RsvdSecCnt*FAT.BPB.BytsPerSec, FAT.table, length);

		//get read all file
		FAT.root = new Entry;
		FAT.currentDir = FAT.root;
		FAT.root->isDir = true;
		FAT.root->firstClus = 0;
		FAT.root->parent = FAT.root;
		memset(FAT.root->entry.DLongFileName, 0, VM_FILE_SYSTEM_MAX_PATH);
		memset(FAT.root->entry.DShortFileName, 0, VM_FILE_SYSTEM_SFN_SIZE);
		FAT.root->entry.DLongFileName[0] = '/';
		FAT.root->entry.DShortFileName[0] = '/';
		readDFS(FAT.root, FAT.BPB.RootEntCnt, FAT.firstRootSector*FAT.BPB.BytsPerSec); //get all files structure
		//all directories loaded
		openFile.push_back(NULL);
		openFile.push_back(NULL);
		openFile.push_back(NULL);
		MachineResumeSignals(&sig);
	}

	void entryToInt(Entry *myentry, uint8_t* res)
	{
		res[11] = myentry->entry.DAttributes;
		res[12] = 0;
		res[13] = myentry->entry.DCreate.DHundredth;

		uint16_t time = 0x0;
		uint16_t date = 0x0;
		date = date | myentry->entry.DCreate.DDay | (myentry->entry.DCreate.DMonth << 5) | ((myentry->entry.DCreate.DYear - 1980) << 9);
		time = time | myentry->entry.DCreate.DSecond | (myentry->entry.DCreate.DMinute << 5) | (myentry->entry.DCreate.DHour << 11);
		memcpy(res + 14, &time, 2);
		memcpy(res + 16, &date, 2);
		memcpy(res + 18, &date, 2);
		memcpy(res + 22, &time, 2);
		memcpy(res + 24, &date, 2);
		memcpy(res + 26, &(myentry->firstClus), 2);
		memcpy(res + 28, &(myentry->entry.DSize), 4);
	}

	TVMStatus VMStart(int tickms, TVMMemorySize heapsize, TVMMemorySize sharedsize, const char *mount,
		int argc, char *argv[])
	{
		TVMMainEntry vm = VMLoadModule(argv[0]);
		uint8_t *shared = (uint8_t *)MachineInitialize(sharedsize);
		plist.tickms = tickms;
		MachineRequestAlarm(tickms * 1000, callback, NULL);
		MachineEnableSignals();

		if (vm == NULL || mount == NULL) {
			VMUnloadModule();
			MachineTerminate();
			return VM_STATUS_FAILURE;
		}
		else {

			///Initialize system and shared memory to memory pool
			uint8_t *system = new uint8_t[heapsize];

			Memctr.addMem(heapsize, system); //ID == 0
			Memctr.addMem(sharedsize, shared); //ID == 1
			plist.idle = new PCB(NULL, NULL, 0x100000, 0, -1); //idle init here
			MachineContextCreate(&(plist.idle->smc), idle, NULL,
				plist.idle->stack, plist.idle->memsize);
			plist.idle->state = VM_THREAD_STATE_READY;
			plist.ready[0].push(plist.idle);
			plist.current = new PCB(NULL, NULL, 0x100000, VM_THREAD_PRIORITY_NORMAL, 0);
			plist.current->state = VM_THREAD_STATE_RUNNING;
			plist.addThread(plist.current);

			mountDisk(mount);
			
			vm(argc, argv);
			
			VMUnloadModule();
			MachineTerminate();
		}
		return VM_STATUS_SUCCESS;
	}

	TVMStatus VMFileOpen(const char *filename, int flags, int mode,
		int *filedescriptor) {
		TMachineSignalState sig;
		MachineSuspendSignals(&sig);
		if (filename == NULL || filedescriptor == NULL) {
			MachineResumeSignals(&sig);
			return VM_STATUS_ERROR_INVALID_PARAMETER;
		}
		//diskDes
		*filedescriptor = openFile.size();
		string comp(filename);
		transform(comp.begin(), comp.end(), comp.begin(), ::toupper);
		
		for (unsigned int i = 0; i < FAT.currentDir->subEntry.size(); i++) {
			
			if ( (FAT.currentDir->subEntry[i]->entry.DLongFileName[0] != 0 && strcmp(FAT.currentDir->subEntry[i]->entry.DLongFileName, filename) == 0)
				|| strcmp(FAT.currentDir->subEntry[i]->entry.DShortFileName, comp.c_str()) == 0) { //found the file
				FAT.currentDir->subEntry[i]->permission = mode;
				if ((flags & O_TRUNC) != 0x0) {
					//delete the original file
					//update FAT
					uint16_t pos = FAT.currentDir->subEntry[i]->firstClus;
					FAT.currentDir->subEntry[i]->entry.DSize = 0; //mark size as 0, I don't care date is there or not.
					while (FAT.table[pos] < 0xFFF8) { //mark other clusters as free
						pos = FAT.table[pos];
						FAT.table[pos] = 0x0000;
					}
					FAT.table[FAT.currentDir->subEntry[i]->firstClus] = 0xFFFF;
				}
				if ((flags & O_APPEND) != 0x0) {
					FAT.currentDir->subEntry[i]->offset = FAT.currentDir->subEntry[i]->entry.DSize;
				}
				openFile.push_back(FAT.currentDir->subEntry[i]);
				MachineResumeSignals(&sig);
				return VM_STATUS_SUCCESS;
			}
		}
		//cannot find the file
		if ((flags & O_CREAT) != 0x0) {
			//set up a new file
			Entry *newentry = new Entry;
			newentry->isDir = 0;
			newentry->permission = mode;
			string name(filename);
			string temp = name.substr(0, name.find('.'));
			memcpy(newentry->entry.DLongFileName, filename, strlen(filename));
			newentry->entry.DLongFileName[strlen(filename)] = 0;
			transform(temp.begin(), temp.end(), temp.begin(), ::toupper);
			if (name.length() > 8) {
				temp = temp.substr(0, 6);
				int count = 1;
				for (unsigned int i = 0; i < FAT.currentDir->subEntry.size(); i++) {
					string tcmp(FAT.currentDir->subEntry[i]->entry.DShortFileName);
					tcmp = tcmp.substr(0, 6);
					if (strcmp(tcmp.c_str(), temp.c_str()) == 0)
						count++;
				}
				stringstream ss;
				string s;
				ss << count;
				ss >> s;
				temp += "~" + s;
			}
			string exten;

			if ((int)name.find('.') != -1) {
				exten = name.substr(name.find('.') + 1, name.length() - name.find('.'));
				name = temp + name.substr(name.find('.'), name.length() - name.find('.'));
			}

			transform(name.begin(), name.end(), name.begin(), ::toupper);
			memcpy(newentry->entry.DShortFileName, name.c_str(), strlen(filename));;
			newentry->entry.DShortFileName[strlen(filename)] = 0;
			newentry->parent = FAT.currentDir;
			newentry->entry.DSize = 0;
			newentry->entry.DAttributes = 0;
			VMDateTime(&(newentry->entry.DCreate));
			VMDateTime(&(newentry->entry.DAccess));
			VMDateTime(&(newentry->entry.DModify));

			//write back to FAT, allocate one cluster first
			for (unsigned int i = 0; i < FAT.BPB.FATSz * FAT.BPB.BytsPerSec / 2; i++) {
				if (FAT.table[i] == 0x0000){
					newentry->firstClus = i;
					FAT.table[i] = 0xFFFF;
					int y = 2;
					FileWrite(diskDes, FAT.BPB.RsvdSecCnt*FAT.BPB.BytsPerSec + i * 2, FAT.table+i, &y);
					break;
				}
			}
			//build an entry file
			//longentry
			uint8_t *longentry = new uint8_t[32];
			longentry[0] = 0x41;
			longentry[11] = 15;
			memset(longentry + 1, 0, 10);
			memset(longentry + 14, 0, 12);
			memset(longentry + 28, 0, 4);
			string x(newentry->entry.DLongFileName);
			int len2 = x.length() > 10 ? 10 : x.length();
			memcpy(longentry + 1, x.c_str(), len2);

			if (x.length() > 10) {
				x = x.substr(10, x.length() - 9);
				int len = x.length() > 12 ? 12 : x.length();
				memcpy(longentry + 14, x.c_str(), len);
			}

			if (x.length() > 12) {
				x = x.substr(12, x.length() - 11);
				int len = x.length() > 4 ? 4 : x.length();
				memcpy(longentry + 28, x.c_str(), len);
			}

			//shortentry
			uint8_t *shortentry = new uint8_t[32];
			memcpy(shortentry, temp.c_str(), 8);
			transform(exten.begin(), exten.end(), exten.begin(), ::toupper);
			memcpy(shortentry + 8, exten.c_str(), 3);
			shortentry[11] = 0;
			entryToInt(newentry, shortentry);
			newentry->offset = 0;
			int length = FAT.BPB.RootEntCnt * 32;
			uint8_t *tmp = new uint8_t[FAT.BPB.RootEntCnt * 32];

			FileRead(diskDes, FAT.firstRootSector*FAT.BPB.BytsPerSec, tmp, &length);

			//update current cluster or root
			if (FAT.currentDir == FAT.root) {
				FAT.root->subEntry.push_back(newentry);
				for (unsigned int i = 0; i < FAT.BPB.RootEntCnt *32 ; i+=32) {
					if (tmp[i] == 0x00) {
						newentry->location = FAT.firstRootSector*FAT.BPB.BytsPerSec + i + 32;
						int y = 32;
						FileWrite(diskDes, FAT.firstRootSector*FAT.BPB.BytsPerSec + i, longentry, &y);
						FileWrite(diskDes, FAT.firstRootSector*FAT.BPB.BytsPerSec + i + 32, shortentry, &y);
						break;
					}
				}

			}
			else {
				int clus = FAT.currentDir->firstClus;
				FAT.currentDir->subEntry.push_back(newentry);
				for (unsigned int i = 0; i < FAT.BPB.BytsPerSec*2; i += 32) {
					if (tmp[i] == 0x00) {
						newentry->location = (FAT.firstDataSector+(clus -2)*2) * FAT.BPB.BytsPerSec + i + 32;
						int y = 32;
						FileWrite(diskDes, newentry->location - 32, longentry, &y);
						FileWrite(diskDes, newentry->location, shortentry, &y);
						break;
					}
				}
			}
			delete[] tmp;
			openFile.push_back(newentry);
			MachineResumeSignals(&sig);
			return VM_STATUS_SUCCESS;
		}
		else {
			MachineResumeSignals(&sig);
			return VM_STATUS_FAILURE;
		}

	}

	TVMStatus VMFileWrite(int filedescriptor, void *data, int *length) {
		TMachineSignalState sig;
		MachineSuspendSignals(&sig);
        
		if (data == NULL || length == NULL) {
			MachineResumeSignals(&sig);
			return VM_STATUS_ERROR_INVALID_PARAMETER;
		}
		if (filedescriptor < 3) {
			//shared ID == 1 and the maxium size is 512
			int remaining = *length;
			PCB *temp = plist.current;
			temp->fileresult = BLOCK;
			void* shared;
			remaining < 512 ? VMMemoryPoolAllocate(1, remaining, &shared) : VMMemoryPoolAllocate(1, 512, &shared);

			while (remaining > 512) {
				memcpy(shared, data, 512);
				MachineFileWrite(filedescriptor, shared, 512, fileCallback, plist.current);
				temp->state = VM_THREAD_STATE_WAITING;
				schedule();
				remaining -= 512;
				data = (char*)data + 512;//((unsigned int *)(data) + 512);
			}
			memcpy(shared, data, remaining);
			MachineFileWrite(filedescriptor, shared, remaining, fileCallback, plist.current);
			temp->state = VM_THREAD_STATE_WAITING;
			schedule();
			VMMemoryPoolDeallocate(1, shared);
			if (temp->fileresult < 0) {
				MachineResumeSignals(&sig);
				return VM_STATUS_FAILURE;
			}
			else {
				MachineResumeSignals(&sig);
				return VM_STATUS_SUCCESS;
			}
		}
		else {
            cout << "write" << endl;
			Entry *read;
			if (filedescriptor < (int)openFile.size() && openFile[filedescriptor] != NULL) {
				read = openFile[filedescriptor];
				int clus = read->firstClus;
				int os = read->offset;
				int len = *length;
				read->entry.DSize += len; //update size
				
				int y = 4;
				FileWrite(diskDes, read->location + 28, &(read->entry.DSize), &y);
				//first of all, determine where is our offset
				while (os > FAT.BPB.BytsPerSec * FAT.BPB.SecPerClus) { //aussuming offset won't go nowhere
					
					clus = FAT.table[clus];
					os -= FAT.BPB.BytsPerSec * FAT.BPB.SecPerClus;
				}
				
				//then write data to it.
				int start = (FAT.firstDataSector + (clus- 2)*2) * FAT.BPB.BytsPerSec + os;
				if ((1024 - os) >= len) {
					int y = len;
					FileWrite(diskDes, start, data, &y);
				}
				else {
					int y = 1024 - os;
					FileWrite(diskDes, start, data, &y);
					data = (char*)data + (1024 - len);
					len -= 1024 - os;
					while (len > 0) {
						//if no free size
						if (FAT.table[clus] >= 0xFFF8 && FAT.table[clus] != 0x00) {
							int i = 1;
							while (FAT.table[i] != 0x00)
								i++;
							FAT.table[clus] = i;
							clus = i;
							FAT.table[clus] = 0xFFFF; //new value;
							int y = FAT.BPB.NumFATs * FAT.BPB.FATSz * FAT.BPB.BytsPerSec;
							FileWrite(diskDes, FAT.BPB.RsvdSecCnt*FAT.BPB.BytsPerSec , FAT.table, &y);
						}
						else
							clus = FAT.table[clus];

						start = (FAT.firstDataSector + (clus - 2)*2) * FAT.BPB.BytsPerSec;
						if (len >= 1024) {
							int y = 1024;
							FileWrite(diskDes, start, data, &y);
							len -= 1024;
							data = (char*)data + 1024;
						}
						else {
							int y = len;
							FileWrite(diskDes, start, data, &y);
							len -= len;
							data = (char*)data + len;
						}
					}
				}
			}
			
			MachineResumeSignals(&sig);
			return VM_STATUS_SUCCESS;
		}
	}

	TVMStatus VMFileRead(int filedescriptor, void *data, int *length)
	{
		TMachineSignalState sig;
		MachineSuspendSignals(&sig);
		if (data == NULL || length == NULL) {
			MachineResumeSignals(&sig);
			return VM_STATUS_ERROR_INVALID_PARAMETER;
		}

		if (filedescriptor < 3) {
			//shared ID == 1 and the maxium size is 512
			int remaining = *length;
			PCB *temp = plist.current;
			temp->fileresult = BLOCK;
			void* shared;
			remaining < 512 ? VMMemoryPoolAllocate(1, remaining, &shared) : VMMemoryPoolAllocate(1, 512, &shared);
			int res = 0;

			while (remaining > 512) {
				MachineFileRead(filedescriptor, shared, 512, fileCallback, temp);
				plist.current->state = VM_THREAD_STATE_WAITING;
				schedule();
				memcpy(data, shared, 512);
				remaining -= 512;
				data = (char*)data + 512; //((unsigned int *)(data) + 512);
				res += temp->fileresult;
			}
			MachineFileRead(filedescriptor, shared, remaining, fileCallback, temp);
			temp->state = VM_THREAD_STATE_WAITING;
			schedule();
			memcpy(data, shared, remaining);
			*length = res + temp->fileresult;
			VMMemoryPoolDeallocate(1, shared);

			if (temp->fileresult < 0) {
				MachineResumeSignals(&sig);
				return VM_STATUS_FAILURE;
			}
			else {
				MachineResumeSignals(&sig);
				return VM_STATUS_SUCCESS;
			}
		}
		else {
            
			Entry *read;
			if (filedescriptor < (int)openFile.size() && openFile[filedescriptor] != NULL) {
				read = openFile[filedescriptor];
				int clus = read->firstClus;
				int os = read->offset;
				if (*length > (int)read->entry.DSize - os)
					*length = read->entry.DSize - os;

				read->offset += *length;
				int len = *length;

				while (os > FAT.BPB.BytsPerSec * FAT.BPB.SecPerClus) { //aussuming offset won't go nowhere
					clus = FAT.table[clus];
					os -= FAT.BPB.BytsPerSec * FAT.BPB.SecPerClus;
				}

				int start = (FAT.firstDataSector + (clus - 2)*2) * FAT.BPB.BytsPerSec + os;
                
				if ((1024 - os) >= len) {
					int y = len;
					FileRead(diskDes, start, data, &y);
					data = (char *)data + len;
				}
				else {
					int y = 1024 - os;
					FileRead(diskDes, start, data, &y);
					data = (char*)data + (1024 - os);
					len -= 1024 - os;
					while (len > 0) {
						clus = FAT.table[clus];
						start = (FAT.firstDataSector + (clus - 2)*2) * FAT.BPB.BytsPerSec;
						if (len >= 1024) {
							int y = 1024;
							FileRead(diskDes, start, data, &y);
							len -= 1024;
							data = (char*)data + 1024;
						}
						else {
							int y = len;
							FileRead(diskDes, start, data, &y);
							len -= len;
							data = (char*)data + len;
						}
					}
				}
				MachineResumeSignals(&sig);
				return VM_STATUS_SUCCESS;
			} 
			else {
				MachineResumeSignals(&sig);
				return VM_STATUS_FAILURE;
			}
			
		}

	}

	void FileRead(int filedescriptor, int start, void *data, int *length)
	{
        
		MachineFileSeek(filedescriptor, start, 0, fileCallback, plist.current);
		plist.current->state = VM_THREAD_STATE_WAITING;
		schedule();
        
		int remaining = *length;
		PCB *temp = plist.current;
		temp->fileresult = BLOCK;
		void* shared;
		remaining < 512 ? VMMemoryPoolAllocate(1, remaining, &shared) : VMMemoryPoolAllocate(1, 512, &shared);
		int res = 0;
        
		while (remaining > 512) {
			MachineFileRead(filedescriptor, shared, 512, fileCallback, temp);
			plist.current->state = VM_THREAD_STATE_WAITING;
			schedule();
			memcpy(data, shared, 512);
			remaining -= 512;
			data = (char*)data + 512;//((unsigned int *)(data) + 512);
			res += temp->fileresult;
		}
        cout << "xxx" << endl;
		MachineFileRead(filedescriptor, shared, remaining, fileCallback, temp);
		temp->state = VM_THREAD_STATE_WAITING;
		schedule();
		memcpy(data, shared, remaining);
		*length = res + temp->fileresult;
		VMMemoryPoolDeallocate(1, shared);

		MachineFileSeek(filedescriptor, 0, 0, fileCallback, plist.current); //restore back
		plist.current->state = VM_THREAD_STATE_WAITING;
		schedule();
	}

	void FileWrite(int filedescriptor, int start, void *data, int *length)
	{
		MachineFileSeek(filedescriptor, start, 0, fileCallback, plist.current);
		plist.current->state = VM_THREAD_STATE_WAITING;
		schedule();

		int remaining = *length;
		PCB *temp = plist.current;
		temp->fileresult = BLOCK;
		void* shared;
		remaining < 512 ? VMMemoryPoolAllocate(1, remaining, &shared) : VMMemoryPoolAllocate(1, 512, &shared);

		while (remaining > 512) {
			memcpy(shared, data, 512);
			MachineFileWrite(filedescriptor, shared, 512, fileCallback, plist.current);
			temp->state = VM_THREAD_STATE_WAITING;
			schedule();
			remaining -= 512;
			data = (char*)data + 512;//((unsigned int *)(data) + 512);
		}
		memcpy(shared, data, remaining);
		MachineFileWrite(filedescriptor, shared, remaining, fileCallback, plist.current);
		temp->state = VM_THREAD_STATE_WAITING;
		schedule();
		VMMemoryPoolDeallocate(1, shared);

	}

	TVMStatus VMFileSeek(int filedescriptor, int offset, int whence,
		int *newoffset) {
		TMachineSignalState sig;
		MachineSuspendSignals(&sig);
		if (filedescriptor < 3) {
			PCB *temp = plist.current;
			temp->fileresult = BLOCK;
			MachineFileSeek(filedescriptor, offset, whence, fileCallback, temp);
			plist.current->state = VM_THREAD_STATE_WAITING;
			schedule();
			MachineResumeSignals(&sig);
			if (temp->fileresult < 0)
				return VM_STATUS_FAILURE;
			else {
				if (newoffset != NULL)
					*newoffset = temp->fileresult;
				return VM_STATUS_SUCCESS;
			}
		}
		else {
			Entry *read;
			if (filedescriptor < (int)openFile.size() && openFile[filedescriptor] != NULL) {
				read = openFile[filedescriptor];
				if (whence == 0) {
					read->offset = offset;
					*newoffset = read->offset;
				}
			}
		}
		MachineResumeSignals(&sig);
		return VM_STATUS_SUCCESS;
	}

	TVMStatus VMDirectoryOpen(const char *dirname, int *dirdescriptor)
	{
		TMachineSignalState sig;
		MachineSuspendSignals(&sig);

		if (dirname == NULL || dirdescriptor == NULL){
			MachineResumeSignals(&sig);
			return VM_STATUS_ERROR_INVALID_PARAMETER;	
		}
		
		*dirdescriptor = openDir.size();
		if (dirname[0] != '/') { //relative path
			string comp(dirname);
			transform(comp.begin(), comp.end(), comp.begin(), ::toupper);
			//assume current directory
			for (unsigned int i = 0; i < FAT.currentDir->subEntry.size(); i++) {
				if ((FAT.currentDir->subEntry[i]->entry.DLongFileName [0] != 0 && strcmp(FAT.currentDir->subEntry[i]->entry.DLongFileName, dirname) == 0)
					|| strcmp(FAT.currentDir->subEntry[i]->entry.DShortFileName, comp.c_str()) == 0) { //found the file
					if (FAT.currentDir->subEntry[i]->isDir != 0x0) {
						FAT.currentDir->subEntry[i]->dir = 0;
						openDir.push_back(FAT.currentDir->subEntry[i]);
						MachineResumeSignals(&sig);
						return VM_STATUS_SUCCESS;
					}
				}
			}
		}
		else { //absolute path contains "/"
			string temp(dirname);
			if (temp == "/") {
				FAT.root->dir = 0;
				openDir.push_back(FAT.root);
				MachineResumeSignals(&sig);
				return VM_STATUS_SUCCESS;
			}
			else {
				string folders[20];
				int count = 0;
				while ((int)temp.find_first_of('/') != -1) {
					temp = temp.substr(1, temp.length() - 1);
					folders[count++] = temp.substr(0, temp.find_first_of('/'));
					if ((int)temp.find_first_of('/') == -1)
						break;
					temp = temp.substr(temp.find_first_of('/'), temp.length() - temp.find_first_of('/'));
				}
				Entry *now = FAT.root;
				for (int i = 0; i < count; i++) {
					Entry *test = now;
					string comp = folders[i];
					transform(comp.begin(), comp.end(), comp.begin(), ::toupper);
					for (unsigned int j = 0; j < now->subEntry.size(); j++) {
						if ((now->subEntry[j]->entry.DLongFileName [0] != 0 && strcmp(now->subEntry[j]->entry.DLongFileName, folders[i].c_str()) == 0)
							|| strcmp(now->subEntry[j]->entry.DShortFileName, comp.c_str()) == 0) { //found the file
							if (now->subEntry[j]->isDir != 0x0) {
								now = now->subEntry[j];
								break;
							}
						}
					}
					if (now == test) {
						MachineResumeSignals(&sig);
						return VM_STATUS_FAILURE;
					}
				}
				now->dir = 0;
				openDir.push_back(now);
				MachineResumeSignals(&sig);
				return VM_STATUS_SUCCESS;
			}
		}
		//cannot find
		MachineResumeSignals(&sig);
		return VM_STATUS_FAILURE;
	}

	TVMStatus VMDirectoryClose(int dirdescriptor)
	{
		TMachineSignalState sig;
		MachineSuspendSignals(&sig);
		Entry *read = openDir[dirdescriptor];
		read->dir = 0;
		openDir[dirdescriptor] = NULL;
		MachineResumeSignals(&sig);
		return VM_STATUS_SUCCESS;
	}

	TVMStatus VMDirectoryRead(int dirdescriptor, SVMDirectoryEntryRef dirent)
	{
		TMachineSignalState sig;
		MachineSuspendSignals(&sig);

		if (dirent == NULL) {
			MachineResumeSignals(&sig);
			return VM_STATUS_ERROR_INVALID_PARAMETER;
		}
		
		Entry *read = openDir[dirdescriptor];
		read->dir++;
		if (read->dir <= (int)read->subEntry.size()) {
			*dirent = read->subEntry[read->dir - 1]->entry;
			MachineResumeSignals(&sig);
			return VM_STATUS_SUCCESS;
		}
		else {
			MachineResumeSignals(&sig);
			return VM_STATUS_FAILURE;
		}
	}

	TVMStatus VMDirectoryRewind(int dirdescriptor)
	{
		TMachineSignalState sig;
		MachineSuspendSignals(&sig);

		Entry *read = openDir[dirdescriptor];
		if (read == NULL) {
			MachineResumeSignals(&sig);
			return VM_STATUS_FAILURE;
		}
		else {
			read->dir = 0;
			MachineResumeSignals(&sig);
			return VM_STATUS_SUCCESS;
		}
	}

	TVMStatus VMDirectoryCurrent(char *abspath)
	{
		TMachineSignalState sig;
		MachineSuspendSignals(&sig);

		if (abspath == NULL) {
			MachineResumeSignals(&sig);
			return VM_STATUS_ERROR_INVALID_PARAMETER;
		}
		memset(abspath, 0, strlen(abspath));
		string path(FAT.currentDir->entry.DLongFileName);
		Entry * temp = FAT.currentDir;
		while (temp != FAT.root) {
			temp = temp->parent;
			if (temp == FAT.root)
				path = "/" + path;
			else
				path = string(temp->entry.DLongFileName) + "/" + path;
		}
        
		memcpy(abspath, path.c_str(), path.length());
        abspath[path.length()] = 0;
		MachineResumeSignals(&sig);
		return VM_STATUS_SUCCESS;
	}

	TVMStatus VMDirectoryChange(const char *path)
	{
		TMachineSignalState sig;
		MachineSuspendSignals(&sig);

		if (path[0] == '/') {
			string temp(path);
			if (temp == "/") {
				FAT.currentDir = FAT.root;
				MachineResumeSignals(&sig);
				return VM_STATUS_SUCCESS;
			}
			else {
				string folders[20];
				int count = 0;
				while ((int)temp.find_first_of('/') != -1) {
					temp = temp.substr(1, temp.length() - 1);
					folders[count++] = temp.substr(0, temp.find_first_of('/'));
					if ((int)temp.find_first_of('/') == -1)
						break;
					temp = temp.substr(temp.find_first_of('/'), temp.length() - temp.find_first_of('/'));
				}
				Entry *now = FAT.root;
				for (int i = 0; i < count; i++) {
					Entry *test = now;
					string comp = folders[i];
					transform(comp.begin(), comp.end(), comp.begin(), ::toupper);
					for (unsigned int j = 0; j < now->subEntry.size(); j++) {
						if ((now->subEntry[j]->entry.DLongFileName [0] != 0 && strcmp(now->subEntry[j]->entry.DLongFileName, folders[i].c_str()) == 0)
							|| strcmp(now->subEntry[j]->entry.DShortFileName, comp.c_str()) == 0) { //found the file
							if (FAT.currentDir->subEntry[i]->isDir != 0x0) {
								now = now->subEntry[j];
								break;
							}
						}
					}
					if (now == test) {
						MachineResumeSignals(&sig);
						return VM_STATUS_FAILURE;
					}
				}
				FAT.currentDir = now;
				MachineResumeSignals(&sig);
				return VM_STATUS_SUCCESS;
			}
		}
		else { //current Dir
			string comp(path);
			transform(comp.begin(), comp.end(), comp.begin(), ::toupper);
			for (unsigned int i = 0; i < FAT.currentDir->subEntry.size(); i++) {
				if ((FAT.currentDir->subEntry[i]->entry.DLongFileName[0] != 0 && strcmp(FAT.currentDir->subEntry[i]->entry.DLongFileName, path) == 0)
					|| strcmp(FAT.currentDir->subEntry[i]->entry.DShortFileName, comp.c_str()) == 0) { //found the file
					if (FAT.currentDir->subEntry[i]->isDir != 0x0) {
						FAT.currentDir = FAT.currentDir->subEntry[i];
						MachineResumeSignals(&sig);
						return VM_STATUS_SUCCESS;
					}
				}
			}
			MachineResumeSignals(&sig);
			return VM_STATUS_FAILURE;
		}
	}

	TVMStatus VMDirectoryCreate(const char *dirname)
	{
		TMachineSignalState sig;
		MachineSuspendSignals(&sig);

		//transfrom absolute path

		//done

		//set up a new file
		Entry *newentry = new Entry;
		newentry->isDir = 1;
		string name(dirname);
		
		string temp = name.substr(0, name.find('.'));
		memcpy(newentry->entry.DLongFileName, dirname, strlen(dirname));
		newentry->entry.DLongFileName[strlen(dirname)] = 0;
		transform(temp.begin(), temp.end(), temp.begin(), ::toupper);
		if (name.length() > 8) {
			temp = temp.substr(0, 6);
			int count = 1;
			for (unsigned int i = 0; i < FAT.currentDir->subEntry.size(); i++) {
				string tcmp(FAT.currentDir->subEntry[i]->entry.DShortFileName);
				tcmp = tcmp.substr(0, 6);
				if (strcmp(tcmp.c_str(), temp.c_str()) == 0)
					count++;
			}
			stringstream ss;
			string s;
			ss << count;
			ss >> s;
			temp += "~" + s;
		}
		string exten = "";
		if ((int)name.find('.') != -1) {
			exten = name.substr(name.find('.') + 1, name.length() - name.find('.'));
			name = temp + name.substr(name.find('.'), name.length() - name.find('.'));
		}
		
		transform(name.begin(), name.end(), name.begin(), ::toupper);
		memcpy(newentry->entry.DShortFileName, name.c_str(), strlen(dirname));;
		newentry->entry.DShortFileName[strlen(dirname)] = 0;
		newentry->parent = FAT.currentDir;
		newentry->entry.DSize = 0;
		newentry->entry.DAttributes = 0;
		VMDateTime(&(newentry->entry.DCreate));
		VMDateTime(&(newentry->entry.DAccess));
		VMDateTime(&(newentry->entry.DModify));

		//write back to FAT, allocate one cluster first
		for (unsigned int i = 0; i < FAT.BPB.FATSz * FAT.BPB.BytsPerSec / 2; i++) {
			if (FAT.table[i] == 0x0000){
				newentry->firstClus = i;
				FAT.table[i] = 0xFFFF;
				int y = 2;
				FileWrite(diskDes, FAT.BPB.RsvdSecCnt*FAT.BPB.BytsPerSec + i * 2, FAT.table + i, &y);
				break;
			}
		}
		//build an entry file
		//longentry
		uint8_t *longentry = new uint8_t[32];
		longentry[0] = 0x41;
		longentry[11] = 15;
		memset(longentry + 1, 0, 10);
		memset(longentry + 14, 0, 12);
		memset(longentry + 28, 0, 4);
		string x(newentry->entry.DLongFileName);
		int len2 = x.length() > 10 ? 10 : x.length();
		memcpy(longentry + 1, x.c_str(), len2);

		if (x.length() > 10) {
			x = x.substr(10, x.length() - 9);
			int len = x.length() > 12 ? 12 : x.length();
			memcpy(longentry + 14, x.c_str(), len);
		}

		if (x.length() > 12) {
			x = x.substr(12, x.length() - 11);
			int len = x.length() > 4 ? 4 : x.length();
			memcpy(longentry + 28, x.c_str(), len);
		}

		//shortentry
		uint8_t *shortentry = new uint8_t[32];
		memcpy(shortentry, temp.c_str(), 8);
		transform(exten.begin(), exten.end(), exten.begin(), ::toupper);
		memcpy(shortentry + 8, exten.c_str(), 3);
		
		entryToInt(newentry, shortentry);
		shortentry[11] = 0x10;
		newentry->entry.DAttributes = 0x10;
		newentry->offset = 0;
		int length = FAT.BPB.RootEntCnt * 32;


		//update current cluster or root
		if (FAT.currentDir == FAT.root) {
			FAT.root->subEntry.push_back(newentry);
			uint8_t *tmp = new uint8_t[FAT.BPB.RootEntCnt * 32];
			FileRead(diskDes, FAT.firstRootSector*FAT.BPB.BytsPerSec, tmp, &length);
			for (unsigned int i = 0; i < FAT.BPB.RootEntCnt * 32; i += 32) {
				if (tmp[i] == 0x00) {
					newentry->location = FAT.firstRootSector*FAT.BPB.BytsPerSec + i + 32;
					int y = 32;
					FileWrite(diskDes, FAT.firstRootSector*FAT.BPB.BytsPerSec + i, longentry, &y);
					FileWrite(diskDes, FAT.firstRootSector*FAT.BPB.BytsPerSec + i + 32, shortentry, &y);
					break;
				}
			}
			delete[] tmp;
		}
		else {
			int clus = FAT.currentDir->firstClus;
			FAT.currentDir->subEntry.push_back(newentry);
			uint8_t *tmp = new uint8_t[FAT.BPB.RootEntCnt * 32];
			FileRead(diskDes, FAT.firstRootSector*FAT.BPB.BytsPerSec, tmp, &length);
			for (unsigned int i = 0; i < FAT.BPB.BytsPerSec * 2; i += 32) {
				if (tmp[i] == 0x00) {
					newentry->location = (FAT.firstDataSector + (clus - 2) * 2) * FAT.BPB.BytsPerSec + i + 32;
					int y = 32;
					FileWrite(diskDes, newentry->location - 32, longentry, &y);
					FileWrite(diskDes, newentry->location, shortentry, &y);
					break;
				}
			}
			delete[] tmp;
		}
		

		MachineResumeSignals(&sig);
		return VM_STATUS_SUCCESS;
		MachineResumeSignals(&sig);
		return VM_STATUS_SUCCESS;
	}

	TVMStatus VMDirectoryUnlink(const char *path)
	{
		TMachineSignalState sig;
		MachineSuspendSignals(&sig);


		MachineResumeSignals(&sig);
		return VM_STATUS_SUCCESS;
	}
}

