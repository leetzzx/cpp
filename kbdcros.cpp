//SEE LICENSE file for copyright and license details.

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
#include <dirent.h>
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
static clock_t start;
static clock_t end;
static long mscounter = 0;
static long accounter = 0;
// this variable is used for timer selection
static float timeindex;
static FILE * macrofile;
static char keymap[250][18];
static char directory[20] = "./macros/"; 
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
  long ntime=500000;
  struct stdrawkeynode *next = NULL;
};

struct rawlink
{
  int len=0;
  // this len will be writed into file, to construct a
  // multi-macrofile, I think this will help in file reader
  int currlen = 0;
  int mode = 0;
  struct stdrawkeynode * head= NULL;
  struct stdrawkeynode * curr= NULL;
  struct stdrawkeynode * tail= NULL;
  char name[100];
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
static bool startmap(char *macroname);
static void uinput_dest();
static void cleanfd();

static void setkeypress(int kcode);
static void setkeyrelease(int kcode);
static void resetevent(); // sendevent and resetevent can be decleared
static void sendevent();  // as inline function, because they only
			  // have one line
static void emitevent(int kval, int kcode);
static void keysync();


static void * mstimer(void * arg);
static void * mslooper(void * arg);
static void * gettimeindex(void * arg);
static void * readkeys(void * arg);
static void * recordkeys(void * arg);
static void * makelink(void * arg);
static void * loadlink(void * arg);

static void normal_simulate_key(int kcode);
static void simulate_keysegment(int keyseg[8]);
static void simulate_rawevent(RawNode *node);
static int rawkeycounter(RawNode *head);
// counter for how many keys in rawkey sequence
static int keycombcounter(ComplxNode *head);
// counter for how many key binding in standard sequence
static bool initrawlink(RawLink *RawLink);
static void freerawlink(RawLink *RawLink);
static void cltimelink(RawLink *RawLink);
static void vltimelink(RawLink *RawLink);
static bool writelink(RawLink RawLink);
static void resetposlink(RawLink *RawLink);
static RawNode * getrawnode(int position , RawLink *RawLink);
static void print_rawmacro(RawLink *RawLink);
// I don't know why here cant use RawLink as second argument

static bool ismacroempty(RawLink RawLink);
static void cleankeys(RawLink RawLink);
static void insertrawnode(int position, RawNode *node, RawLink *RawLink);
static bool vali_insertrawnode(int position, RawNode *node, RawLink *RawLink);
static void deleterawnode(int position, RawLink *RawLink);
static bool vali_deleterawnode(int position, RawLink *RawLink);

static int rsimulate_macro (ComplxNode * head);
static void simacro_once(ComplxNode **head);
// because for rawmacro there is no need to use recursive loop to
// simulate macro, So I did't write that function
static void sirawmacro_once(RawLink *RawLink);
static void sirawlink(RawLink *RawLink);
static void sirawlink_nowait(RawLink *RawLink);
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

void * mstimer(void * arg) {
  while(1){
    mscounter= ~mscounter;
    end = clock();
  }
}

void * mslooper(void * arg) {
  while(1){
    usleep(1000);
    mscounter++;
  }
}

void * gettimeindex(void * arg) {
  pthread_t tindex;
  int err2;
  err2 = pthread_create(&tindex, NULL, mslooper, NULL);
  sleep(2);
  long b = mscounter;
  sleep(2);
  long c = mscounter;
  timeindex = (float)(c-b)/2000;
  //printf("index is %f\n", timeindex);
  pthread_cancel(tindex);
  pthread_exit(NULL);
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

void * recordkeys(void * arg) {
  char  macroname[100];
  printf("Please input one name for macro recording\n");
  scanf("%s", macroname);
  char * macrofp = strcat(directory, macroname);
  printf("the file you want to write in is %s\n", macrofp); 
  startmap(macrofp);
  usleep(10000000);
  // In terminal, here is a problem is that when First keyevent
  // arrived, its status always be released. because you always need
  // to press ENTER to get name for file
  while (1)  
    {
      arg = NULL;
      if (read (keys_fd, &event, sizeof (event)) == sizeof (event))  
        {  
          if (event.type == EV_KEY)
	    {
	      if (event.code == KEY_ESC){
		exit(1);
		fclose(macrofile);
	      }
	      fprintf(macrofile, "%s %d %d\n",keymap[event.code], event.code,event.value);
	    }  
	}
    }
  return arg;
}

void * makelink(void * arg) {
  RawLink * Link2 = (RawLink *) arg;
  //macrofile = fopen(Link2->name, "w+");
  while (1)  
    {

      arg = NULL;

      if (read (keys_fd, &event, sizeof (event)) == sizeof (event))  
        {
          if (event.type == EV_KEY)
	    {
	      if (event.code == KEY_ESC){
		print_rawmacro(Link2);
		vltimelink(Link2);
		writelink(*Link2);
		freerawlink(Link2);
		exit(1);
	      }
	      else{
		//accounter = (long)mscounter/timeindex;
		//accounter = (long)mscounter;
		//accounter = (end-start)/1000;
		accounter = event.time.tv_sec*1000+event.time.tv_usec/1000;
		printf ("key %s %s %ld time\n", keymap[event.code],(event.value) ? "Pressed" : "Released", accounter);
		RawNode * a = (RawNode *)malloc(sizeof(RawNode));
		a->kcode = event.code;
		a->kval = event.value;
		a->ntime = accounter;
		//fprintf(macrofile, "key %s %s\n", keymap[a->kcode],(a->kval) ? "Pressed" : "Released");
		appendnode(a, Link2);
	      }
	    }  
	}
    }
  return NULL;
}

void * loadlink(void * arg) {
  RawLink * Link1 = (RawLink *) arg;
  int len;
  int mode;
  int kcode;
  int kval;
  int ntime;
  char mean[21];
  char  macroname[100];
  strcpy(macroname, Link1->name);

  char * macrofp = strcat(directory, macroname);
  printf("the file you want to read is %s\n", macrofp);
  // printf("the file you want to write in is %s\n", macrofp); 
  macrofile = fopen(macrofp, "r+");
  fscanf(macrofile, "[%s %d %d]", mean, &len, &mode);
  printf("%s %d %d", mean,len, mode);
  while((fscanf(macrofile, "%s %d %d %d", mean, &kcode, &kval, &ntime))!=EOF)
    {
      RawNode *NodeA;
      NodeA = (RawNode *)malloc(sizeof(RawNode));
      NodeA->kcode = kcode;
      NodeA->kval = kval;
      NodeA->ntime = ntime;
      appendnode(NodeA, Link1);
    }
  print_rawmacro(Link1);
  // In terminal, here is a problem is that when First keyevent
  // arrived, its status always be released. because you always need
  // to press ENTER to get name for file
  return NULL;
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
      // printf("num:%d stands for %s\n", i,keymap[i]);
      i++;       
    }
  fclose(fp);
}

bool startmap(char *macroname) {
  if((fopen(macroname,"w") == NULL)) {
    return false;
  }
  else {
    return true;
  }
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

void freerawlink(RawLink *RawLink) {
  // ismacroempty(*RawLink); justify if a link is empty or not is
  // unnessary.
  RawNode * node;
  RawNode * head = RawLink->head;
  while(head!=NULL){
    node= head;
    head= head->next;
    free(node);
  }
}

void cltimelink(RawLink *RawLink) {
  RawNode * nodeA = RawLink->tail;
  RawNode * nodeB = RawLink->head->next;
  while (nodeB->next!=NULL) {
    nodeB->ntime = nodeB->next->ntime - nodeB->ntime;
    nodeB = nodeB->next;
  }
  nodeA->ntime = 0;
}

void vltimelink(RawLink *RawLink) {
 RawNode * nodeA = RawLink->head->next;
  RawNode * nodeB = nodeA->next;
  while (nodeB!=NULL) {
    nodeB->ntime = nodeB->ntime - nodeA->ntime +100;
    nodeB = nodeB->next;
  }
  nodeA->ntime = 100;
}

bool writelink(RawLink RawLink) {
  RawNode *head = RawLink.head;
  char  macroname[100];
  strcpy(macroname, RawLink.name);
  char * macrofp = strcat(directory, macroname);
  // printf("the file you want to write in is %s\n", macrofp); 
  macrofile = fopen(macrofp, "w+");
  fprintf(macrofile, "[%s %d %d]\n",RawLink.name,RawLink.len, RawLink.mode);
  while(head->next!=NULL){
    head = head->next;
    fprintf(macrofile, "%s %d %d %ld\n", keymap[head->kcode], head->kcode, head->kval, head->ntime);
  }
  // In terminal, here is a problem is that when First keyevent
  // arrived, its status always be released. because you always need
  // to press ENTER to get name for file
  return true;
}

void resetposlink(RawLink *RawLink){
  RawLink->curr = RawLink->head;
  RawLink->currlen = 0;
}

RawNode * getrawnode(int position, RawLink *RawLink) {
  int i = 0;
  RawNode * node = RawLink->head;
  for(;i<position;i++){
    node = node->next;
  }
  return node;
}



bool ismacroempty(RawLink RawLink){
  RawNode *head = RawLink.head;
  if(head->next == NULL) {
    return true;
  }
  else {
    return false;
  }
}

void cleankeys(RawLink RawLink) {
  int i =0;
  RawNode *nodeA = RawLink.head;
  if(RawLink.len > 250){
    for(; i<250; i++){
      emitevent(0, i);
      keysync();
      usleep(20);
    }
  }
  else {
    while(nodeA->next!=NULL) {
      nodeA = nodeA->next;
      nodeA->kval = 0;
      simulate_rawevent(nodeA);
      usleep(20);
    }
  }
}

void print_rawmacro(RawLink *RawLink) {
  int i = 0;
  RawNode *head = RawLink->head;
  if(ismacroempty(*RawLink)){
    perror("This is an empty macro list");
  }
  else{
    printf("There are %d rawkeys in macro, current node is at %d\n", RawLink->len,RawLink->currlen);
    printf("Tail information: code %d, val %d\n", RawLink->tail->kcode,RawLink->tail->kval);
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
  int len = RawLink->len;
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

void deleterawnode(int position, RawLink *RawLink){
  int len = RawLink->len;
  RawNode *before = getrawnode(position-1, RawLink);
  RawNode *after = before->next;
  if(position == len) {
    RawLink->tail = before;
  }
  before->next = after->next;
  RawLink->len--;
  free(after);
}



bool vali_deleterawnode(int position, RawLink *RawLink){
  int len = rawkeycounter(RawLink->head);
  if( position>len || position<=0) {
    perror("Please don't delete a node that does't exist");
    return false;
  }
  else{
    RawNode *before = getrawnode(position-1, RawLink);
    RawNode *after = before->next;
    if(position == len) {
      RawLink->tail = before;
    }
    before->next = after->next;
    RawLink->len--;
    free(after);
    return true;
  }
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

void sirawlink(RawLink *RawLink) {
  RawNode * nodeA = RawLink->head;
  while (nodeA->next!=NULL) {
    nodeA = nodeA->next;
    emitevent(nodeA->kval, nodeA->kcode);
    usleep(nodeA->ntime*1000);
  }
}

void sirawlink_nowait(RawLink *RawLink) {
  RawNode * nodeA = RawLink->head;
  while (nodeA->next!=NULL) {
    nodeA = nodeA->next;
    emitevent(nodeA->kval, nodeA->kcode);
    //usleep(nodeA->ntime); this line is not safe, because some ntime
    //will greater than 250ms aka 250000us, and will cause a repeat
    //key, so here use fix step of 200us
    usleep(200);
  }
}

char* getkey(RawNode *node) {
  char * meaning;
  meaning = keymap[node->kcode];
  return meaning;
}

void appendnode(RawNode *node,RawLink *RawLink) {
  RawLink->tail->next = node;
  RawLink->tail = node;
  RawLink->len++;
  node->next = NULL;
  // I don't understand why it makes an error if I remove last one
  // line although I have initialize RawNode.next into NULL. I have to
  // initialize it to NULL again, It is strange
}

int filter(const struct dirent *macro){
  if(macro->d_name[0] == '.'){
    return 0; 
  }
  else{
    return 1;
  }
}

int main(int argc, char *argv[])
{
  
  loadkeymap();
  int opt;
  int err;
  char dmode;
  RawLink Link;
  initrawlink(&Link);
  pthread_t tid1;
  pthread_t tid2;
  pthread_t tid3;
  void * arg1 = (void *) &Link;
  while ((opt=getopt(argc, argv, "lm:n:"))!=-1) {
    switch (opt) {
    case 'l': {
      struct dirent ** macro_list;
      int count;
      int i;
      count = scandir(directory, &macro_list, filter, alphasort);
      // list all macros in macro directory
      if(count < 0 ){
	perror("scandir");
	return EXIT_FAILURE;
      }

      printf("there are %d macros\n", count);
      printf("--------------------------\n");
      for(i = 0;i<count;i++) {
	struct dirent *macro;
	macro = macro_list[i];

	printf("%i macro is %s\n", i+1, macro->d_name);
	free(macro);
      }
      free(macro_list);
      break;
    }
    case 'm': {
      if(!strcmp("r", optarg)){
	printf("record a keymacro\n");
	printf("--------------------------\n");
	dmode = 'r';
      }
      else if (!strcmp("s", optarg)){
	printf("simulate a keymacro\n");
	printf("--------------------------\n");
	dmode = 's';
      }
      else {
	perror("mode must be r for record or s for simulate\n");
	return 0;
      }
      break;
    }
    case 'n': {
      strcpy(Link.name, optarg);
      break;
    }
    case '?': {
      perror("unknown option received\n");
      break;
    }
    default:
      break;
    }
  }
  switch (dmode) {
  case 'r': {
    reader_init();
    tid1 = pthread_self();
    pthread_create(&tid1, NULL, gettimeindex, NULL);
    pthread_join(tid1, NULL);
    err = pthread_create(&tid2, NULL, mslooper, NULL);
    pthread_create(&tid3, NULL, makelink, arg1);
    sleep(20);
    close(keys_fd);
    break;
  }
  case 's': {
    uinput_init();
    tid1 = pthread_self();
    pthread_create(&tid1, NULL, loadlink, arg1);
    pthread_join(tid1, NULL);
    cltimelink(&Link);
    sirawlink(&Link);
    cleankeys(Link);
    uinput_dest();
  }
  }
  // in valgrind program always has one heap block is not freed,
  // because although we have malloc according memory with initrawlink
  // function, we can't normally free it in the main function, this
  // does't metter
  
  return 0;
}
