#include <queue>
#include <iostream>
#include <cstdlib>
#define CMD_TX 2
#define QD 16


class Eloop;

class Event {
public:
  int tim;
  int type;
  int n512;
  int lba;
  Event(int tim) {
    this->tim = tim;
  };
  void run(Eloop *eloop) {
    std::cout << tim << "us "<< lba << " " << n512 << std::endl;
  }
};


bool operator<(const Event &event1, const Event event2) {
  return event1.tim < event2.tim;
}

class Eloop
{
  std::priority_queue<Event> eq;
  unsigned int sim_us;
public:
  Eloop() {
    sim_us = 0;
  }
  void add(Event event) {
    eq.push(event);
  }
  void next_req() {
    Event ev(sim_us + 10);
    ev.type = CMD_TX;
    ev.lba = rand() % 16384;
    ev.n512 = rand() & 0x1 ? 1 : 8;
    add(ev);
  }
  bool run() {
    if (eq.empty())
      return false;
    else {
      Event event = eq.top();
      event.run(this);
      eq.pop();
      return true;
    }
  }
};

Eloop *eloop;

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
