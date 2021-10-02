// SEE LICENSE file for copyright and license details.

//Macrowhale kbdmacro manager is designed to help jingos to reduce the
//imcompabilities which comes when jingos makes adaptations to
//different programs which has't make adaptations by themselves. Macro
//is a string of kbd events. It can conceive a lot of thing with only
//one click.

//This receives events from keyboard devices. which will construct an
//array, we don't make driver for devices. we reorgnize kbd events
//into a more specific form. So we can write this form data structure
//into a config file. And later translate config file into specific
//keyboard events.

  
//Macros has only two mode, one is common mode, another is application
//mode. They don't perform differently. They just have different kind
//of name. So that we will arrange them in GUI application more
//beautiful.

//We won't record which wayland window macro recorded in. And we don't
//offer network access in this program. So that your privacy won't be
//violated.
#include <linux/input.h>
#include <linux/uinput.h>
#include <linux/ioctl.h>
#include <stdio.h>
#include <signal.h>
#include <assert.h>
#include <fcntl.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <stdarg.h>
#include <pthread.h>
/* MACROS */
#define STD_DURATION 0.05
#define STD_STEP 0.1
#define KEY_ALL 250
// Duration means how much time a stdkey event last
// STEP means how much time between two stdkey events


/* ENUMS */
enum {StdType, StdcplxType, RawType};
enum {Key1, Key2, Key3, Key4, Key5, Key6, Key7, Key8};

/* STRUCT and VARIABLE */
static int keys_fd;
static int uinput_fd = -1;

struct input_event event;
struct uinput_setup usetup;
struct stdkeynode
{
  int ktype;
  int val;
  struct stdkeynode *next;
};

struct stdcomplicatedkeynode
{
  int ktype;
  int keys[8]={0,0,0,0,0,0,0,0}; // short may be better [todo]
  struct stdcomplicatedkeynode *next;
};
// upper two kinds of keynode will consist of the processed queue in
// simplified mode. In this mode, I supposed that two or more keys
// which have overlay time to be a composite key that holds for a
// period mutual time , so that I can simplify the whole keymacros
// system which is consisted by rawkeynode queue, most macros will
// perform properly, such as press CTRL-ShIFT-R and then press
// CTRL-SHIFT-B, but the user may recording this macro in the way that
// CTRL and SHIFT is not released for the whole time. This worked in
// most cases because application don't take action when CTRL-SHIFT is
// pressed, however If two normal key is pressed together, then
// release one key and press another key, simplified mode will take
// this stage into two steps because the simplified mode is a queue
// transformed from real-simulate mode's queue, then software macro
// running in may perform differently and wrongly.


struct stdrawkeynode
{
  int ktype;
  int kval;
  int ntime;
  struct stdrawkeynode *next;
};

int macro_num; // this value stores how many macros in application
int fixed_macro_num; // count for how many macros is fixed on display

/* FUNCTION DECLARATIONS */
static void reader_init();
static void uinput_init();
static void init_all();
static void uinput_dest();
static void cleanup();
static void setkeypress(int kval);
static void setkeyrelease(int kval);
static void resetevent(); // sendevent and resetevent can be decleared
static void sendevent();  // as inline function, because they only
			  // have one line
static void keysync();
static void * readkeys(void * arg);
static void normal_simulate_key(int kval);
static void simulate_keysegment(int keyseg[8]);


/* FUNCTIONS IMPLEMENTATIONS */
void reader_init() {
  keys_fd = open("/dev/input/event3", O_RDONLY);
}

void uinput_init() {
  uinput_fd = open("/dev/uinput", O_WRONLY|O_NONBLOCK);
  if(uinput_fd < 0 )
    {
      printf("Unable to open /dev/uinput/n");
    }
  ioctl(uinput_fd, UI_SET_EVBIT, EV_KEY);
  for (int i = 1; i<KEY_ALL; ++i) {
    ioctl(uinput_fd, UI_SET_KEYBIT, i);
  }
  memset(&usetup, 0, sizeof(usetup));
  usetup.id.bustype = BUS_USB;
  usetup.id.vendor = 0;
  usetup.id.product = 0;
  strcpy(usetup.name, "Virtual keyboard");
  ioctl(uinput_fd, UI_DEV_SETUP, &usetup);
  if(ioctl(uinput_fd, UI_DEV_CREATE))
    {
      printf("Unable to create UINPUT device");
    }
  sleep(1);
  /* We are inserting a pause here so that userspace has time to
     detect, initialize the new device */
}
void init_all() {
  reader_init();
  uinput_init();
}

void uinput_dest() {
  sleep(1);
  if (ioctl(uinput_fd, UI_DEV_DESTROY)<0) {
    printf("destroy uinput failed");
  }
}

void cleanup() {
  uinput_dest();
  if(close(keys_fd) < 0) {
    printf("close reader failed");
    exit(errno);
  }
  if(close(uinput_fd) < 0) {
    printf("close writer failed");
    exit(errno);
  }
}

void setkeypress(int kval) {
  event.type = EV_KEY;
  event.code = kval;  
  event.value = 1;
  gettimeofday(&event.time,0);  
}

void setkeyrelease(int kval) {
  event.type = EV_KEY;
  event.code = kval;  
  event.value = 0;
  gettimeofday(&event.time,0);  
}

void sendevent() {
  write(uinput_fd,&event,sizeof(event));  
}

void resetevent() {
  memset(&event, 0, sizeof(event));
}

void *readkeys(void * arg) {
  // int keyread_fd;
  // keyread_fd = open("/dev/input/event3", O_RDWR);
  while (1)  
    {
      arg = NULL;
      // this line only to avoid flycheck error, no special
      // function. Because flycheck warns while arg is not used
      if (read (keys_fd, &event, sizeof (event)) == sizeof (event))  
        {  
          if (event.type == EV_KEY)  
            if (event.value == 0 || event.value == 1)  
	      {  
		printf ("key %d %s\n", event.code,  
			(event.value) ? "Pressed" : "Released");
		if(event.code==KEY_ENTER)
		  {
		    printf("enter pressed");
		    //break;
		  }
	      }  
        }  
    }
}

void keysync() {
  event.type = EV_SYN;  
  event.code = SYN_REPORT;  
  event.value = 0;
  sendevent();
}


void normal_simulate_key(int kval)  
{
  setkeypress(kval);
  sendevent();
  keysync();
  resetevent(); // this line seems unnessary
  setkeyrelease(kval);  
  sendevent();
  keysync();
  resetevent();
}

void simulate_keysegment(int keyseg[8]){
  setkeypress(keyseg[Key1]);
  sendevent();
  keysync();
  resetevent();
  setkeypress(keyseg[Key2]);
  sendevent();
  keysync();
  resetevent();
  for (int i =2; keyseg[i] !=0; ++i) {
    setkeypress(keyseg[i]);
    sendevent();
    keysync();
    resetevent();
  }
  setkeyrelease(keyseg[Key1]);
  sendevent();
  keysync();
  resetevent();
  setkeyrelease(keyseg[Key2]);
  sendevent();
  keysync();
  resetevent();
  for (int i =2; keyseg[i] !=0; ++i) {
    setkeyrelease(keyseg[i]);
    sendevent();
    keysync();
    resetevent();
  }
}
// here use reduntant code to avoid the first two key press and
// release's varification. May help to work better


int main()
{
  
  init_all();
  sleep(2);
  pthread_t tid;
  tid = pthread_self();
  int err;
  err = pthread_create(&tid, NULL, readkeys, NULL);
  /* this thread dont use a cleanup program so there is a leak which
     is about 272 bytes */


  
   struct stdcomplicatedkeynode node1;
   node1.next= NULL;
   node1.keys[Key1] = KEY_C;
   node1.keys[Key2] = KEY_C;
   node1.keys[Key3] = KEY_A;
  // struct stdcomplicatedkeynode node2;
  // node1.next = &node2;
  // node2.next = NULL;
   sleep(20);
   simulate_keysegment(node1.keys);
   //cleanup();
   return 0;
}

