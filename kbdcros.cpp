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
#include <sys/time.h>
#include <sys/unistd.h>
#include <stdio.h>
#include <signal.h>
#include <assert.h>
#include <fcntl.h>
#include <string.h>
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
// enum {StdcplxType, RawType, BreakPoint, Start};
// KeyType is banned in future program
enum {Key1, Key2, Key3, Key4, Key5, Key6, Key7, Key8};


/* STRUCT and VARIABLE */
static int keys_fd = -1;
static int uinput_fd = -1;
static char keymap[250][18];
// because the longest string in file has 17 char, but when testing
// ,the latter index seems not important. In valgrind test
// program, neithor 12 nor 18 will make a memory leak [todo]

struct input_event event;
struct uinput_setup usetup;

struct stdkeynode
{
  int ktype;
  int val;
  struct stdkeynode *next;
};

struct stdcmplxkeynode
{
  // int ktype;
  int keys[8]={0,0,0,0,0,0,0,0}; // short may be better [todo]
  struct stdcmplxkeynode *next;
};


struct stdrawkeynode
{
  // int ktype;
  int kval=0; // press or release
  int kcode=0; // which key to simulate
  long ntime;
  struct stdrawkeynode *next = NULL;
};

struct rawlink
{
  int len=0;
  int currlen = 0;
  struct stdrawkeynode * head= NULL;
  struct stdrawkeynode * curr= NULL;
  struct stdrawkeynode * tail= NULL;
};

int macro_num; // this value stores how many macros in application
int fixed_macro_num; // count for how many macros is fixed on display
typedef struct stdcmplxkeynode ComplxNode;
typedef struct stdrawkeynode RawNode;
typedef struct rawlink RawLink;

/* FUNCTION DECLARATIONS */
static void reader_init();
static void uinput_init();
static void init_all();
// init functions
static void loadkeymap();
static void uinput_dest();
static void cleanfd();

static void setkeypress(int kcode);
static void setkeyrelease(int kcode);
static void resetevent(); // sendevent and resetevent can be decleared
static void sendevent();  // as inline function, because they only
			  // have one line
static void emitevent(int kval, int kcode);
static void keysync();
static void * readkeys(void * arg);

static void normal_simulate_key(int kcode);
static void simulate_keysegment(int keyseg[8]);
static void simulate_rawevent(RawNode *node);
static int rawkeycounter(RawNode *head);
// counter for how many keys in rawkey sequence
static int keycombcounter(ComplxNode *head);
// counter for how many key binding in standard sequence
static bool initrawlink(RawLink *RawLink);
static RawNode * getrawnode(int position , RawLink *RawLink);
static void print_rawmacro(RawLink *RawLink);
// I don't know why here cant use RawLink as second argument

static bool ismacroempty(RawLink *RawLink);
static void insertrawnode(int position, RawNode *node, RawLink *RawLink);
static bool vali_insertrawnode(int position, RawNode *node, RawLink *RawLink);
static int rsimulate_macro (ComplxNode * head);
static void simacro_once(ComplxNode **head);
// because for rawmacro there is no need to use recursive loop to
// simulate macro, So I did't write that function
static void sirawmacro_once(RawLink *RawLink);
static char* getkey(RawNode *node);
static char* getcombination(RawNode *node);
static void appendnode(RawNode *node, RawLink *RawLink);

/* FUNCTIONS IMPLEMENTATIONS */
void reader_init() {
  keys_fd = open("/dev/input/event3", O_RDONLY);
  if(keys_fd < 0) {
    perror("Reader init failed\n");
    exit(13);
  }
}

void uinput_init() {
  uinput_fd = open("/dev/uinput", O_WRONLY|O_NONBLOCK);
  if(uinput_fd < 0 )
    {
      perror("Unable to open /dev/uinput\n");
      exit(13);
    }
  ioctl(uinput_fd, UI_SET_EVBIT, EV_KEY);
  for (int i = 0; i<KEY_ALL; ++i) {
    ioctl(uinput_fd, UI_SET_KEYBIT, i);
  }
  // here also enable 0, so that we can use it to simulate breakpoint,
  // and 249 is also enabled so that we can use it to simulate
  // starting action
  memset(&usetup, 0, sizeof(usetup));
  usetup.id.bustype = BUS_USB;
  usetup.id.vendor = 0x1;
  usetup.id.product = 0x1;
  strcpy(usetup.name, "Virtual Keyboard Made by JingOS");
  ioctl(uinput_fd, UI_DEV_SETUP, &usetup);
  if(ioctl(uinput_fd, UI_DEV_CREATE))
    {
      perror("Unable to create UINPUT device");
      exit(13);
    }
  sleep(1);
  /* We are inserting a pause here so that userspace has time to
     detect, initialize the new device */
}
void init_all_fd() {
  reader_init();
  uinput_init();
}

void init_all(){
  loadkeymap();
  init_all_fd();
}

void uinput_dest() {
  sleep(1);
  if (ioctl(uinput_fd, UI_DEV_DESTROY)<0) {
    perror("Destroy uinput failed\n");
    exit(6);
  }
}

void cleanfd() {
  uinput_dest();
  if(close(keys_fd) < 0) {
    perror("Close reader failed\n");
    exit(9);
  }
  if(close(uinput_fd) < 0) {
    perror("Close writer failed\n");
    exit(9);
    
  }
}



void setkeypress(int kcode) {
  event.type = EV_KEY;
  event.code = kcode;  
  event.value = 1;
  gettimeofday(&event.time,0);  
}

void setkeyrelease(int kcode) {
  event.type = EV_KEY;
  event.code = kcode;  
  event.value = 0;
  gettimeofday(&event.time,0);  
}

void sendevent() {
  write(uinput_fd,&event,sizeof(event));  
}

void emitevent(int kval, int kcode) {
  event.type = EV_KEY;
  event.code = kcode;
  event.value = kval;
  gettimeofday(&event.time,0);
  sendevent();
  keysync();
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
	    {
	      if (event.code == KEY_ESC){
		exit(1);
	      }
	      if (event.value == 0 || event.value == 1)  
		{  
		  printf ("key %s %s\n", keymap[event.code],  
			  (event.value) ? "Pressed" : "Released");
		}  
	    }  
	}
    }
  return arg;
}


void keysync() {
  event.type = EV_SYN;  
  event.code = SYN_REPORT;  
  event.value = 0;
  sendevent();
}

void loadkeymap() {
  FILE * fp = fopen("./keymap.map", "r");
  int i = 0;
  while((fscanf(fp, "%d %s",  &i, keymap[i])) != EOF)
    {
      printf("num:%d stands for %s\n", i,keymap[i]);
      i++;       
    }
  fclose(fp);
}

void normal_simulate_key(int kcode)  
{
  setkeypress(kcode);
  sendevent();
  keysync();
  resetevent(); // this line seems unnessary
  setkeyrelease(kcode);  
  sendevent();
  keysync();
  resetevent();
}

void simulate_keysegment(int keyseg[8]){
  setkeypress(keyseg[Key1]);
  sendevent();
  keysync();
  resetevent();
  for (int i =1; keyseg[i] !=0; ++i) {
    setkeypress(keyseg[i]);
    sendevent();
    keysync();
    resetevent();
  }
  setkeyrelease(keyseg[Key1]);
  sendevent();
  keysync();
  resetevent();
  for (int i =1; keyseg[i] !=0; ++i) {
    setkeyrelease(keyseg[i]);
    sendevent();
    keysync();
    resetevent();
  }
}
// here use reduntant code to avoid the first two key press and
// release's varification. May help to work better

void simulate_rawevent(RawNode *node)
{
  emitevent(node->kval, node->kcode);
}

int rawkeycounter(RawNode * head) {
  int i = 0;
  RawNode * node = head;
  for(;node->next!=NULL;i++){
    node = node->next;
  }
  return i;
}

int keycombcounter(ComplxNode * head){
  int i = 0;
  ComplxNode * node = head;
  for(;node->next != NULL;i++){
    node = node->next;
  }
  return i;
}

bool initrawlink(RawLink *RawLink) {
 RawLink->head  = (RawNode *)malloc(sizeof(RawNode));
 RawLink->curr =  RawLink->head;
 RawLink->tail = RawLink->head;
  if(!RawLink){
    return false;
  }
  else{
    RawLink->head->next = NULL;
    return true;
  }
}

RawNode * getrawnode(int position, RawLink *RawLink) {
  int i = 0;
  RawNode * node = RawLink->head;
  for(;i<position;i++){
    node = node->next;
  }
  return node;
}



bool ismacroempty(RawLink *RawLink){
  RawNode *head = RawLink->head;
  if(head->next == NULL) {
    return true;
  }
  else {
    return false;
  }
}

void print_rawmacro(RawLink *RawLink) {
  int i = 0;
  RawNode *head = RawLink->head;
  if(ismacroempty(RawLink)){
    perror("This is an empty macro list");
  }
  else{
    printf("There are %d rawkeys in macro, current node is at %d\n", RawLink->len,RawLink->currlen);
    while(head->next!=NULL)
      {
	i++;
	head = head->next;
	printf("%d key value is %d and its status is %s\n", i, head->kcode,(head->kval==1)?"pressed" : "released");
      }
  }


}

void insertrawnode(int position, RawNode *node, RawLink *RawLink) {
  // because if we simply use gui to insert a node into macro, there
  // is no need to test for whether position is valid or not
  int len = rawkeycounter(RawLink->head);
  RawNode * nodeA;
  if(position==len+1){
    RawLink->tail = node;
  }
  // this is used to update tail node
  nodeA = getrawnode(position-1, RawLink);
  node->next = nodeA->next;
  nodeA->next = node;
  RawLink->len++;
}

bool vali_insertrawnode(int position, RawNode *node, RawLink *RawLink){
  int len = rawkeycounter(RawLink->head);
  RawNode * nodeA;
  if(position > len+1 && position<0){
    return false;
  }
  else{
    if(position==len+1){
      RawLink->tail = node;
    }
    // this is used to update tail node
     nodeA = getrawnode(position-1, RawLink);
  }
  node->next = nodeA->next;
  nodeA->next = node;
  RawLink->len++;
  return true;
}


int rsimulate_macro (ComplxNode * head){
  ComplxNode *node = head;
  int i = 1;
  simulate_keysegment(node->keys);
  if(node->next != NULL) {
    i++;
    rsimulate_macro(node->next);
  }
  return i;
} 
// this simulate function is used for testing

void simacro_once(ComplxNode **head) {
  ComplxNode ** node = head;
  *node = (** node).next;
  simulate_keysegment((**node).keys);
  //  return head;
}


void sirawmacro_once(RawLink *RawLink) {
  RawNode ** node = &RawLink->curr;
  *node = (** node).next;
  simulate_rawevent(*node);
  RawLink->currlen++;
  //  return head;
}
// this function is used for latter gui application, and then
// construct a counter for gui to release user's curiosity, here I use
// second level to revise pointer.

char* getkey(RawNode *node) {
  char * meaning;
  meaning = keymap[node->kcode];
  return meaning;
}

void appendnode(RawNode *node,RawLink *RawLink) {
  RawLink->tail->next = node;
  RawLink->tail = node;
  RawLink->len++;
}

int main()
{
  
  
  //  keys_fd = open("/dev/input/event3", O_RDWR);
  //loadkeymap();
  // uinput_fd = keys_fd;
  // pthread_t tid;
  // reader_init();
  // tid = pthread_self();
  // int err;
  // err = pthread_create(&tid, NULL, readkeys, NULL);
  /* this thread dont use a cleanup program so there is a leak which
     is about 272 bytes */
  uinput_fd = open("/dev/input/event3", O_RDWR);
  RawLink Link;
  initrawlink(&Link);
  print_rawmacro(&Link);
  RawNode * node1;
  RawNode * node2;
  node1 = (RawNode *)malloc(sizeof(RawNode));
  node2 = (RawNode *)malloc(sizeof(RawNode));
  node2->kcode =KEY_1;
  node1->kcode = KEY_1;
  node1->kval = 1;
  node2->kval = 0;

  insertrawnode(1, node1, &Link);
  RawNode *tail1 = Link.tail;
  sirawmacro_once(&Link);
  printf("For now tail's code is %d, val is %d\n", tail1->kcode,tail1->kval);
  insertrawnode(2, node2, &Link);
  RawNode *tail2 = Link.tail;
  sirawmacro_once(&Link);
  printf("For now tail's code is %d, val is %d\n", tail2->kcode,tail2->kval);
  print_rawmacro(&Link);

  // sleep(2);
  //cleanup();
  return 0;

}
