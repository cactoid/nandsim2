#include <systemc>
using namespace sc_core;

bool initialized = false;

SC_MODULE(Host) {
  sc_event h2c_req;
  sc_event h2c_idle;
  SC_CTOR(Host) {
    SC_THREAD(h);
    SC_THREAD(h2c);
  }
  void h() {
    while (1) {
      if (initialized) {
	printf("here\n");

	printf("here\n");
	h2c_req.notify();
      } else {
	wait(1, SC_US);
	initialized = true;
      }
    }
  }
  void h2c() {
    while (1) {
      h2c_idle.notify();
      printf("done here\n");
      wait(h2c_req);
      printf("done here\n");
    }
  }
};


int sc_main(int argc, char *argv[])
{
  Host host("host");

  sc_start(1000, SC_US);

  printf("hello world\n");
  return 0;
}
