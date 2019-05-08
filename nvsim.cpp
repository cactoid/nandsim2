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
//#define QD 16

#define N_DIE 4
#define N_CH 16

#define BLK_SIZE (512*8)
//#define REQCNT (1024*4*4*4)
#define REQCNT (128*1024)

#define TRUS (10)

#define NAND_CH_MHZ (1200)
#define PCIE_LANE (32) // Gen3

#define RBUF_CAP (16 * 1024 * 1024 * 8*2*2)
//#define RBUF_CAP (16 * 1024 * 1024*1)

typedef struct {
  int id;
  int tim;
  int type;
  int n512;
  int lba;
  int die;
  int ch;
} event_t;

class MyCompare {
public:
  template<typename T>
  bool operator()(T *a, T *b) {
    return (*a) < (*b);
  }
};


#define BUF_TH (2)

int full = 0;
typedef event_t * Event;
typedef std::queue<Event> dieq_t;
typedef std::queue<Event> diebuf_q_t;
typedef std::queue<Event> chq_t;
typedef std::queue<Event> rbufq_t;
chq_t chq[N_CH];
dieq_t dieq[N_CH][N_DIE];
diebuf_q_t diebuf_q[N_CH][N_DIE];
int diebuf[N_CH][N_DIE];
std::vector<Event> diebuf_dep;
//Event *diebuf_dep[N_CH][N_DIE];
int pcie_stat = 0;
int ch_stat[N_CH];
int die_stat[N_CH][N_DIE];
int rbuf_sum = 0;
rbufq_t rbufq;
int reqcnt = 0;


int div_ceil(int x, int y)
{
  return (x + y - 1) / y;
}

void sub();
void next_req();

int done_cmd_req = 0;
int done_nand_read0 = 0;
int done_nand_read = 0;
int done_nand_ch = 0;
int id_cnt = 0;
int done_cnt = 0;



class Eloop
{
  std::priority_queue<Event, std::vector<Event>, MyCompare> eq;
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
    Event ev = (Event)malloc(sizeof(event_t));
    ev->tim = sim_ns + 100;
    ev->id = id_cnt++;
    ev->type = NVME_CMD_REQ;
    ev->lba = rand() % 16384;
    ev->die = ev->lba % N_DIE;
    ev->ch = (ev->lba / N_DIE) % N_CH;
    //ev->n512 = rand() & 0x1 ? 1 : 8;
    ev->n512 = BLK_SIZE / 512;
    add(ev);
  }
  bool run() {
    if (eq.empty())
      return false;
    else {
      Event ev = eq.top();
      sim_ns = ev->tim;

      if (ev->type == NVME_CMD_REQ) {
	//std::cout << id << " NVME_CMD_REQ " << tim << "us "<< ch << " " << die << " " << n512 << std::endl;
	dieq[ev->ch][ev->die].push(ev);
	done_cmd_req ++;
      } else if (ev->type == NAND_READ_DONE) {
	done_nand_read0 ++;
	if (diebuf_q[ev->ch][ev->die].size() < BUF_TH)
	  die_stat[ev->ch][ev->die] = 0;
	diebuf_q[ev->ch][ev->die].push(ev);
	chq[ev->ch].push(ev);
	done_nand_read ++;
      } else if (ev->type == NAND_CH_DONE) {
	rbufq.push(ev);
	rbuf_sum += ev->n512 * 512;
	ch_stat[ev->ch] = 0;
	done_nand_ch ++;
      } else if (ev->type == PCIE_DONE) {
	done_cnt ++;
	pcie_stat = 0;
	if (reqcnt < REQCNT)
	  next_req();
      }
      sub();

      eq.pop();
      //std::cout << "pop:" << eq.size() << std::endl;
      return true;
    }
  }
};



Eloop *eloop;





bool operator<(event_t event1, event_t event2) {
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
	ev->tim = eloop->sim_ns + div_ceil(ev->n512 * 512,PCIE_LANE);
	ev->type = PCIE_DONE;
	pcie_stat = 1;
	eloop->add(ev);
      }
    }
    
    //std::cout << "sub " << __LINE__ << std::endl;
    for (int i_ch=0; i_ch<N_CH; i_ch++) {
      if (ch_stat[i_ch] == 0) {
	if (chq[i_ch].size() > 0) {
	  Event ev = chq[i_ch].front();
	  if (RBUF_CAP - rbuf_sum > ev->n512 * 512) {
	    //std::cout << "go " << ev->id << std::endl;
	    diebuf_q[ev->ch][ev->die].pop();
	    if (diebuf_q[ev->ch][ev->die].size() < BUF_TH)
	      die_stat[ev->ch][ev->die] = 0;
	    ch_stat[i_ch] = 1;
	    chq[i_ch].pop();
	    ev->tim = eloop->sim_ns + div_ceil(ev->n512 * 512 * 1000, NAND_CH_MHZ);
	    ev->type = NAND_CH_DONE;
	    eloop->add(ev);
	  } else {
	    //std::cout << "full " << ev->id << std::endl;
	    full ++;
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
	    ev->tim = eloop->sim_ns + TRUS * 1000;
	    ev->type = NAND_READ_DONE;
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
  std::cout << "Paallel Die Read Limit : " << N_DIE * N_CH * 4096 / TRUS / 1000 << std::endl;
  std::cout << "Paallel NAND Ch Limit : " << N_CH * NAND_CH_MHZ / 1000.0 << std::endl;
  std::cout << "PCIe Limit : " << PCIE_LANE << std::endl;
  std::cout << "Actual GB/s : " << REQCNT * BLK_SIZE / eloop->sim_ns << std::endl;
  std::cout << "MIOPS : " << (double)REQCNT*1024 / eloop->sim_ns << std::endl;
  std::cout << "RBUF MB : " << RBUF_CAP / 1024 / 1024 << std::endl;
  std::cout << "done_cmd_req : " << done_cmd_req<< std::endl;
  std::cout << "done_nand_read : " << done_nand_read<< std::endl;
  std::cout << "done_nand_read0 : " << done_nand_read0<< std::endl;
  std::cout << "done_nand_ch : " << done_nand_ch<< std::endl;
  std::cout << "done_cnt : " << done_cnt<< std::endl;
  std::cout << "full : " << full<< std::endl;
}
