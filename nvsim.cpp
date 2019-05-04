#include <queue>
#include <vector>
#include <iostream>
#include <cstdlib>


#define NVME_CMD_REQ 2
#define PCIE_CMD_TX_END 3
#define NAND_READ_DONE 4
#define NAND_CH_DONE 5
#define PCIE_DONE 6

#define QD 16

#define N_DIE 4
#define N_CH 8

#define TRUS (60)

#define NAND_CH_MHZ (400)
#define PCIE_LANE (16) // Gen3

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

class Event {
public:
  int tim;
  int type;
  int n512;
  int lba;
  int die;
  int ch;
  Event(int tim) {
    this->tim = tim;
  };
  void run() {
    if (type == NVME_CMD_REQ) {
      std::cout << "NVME_CMD_REQ " << tim << "us "<< lba << " " << n512 << std::endl;
      die = lba % N_DIE;
      ch = (lba / N_DIE) % N_CH;
      dieq[ch][die].push(*this);
      std::cout << "sub" << ch << "." << die << std::endl;
    } else if (type == NAND_READ_DONE) {
      std::cout << "NAND_READ_DONE " << tim << "us "<< lba << " " << n512 << std::endl;
      die_stat[ch][die] = 0;
      chq[ch].push(*this);
    } else if (type == NAND_CH_DONE) {
      std::cout << "NAND_CH_DONE " << tim << "us "<< lba << " " << n512 << std::endl;
      rbufq.push(*this);
      rbuf_sum += n512 * 512;
      ch_stat[ch] = 0;
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
    eq.push(event);
  }
  void next_req() {
    Event ev(sim_us + 10);
    ev.type = NVME_CMD_REQ;
    ev.lba = rand() % 16384;
    ev.n512 = rand() & 0x1 ? 1 : 8;
    add(ev);
  }
  bool run() {
    if (eq.empty())
      return false;
    else {
      Event event = eq.top();
      event.run();
      eq.pop();
      return true;
    }
  }
};



Eloop *eloop;





bool operator<(const Event &event1, const Event event2) {
  return event1.tim < event2.tim;
}

void
sub()
{
    if (pcie_stat == 0) {
      if (rbuf_sum > 0) {
	Event ev = rbufq.front();
	rbufq.pop();
	ev.tim = eloop->sim_us + ev.n512 * 512 / PCIE_LANE / 8 / 1024;
	ev.type = PCIE_DONE;
	eloop->add(ev);
      }
    }
    
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
	  }
	}
      }
    }
    for (int i_ch=0; i_ch<N_CH; i_ch++) {
      for (int i_die=0; i_die<N_DIE; i_die++) {
	if (die_stat[i_ch][i_die] == 0) {
	  if (dieq[i_ch][i_die].size() > 0) {
	    std::cout << "sub3" << std::endl;
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
}
