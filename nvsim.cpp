#include <queue>
#include <vector>
#include <iostream>
#include <cstdlib>


#define NVME_CMD_REQ 2
#define PCIE_CMD_TX_END 3
#define NAND_READ_DONE 4
#define NAND_CH_DONE 5
#define PCIE_DONE 6

#define QD 4

#define N_DIE 4
#define N_CH 8

#define TRUS (60)

#define NAND_CH_MHZ (400)
#define PCIE_LANE (1) // Gen3

#define RBUF_CAP (16 * 1024 * 1024)

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


void sub();

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
      std::cout << id << " NVME_CMD_REQ " << tim << "us "<< ch << " " << die << " " << n512 << std::endl;
      dieq[ch][die].push(*this);
    } else if (type == NAND_READ_DONE) {
      std::cout << id << " NAND_READ_DONE " << tim << "us "<< ch << " " << die << " " << n512 << std::endl;
      die_stat[ch][die] = 0;
      chq[ch].push(*this);
    } else if (type == NAND_CH_DONE) {
      std::cout << id << " NAND_CH_DONE " << tim << "us "<< ch << " " << die << " " << n512 << std::endl;
      rbufq.push(*this);
      rbuf_sum += n512 * 512;
      ch_stat[ch] = 0;
    } else if (type == PCIE_DONE) {
      std::cout << id << " PCIE_DONE " << tim << "us "<< ch << " " << die << " " << n512 << std::endl;
      pcie_stat = 0;
    }
    sub();
  }
};




class Eloop
{
  std::priority_queue<Event> eq;
public:
  unsigned int sim_us;
  Eloop() {
    sim_us = 0;
  }
  void add(Event event) {
    std::cout << "ADD :" << event.tim << " " << event.type << " ch=" << event.ch << " die=" << event.die << " " << eq.size() << std::endl;
    eq.push(event);
  }
  void next_req() {
    Event ev(sim_us + 10);
    ev.type = NVME_CMD_REQ;
    ev.lba = rand() % 16384;
    ev.die = ev.lba % N_DIE;
    ev.ch = (ev.lba / N_DIE) % N_CH;
    ev.n512 = rand() & 0x1 ? 1 : 8;
    add(ev);
  }
  bool run() {
    if (eq.empty())
      return false;
    else {
      Event event = eq.top();
      sim_us = event.tim;
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
	ev.tim = eloop->sim_us + ev.n512 * 512 * 8 / PCIE_LANE / 1024;
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
	    ev.tim = eloop->sim_us + ev.n512 * 512 / NAND_CH_MHZ;
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
	    ev.tim = eloop->sim_us + TRUS;
	    ev.type = NAND_READ_DONE;
	    eloop->add(ev);
	  }
	}
      }
    }
    //std::cout << "sub " << __LINE__ << std::endl;

}


int
main()
{
  eloop = new Eloop();

  for (int qd=0; qd<QD; qd++)
    eloop->next_req();
  while (eloop->run())
    ;
}
