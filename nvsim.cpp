#include <queue>
#include <vector>
#include <iostream>
#include <cstdlib>


#define NVME_CMD_REQ 2
#define PCIE_CMD_TX_END 3
#define NAND_READ_DONE 4
#define NAND_CH_DONE 5
#define PCIE_DONE 6

#define QD 128*4

#define N_DIE 4
#define N_CH 32

#define REQCNT (1024*4*4*4)

#define TRUS (20)

#define NAND_CH_MHZ (800)
#define PCIE_LANE (32) // Gen3

#define RBUF_CAP (16 * 1024 * 1024 * 8)

class Event;


typedef std::queue<Event> dieq_t;
typedef std::queue<Event> chq_t;
typedef std::queue<Event> rbufq_t;
dieq_t chq[N_CH];
dieq_t dieq[N_CH][N_DIE];
int pcie_stat = 0;
int ch_stat[N_CH];
int die_stat[N_CH][N_DIE];
int rbuf_sum = 0;
rbufq_t rbufq;
int reqcnt = 0;


void sub();
void next_req();

int id_cnt = 0;

class Event {
public:
  int id;
  int tim;
  int type;
  int n512;
  int lba;
  int die;
  int ch;
  Event(int tim) {
    this->tim = tim;
    this->id = id_cnt++;
  };
  void run() {
    if (type == NVME_CMD_REQ) {
      //std::cout << id << " NVME_CMD_REQ " << tim << "us "<< ch << " " << die << " " << n512 << std::endl;
      dieq[ch][die].push(*this);
    } else if (type == NAND_READ_DONE) {
      //std::cout << id << " NAND_READ_DONE " << tim << "us "<< ch << " " << die << " " << n512 << std::endl;
      die_stat[ch][die] = 0;
      chq[ch].push(*this);
    } else if (type == NAND_CH_DONE) {
      //std::cout << id << " NAND_CH_DONE " << tim << "us "<< ch << " " << die << " " << n512 << std::endl;
      rbufq.push(*this);
      rbuf_sum += n512 * 512;
      ch_stat[ch] = 0;
    } else if (type == PCIE_DONE) {
      //std::cout << id << " PCIE_DONE " << tim << "us "<< ch << " " << die << " " << n512 << std::endl;
      pcie_stat = 0;
      if (reqcnt < REQCNT)
	next_req();
	
    }
    sub();
  }
};




class Eloop
{
  std::priority_queue<Event> eq;
public:
  unsigned int sim_ns;
  Eloop() {
    sim_ns = 0;
  }
  void add(Event event) {
    //std::cout << "ADD :" << event.tim << " " << event.type << " ch=" << event.ch << " die=" << event.die << " " << eq.size() << std::endl;
    eq.push(event);
  }
  void next_req() {
    reqcnt ++ ;
    Event ev(sim_ns + 100);
    ev.type = NVME_CMD_REQ;
    ev.lba = rand() % 16384;
    ev.die = ev.lba % N_DIE;
    ev.ch = (ev.lba / N_DIE) % N_CH;
    //ev.n512 = rand() & 0x1 ? 1 : 8;
    ev.n512 = 8;
    add(ev);
  }
  bool run() {
    if (eq.empty())
      return false;
    else {
      Event event = eq.top();
      sim_ns = event.tim;
      event.run();
      eq.pop();
      //std::cout << "pop:" << eq.size() << std::endl;
      return true;
    }
  }
};



Eloop *eloop;





bool operator<(const Event &event1, const Event event2) {
  return event1.tim > event2.tim;
}

void
sub()
{
  //std::cout << "sub " << __LINE__ << std::endl;
    if (pcie_stat == 0) {
      if (rbufq.size() > 0) {
	Event ev = rbufq.front();
	rbufq.pop();
	ev.tim = eloop->sim_ns + ev.n512 * 512 / PCIE_LANE;
	ev.type = PCIE_DONE;
	pcie_stat = 1;
	eloop->add(ev);
      }
    }
    
    //std::cout << "sub " << __LINE__ << std::endl;
    for (int i_ch=0; i_ch<N_CH; i_ch++) {
      if (ch_stat[i_ch] == 0) {
	if (chq[i_ch].size() > 0) {
	  Event ev = chq[i_ch].front();
	  if (RBUF_CAP - rbuf_sum > ev.n512 * 512) {
	    ch_stat[i_ch] = 1;
	    chq[i_ch].pop();
	    ev.tim = eloop->sim_ns + ev.n512 * 512 / NAND_CH_MHZ * 1000;
	    ev.type = NAND_CH_DONE;
	    eloop->add(ev);
	  } else {
	    std::cout << "full" << std::endl;
	  }
	}
      }
    }
    //std::cout << "sub " << __LINE__ << std::endl;
    for (int i_ch=0; i_ch<N_CH; i_ch++) {
      for (int i_die=0; i_die<N_DIE; i_die++) {
	if (die_stat[i_ch][i_die] == 0) {
	  if (dieq[i_ch][i_die].size() > 0) {
	    die_stat[i_ch][i_die] = 1;
	    Event ev = dieq[i_ch][i_die].front();
	    dieq[i_ch][i_die].pop();
	    ev.tim = eloop->sim_ns + TRUS * 1000;
	    ev.type = NAND_READ_DONE;
	    eloop->add(ev);
	  }
	}
      }
    }
    //std::cout << "sub " << __LINE__ << std::endl;

}

void
next_req()
{
  eloop->next_req();
}

int
main()
{
  eloop = new Eloop();

  for (int qd=0; qd<QD; qd++)
    eloop->next_req();
  while (eloop->run())
    ;
  std::cout << "Paallel Die Read Limit : " << N_DIE * N_CH * 4096.0 / TRUS / 1000 << std::endl;
  std::cout << "Paallel NAND Ch Limit : " << N_CH * NAND_CH_MHZ << std::endl;
  std::cout << "PCIe Limit : " << PCIE_LANE << std::endl;
  std::cout << "Actual GB/s : " << REQCNT * 4096.0 / eloop->sim_ns << std::endl;
}
