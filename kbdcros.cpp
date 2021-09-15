#include "kbdcros.h"
struct input_event event;
void simulate_key(int fd,int kval)  
{  
  event.type = EV_KEY;  
  event.value = 1;  
  event.code = kval;  

  gettimeofday(&event.time,0);  
  write(fd,&event,sizeof(event)) ;  

  event.type = EV_SYN;  
  event.code = SYN_REPORT;  
  event.value = 0;  
  write(fd, &event, sizeof(event));  
  memset(&event, 0, sizeof(event));  
  gettimeofday(&event.time, NULL);  
  event.type = EV_KEY;  
  event.code = kval;  
  event.value = 0;  
  write(fd, &event, sizeof(event));  
  event.type = EV_SYN;  
  event.code = SYN_REPORT;  
  event.value = 0;  
  write(fd, &event, sizeof(event));  
}  


int main()
{
 int keys_fd;  
  struct input_event t;  
  
  keys_fd = open ("/dev/input/event11", O_RDONLY);  
  if (keys_fd <= 0)  
    {  
      printf ("open /dev/input/event11 device error!\n");  
      return 0;  
    }  
  
  while (1)  
    {  
      if (read (keys_fd, &t, sizeof (t)) == sizeof (t))  
        {  
          if (t.type == EV_KEY)  
            if (t.value == 0 || t.value == 1)  
        {  
              printf ("key %d %s\n", t.code,  
                      (t.value) ? "Pressed" : "Released");  
          if(t.code==KEY_ESC)  
              break;  
        }  
        }  
    }  
  close (keys_fd);  
  return 0;
}

