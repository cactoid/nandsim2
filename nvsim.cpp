#include <queue>



class Eloop
{
public:
  Eloop() {
  };
  void next_req()
  {
    
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
  
}
