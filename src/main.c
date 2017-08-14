/* Terminal ScreenSaver
 * Copyright (C) 2006 Kristian "gamkiller" Gunstone
 *
 * Locking and shadow password retrieval code based on
 * vlock 1.3
 * Copyright (C) 1994 Michael K. Johnson and Marek
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 ***
 *
 * TODO:
 * 		- Attempt to disable blanking and restore it on exit
 * 		  (usnig setterm -blank 0 (Don't know how to get it back?))
 * 		- User-configurable object speeds
 * 		- Better deinit on perrors
 * 		- (more) Security checks
 * 		- Cleaning up
 * 
 * BUGS:
 * 		- Slight misalignments between internal data and curses
 * 		- Floating point exception on init (Due to small termsize)
 * 		- Offset problem in random values
 *
 * THOUGHS:
 * 		- WARNING: "MAXLINES" used in non-line fashion(mirrorbuf)
 *
 * Changelog:
 *
 *      0.8.2
 *              - Read files after SUID drop (Fixes Debian bug #475747)
 *              - Drop SUID even if locking is not enabled (Fixed Debian "bug" #475736)
 *                This one was REALLY stupid.
 *              - Exit with EXIT_SUCCESS or EXIT_FAILURE 
 *              - Minor error report cleanups (severe_error now takes va_list)
 *              - Check if ascii file is a regular file
 * 	0.8.1
 * 		- Added failed login attempt reporting via syslog
 * 	0.8
 * 		- Initial direction selection
 * 		- Inline mirror disabling
 * 		- Command line options for object speeds
 * 	0.7.8.4
 * 		- ASCII autopadding. No need to manually pad spaces!
 * 		- Long options
 * 		- Added an other char to mirror (b->d, d->b)
 * 		- Widest line check fix (last line was never compared)
 * 		- Minor offset adjustments for very small terminals
 * 		- MAXPATH on all path related chars
 * 		- Check if ascii path exceeds MAXPATH
 *
 * 	0.7.8.3
 * 		- VT_SETMODE error checking
 * 		- frsig fix (Fixes broken locking in FreeBSD)
 * 	0.7.8.2
 * 		- flush stdin before getinput (Fixes odd "ghost keys" on input)
 *	0.7.8.1
 *		- Very minor bugfixes. has_colors() warning in code, 
 *	      	changes to help and INSTALL
 * 	0.7.8 
 * 		- Commented out script exec code. Too lazy to do it.
 * 		- Mirroring enabled by default
 * 		- Added A_BOLD (Daemon now looks like in FreeBSD)
 * 		- Removed static allocation for ascii
 * 		- Full color support
 * 		- Fixed typo in mirroring code
 * 	0.7.7
 * 		- Ascii mirroring support
 * 		- file_name broken if not random (from 0.7.5)
 * 	0.7.6
 * 		- Size check for passwd screen
 * 		- Timeout countdown display
 * 		- Password entry timeout code
 * 		- New frame for password box
 * 	0.7.5
 * 		- Minor fixes
 * 		- Added an other default directory, $HOME/.tss/
 * 		- Failed login report on exit
 * 		- Broken terminal configuration on exit (First seen in  0.7.4)
 * 		- O_RDWR define fix (minor)
 * 	0.7.4
 * 		- Minor fixes
 * 		- VT Locking
 * 	0.7.3
 * 		- Fixed fd_isset SIGABRT call when screen was too small
 * 		- Additional experimental locking code
 * 		- Load average as default scrolltext
 * 	0.7.2:
 * 		- Lock screen (no actual lock yet)
 * 		- Dynamic text in scrollbox
 * 		- shellscript exeuction
 * 	0.7.1:
 * 		- Argument handler
 * 		- Scrollbox
 * 		- Delay set
 * 	0.7:
 * 		- Random ascii read support
 * 		- Speed differing on objs
 * 		- Complete code rewrite.
 *
 * */

#define _XOPEN_SOURCE   1

#include <pwd.h>
#include <time.h>
#include <glob.h>
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <syslog.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <strings.h>
#include <termios.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/utsname.h>

#ifndef BSD
 #include <ncurses.h>
 #include <shadow.h>
 #include <sys/vt.h>
 #include <sys/kd.h>
 #define SA_RESTART     0x10000000
#else
 #include <curses.h>
 #include <sys/consio.h>
#endif

#define VERSION			"0.8.2"
#define DEFAULT_ASCII_DIR	"/etc/tss/"
#define DEFAULT_ASCII		"default"
#define MAX_ASCII_SIZE		1024000
#define TIMEOUT			30
#define MAXLINES		1024
#define MAXPATH			512

#define SCROLL_BOX_WIDTH	20

#define UNAME			0
#define INFO			1
  
int lock_delay;
int failed_logins;
int vfd;
int screen_width;
int screen_height;
int current_color;

FILE *fd_ascii;
  
static char username[40];
static char userpass[200];

char mirrorchr[2][15];
char *scroll_buffer;
  
glob_t list;

struct vt_mode ovtm;
struct termios oterm;

static sigset_t osig;

struct ascii_objEx{
  char *data;
  char *line[MAXLINES];
  char *blank[MAXLINES];
  char mirror[MAXLINES];
  float x;
  float y;
  int max_x;
  int max_y;
  float direction_x;
  float direction_y;
  float speed;
  int width;
  int width_real;
  int height;
} ascii_obj;

static struct option const long_options[] = {
    {"no-mirror", no_argument, NULL, 'n'},
    {"scrollbar", no_argument, NULL, 's'},
    {"random", no_argument, NULL, 'r'},
    {"lock-terminal", no_argument, NULL, 'l'},
    {"delay", required_argument, NULL, 'd'},
    {"ascii", required_argument, NULL, 'a'},
    {"object-speed", required_argument, NULL, 'o'},
    {"uname-speed", required_argument, NULL, 'e'},
    {"info-speed", required_argument, NULL, 'i'},
    {"help", no_argument, NULL, 'h'},
    {"version", no_argument, NULL, 'V'},
    {NULL, 0, NULL, 0}
};
  
void report_failed_login(char *user/*, char *pass*/){
  openlog("tss", LOG_PID | LOG_ODELAY, LOG_USER);
  /*syslog(LOG_NOTICE, "Failed login attempt with password \"%s\" for user \"%s\"", pass, user);*/
  syslog(LOG_NOTICE, "Failed login attempt for user \"%s\"", user);
  closelog();
}

void vt_release(int sig){
  ioctl(vfd, VT_RELDISP, 0);
}

void signal_ignorer(int sig){
  return;
}

void restore_terminal(void){
  ioctl(vfd, VT_SETMODE, &ovtm);
  tcsetattr(STDIN_FILENO, TCSANOW, &oterm);
}

int getloadavg(double loadavg[], int nelem);
#ifndef BSD
 void usleep(unsigned long usec);
#endif

long lof(FILE *fptr){
  long old, new;
  fseek(fptr, 0, SEEK_CUR);
  old = ftell(fptr);
  fseek(fptr, 0, SEEK_END);
  new = ftell(fptr);
  fseek(fptr, 0, old);
  return new;
}

void cleanup(void){
  int i;

  for(i = 0; i < (ascii_obj.height - 1); i++){
    /* fprintf(stderr, "DEBUG: Freeing objline %d (%db)\n", i, strlen(ascii_obj.line[i]));*/
    free(ascii_obj.line[i]);
    /* fprintf(stderr, "DEBUG: Freeing blkline %d (%db)\n", i, strlen(ascii_obj.blank[i]));*/
    free(ascii_obj.blank[i]);
  }
    
  globfree(&list);

  if(ascii_obj.data != NULL)
    free(ascii_obj.data);

  free(scroll_buffer);
    
  if(fd_ascii != NULL)
    fclose(fd_ascii);

  if(vfd != -1)
    close(vfd);

}

void severe_error(char *message, ...){
  va_list args;
  char buffer[1024];

  endwin();

  va_start(args, message);
  vsprintf(buffer, message, args);
  va_end(args);

  fprintf(stderr, "%s", buffer);

  cleanup();

  exit(EXIT_FAILURE);
}

double tickcount(void){
  struct timeval tick;
  double time;
  gettimeofday(&tick, 0);
  time = (double) tick.tv_sec + ((double) tick.tv_usec) / 1000000;
  return time;
}

void showver(void){
  printf("Terminal Screensaver v%s (C) 2006 Kristian Gunstone.\n\n", VERSION);
}

void showcopyright(void){
  printf("This is free software; see the source for copying conditions. "
	 "There is NO\nwarranty; not even for MERCHANTABILITY or FITNESS "
	 "FOR A PARTICULAR PURPOSE.\n\n");
}

void usage(char *me){
  showver();
  printf("Usage: %s [-s] [-r] [-l] [-n] [-h] [-V] "
	 "[-d delay] [-a ascii]\n", me);
	 /*"[-d delay] [-a ascii] [-t script] [-u secs]\n", me);*/
  printf("Default: %s -d 120 -o .5 -e .1 -i 1 -a %s/default\n\n", me, DEFAULT_ASCII_DIR);
  printf("  -n, --no-delay              Disable ASCII mirroring\n");
  printf("  -s, --scrollbar             Show load average in a scrollbar\n");
  printf("  -r, --random                Choose random ascii file\n");
  printf("  -l, --lock-terminal         Lock terminal\n");
  printf("  -d, --delay=[delay]         Update every [delay] milliseconds\n");
  printf("  -a, --ascii=[ascii]         Use ascii [ascii]\n");
  printf("  -o, --object-speed=[speed]  Set ascii speed (0.001 - 1.00)\n");
  printf("  -e, --uname-speed=[speed]   Set uname speed (0.001 - 1.00)\n");
  printf("  -i, --info-speed=[speed]    Set info speed (0.001 - 1.00)\n");
  /*
  printf(" [UNDONE] -t Show output of [script] in scrolltext\n");
  printf(" [UNDONE] -u Run [script] every [seconds] seconds\n");
  */
  printf("  -h, --help                  Show this text\n");
  printf("  -V, --version               Show version\n\n");
  showcopyright();
}

void drawpercent(double value){
  double v_percent;
  double s_percent;

  v_percent = 100 * value / TIMEOUT;
  s_percent = screen_height * v_percent / 100;
  
  mvprintw((int)s_percent - 1, 0, " ");
  /*mvprintw((int)s_percent - 1, 1, "    ");
  mvprintw((int)s_percent, 1, "%.1f ", TIMEOUT - value);*/

}

char *getinput(void){
  char *tmpbuf;
  int offset;
  int key;
  int i;
  short busy;
  double timer_beg;
  double timer_end;

  offset = 0;
  tmpbuf = calloc(1025, 1);

  for(i = 0; i < screen_height; i++)
    mvaddch(i, 0, ACS_VLINE);

  /* In case we have garbage in our buffer */
  fflush(stdin);
  
  timer_beg = tickcount();
  busy = 1;
  while(busy){
    key = getch();
  
    timer_end = tickcount() - timer_beg;
    if(timer_end >= TIMEOUT)
      busy = 0;

    drawpercent(timer_end);

    switch(key){
    case ERR: break;
    case '\r':
    case '\n':
      busy = 0;
      break;
    case 127:
    case '\b':
      if(offset > 0){
        offset--;
        tmpbuf[offset] = 0;
      }
      break;
    default:
      if(offset < 1024)
        tmpbuf[offset++] = key;
    }

  }

  return tmpbuf;

}

int lock_screen(int w, int h){
  char *pwd;
  char text[128];
  char blank[128];
  int centerx; 
  int centery;
  int result;
  int i;

  bzero(text, 128);
  sprintf(text, "This screen has been locked by %s.", username);
  bzero(blank, 128);
  sprintf(blank,"Password:");
  memset(&blank[9], 32, strlen(text) - 9); 
  blank[strlen(text) + 1] = 0;

  centerx = (w - (strlen(text)/* - 1*/)) / 2;
  centery = (h - 5) / 2;

  attroff(A_BOLD);
  attron(COLOR_PAIR(8));

  /* Draw box. Dirty. */
  for(i = 1; i < strlen(text) + 1; i++){
    mvaddch(centery, centerx + i, ACS_HLINE);
    mvaddch(centery + 3, centerx + i, ACS_HLINE);
  }
  mvaddch(centery, centerx + i, ACS_URCORNER);
  mvaddch(centery + 3, centerx + i, ACS_LRCORNER);
  for(i = 1; i < 3; i++){
    mvaddch(centery + i, centerx, ACS_VLINE);
    mvaddch(centery + i, centerx + strlen(text) + 1, ACS_VLINE);
  }
  mvaddch(centery, centerx, ACS_ULCORNER);
  mvaddch(centery + 3, centerx, ACS_LLCORNER);
  
  mvprintw(centery + 1, centerx + 1, "%s", text);
  mvprintw(centery + 2, centerx + 1, "%s", blank);
  
  /* Get input and check with password via crypt */
  pwd = getinput();

  if(strcmp(crypt(pwd, userpass), userpass) == 0)
    result = 0;
  else
    result = 1;

  bzero(pwd, strlen(pwd));
  free(pwd);
      
  /* If the terminal is closed, we should exit */
  if(isatty(STDIN_FILENO) == 0){
    perror("isatty");
    restore_terminal();
    severe_error("");
  }

  if(result == 1){
    report_failed_login(username/*, pwd*/);
    sleep(lock_delay++);
    mvprintw(centery + 2, centerx + 11, "Sorry.");
    refresh();
    if(lock_delay > 10){
      sleep(5);		/* Punishment */
      lock_delay = 3;
    }else{
      sleep(1);
    }
    failed_logins++;
  }    
    
  clear();
  attroff(COLOR_PAIR(8));
  attron(A_BOLD);

  return result;

}

/* This code is SUID! Don't mess with it! */
static struct passwd *my_getpwuid(uid_t uid){
  struct passwd *pwd;
#ifndef BSD
  struct spwd *sp;
#endif

  pwd = getpwuid(uid);
  if(!pwd)
    severe_error("getpwuid(%d) failed: %s\n", uid, strerror(errno));

#ifndef BSD 
  /* Why the hell was I doing this? Shit. I forgot :| */
  sp = getspnam(pwd->pw_name);    /* Get pw via name instead of uid */
  if(sp)                          /* If it _worked_, */
    pwd->pw_passwd = sp->sp_pwdp; /* Copy it to our buffer */
                                  /* buy WHY! We already have it %!"/&# */

  endspent();
#endif

  return pwd;
}

void colormvprintw(int y, int x, char *buf){
  int i;

  move(y, x);

  for(i = 0; i < strlen(buf); i++){
    if(buf[i] == 27){

      if(buf[i + 2] == 27)
	while(buf[i + 2] == 27)
	  i += 2;
	
      attroff(COLOR_PAIR(current_color));
      current_color = buf[i + 1] - 48;
      attron(COLOR_PAIR(current_color));
      
      i += 2;

    }
    printw("%c", buf[i]);

  }

}

void perform_mirror(void){
  int i, a, b;
	
  for(i = 0; i < ascii_obj.height; i++){
    strcpy(ascii_obj.mirror, ascii_obj.line[i]);
    /* Pre-flip color codes and char */
    for(a = 0; a < strlen(ascii_obj.mirror) - 1; a++)
      if(ascii_obj.mirror[a] == 27){
	ascii_obj.mirror[a] = ascii_obj.mirror[a + 1];
	ascii_obj.mirror[a + 1] = 27;
	a++;
      }
    for(a = 0, b = strlen(ascii_obj.mirror) - 1; 
        a < strlen(ascii_obj.mirror), b >= 0; 
        a++, b--)
      ascii_obj.line[i][b] = ascii_obj.mirror[a];
  }

  /* Correct parralel characters */
  for(i = 0; i < ascii_obj.height; i++)
    for(a = 0; a < strlen(ascii_obj.line[i]); a++)
      for(b = 0; b < strlen(mirrorchr[0]); b++)
	if(ascii_obj.line[i][a] == mirrorchr[0][b]){
	  ascii_obj.line[i][a] = mirrorchr[1][b];
	  break;
	}

}


int main(int argc, char **argv){

  struct stat sc;
  struct vt_mode vtm;
  struct passwd *pwd;
  static sigset_t sig;
  static struct sigaction sig_action;
  static struct termios term;

  struct utsname _uname;
  struct nameEx{
    char text[128];
    char blank[128];
    float x;
    float y;
    int max_x;
    int max_y;
    float direction_x;
    float direction_y;
    float speed;
    int width;
    int height;
  } name[2];


  double loadavg[3];

  char glob_string[MAXPATH];
  char file_name[MAXPATH];
  /*char file_script[MAXPATH];*/

  short special;
  short forced_direction;
  short file_set;
  short mirror;
  short random;
  short busy;
  short screen_too_small;
  short lock;
  short schedule_scroll_replace;
  short default_scrolltext;

  int obj_check[MAXLINES];
  int ansi_check[MAXLINES];
  int offset;
  int name_count;

  int ret;
  int i, a, c;

  int scroll_count;
  int scroll_length;

  unsigned long delay;
  double scroll_delay;
  double scroll_begin;
  double scroll_end;

  scroll_buffer		= NULL;
  ascii_obj.data	= NULL;
  for(i = 0; i < MAXLINES; i++){
    ascii_obj.line[i]	= NULL;
    ascii_obj.blank[i]	= NULL;
  }
  /* Set defaults */
  name[UNAME].speed	= .5;
  name[INFO].speed	= .1;
  ascii_obj.speed	= 1.0;
  forced_direction	= 0;
  special		= 0;
  mirror		= 1;
  current_color		= 8;
  file_set		= 0;
  failed_logins		= 0;
  vfd			= -1;
  lock_delay		= 1; 		/* First failed pass delay in seconds */
  name_count		= 1;
  lock			= 0;
  random		= 0;
  delay			= 120000;	/* Microseconds */
  scroll_delay		= 5;		/* Seconds */
  default_scrolltext 	= 1;
  bzero(file_name, MAXPATH);
  bzero(ascii_obj.mirror, MAXLINES);

  /* Mirrorable characters */
  sprintf(mirrorchr[0], "/\\()<>{}[]bd`'");
  sprintf(mirrorchr[1], "\\/)(><}{][db'`");

  while( (i = getopt_long(argc, argv, "nsrld:a:o:e:i:Vh", long_options, NULL) ) != -1 )
    switch (i) {
    case 'n': mirror		= 0; break;
    case 's': name_count	= 2; break;
    case 'r': random		= 1; break;
    case 'l': lock		= 1; break;
    case 'd': delay = 1000 * atoi(optarg); break;
    case 'a':
	      if(strlen(optarg) >= MAXPATH){
		fprintf(stderr, "Path too long!\n");
                return EXIT_FAILURE;
	      }
	      file_set		= 1;
	      strcpy(file_name, optarg);
	      break;
    case 'o': 
	      if(atof(optarg) < .001 || atof(optarg) > 1.00){
		usage(argv[0]);
                return EXIT_FAILURE;
	      }else
		ascii_obj.speed = atof(optarg);
	      break;
    case 'e': 
	      if(atof(optarg) < .001 || atof(optarg) > 1.00){
		usage(argv[0]);
                return EXIT_FAILURE;
	      }else
		name[UNAME].speed = atof(optarg);
	      break;
    case 'i': 
	      if(atof(optarg) < .001 || atof(optarg) > 1.00){
		usage(argv[0]);
                return EXIT_FAILURE;
	      }else
		name[INFO].speed = atof(optarg);
	      break;
    case 'V': showver(); showcopyright(); return EXIT_SUCCESS;
    case 'h': usage(argv[0]); return EXIT_SUCCESS;
    default: usage(argv[0]); return EXIT_SUCCESS;
    }

  /* Init */
  srand(time(NULL));

  bzero(glob_string, MAXPATH);
  sprintf(glob_string, "%s*", DEFAULT_ASCII_DIR);
  
  if(default_scrolltext == 1){
    scroll_buffer = calloc(37, 1); /* Max load */
    getloadavg(loadavg, 3);
    sprintf(scroll_buffer, "Load average: %.2f, %.2f, %.2f ", 
	    loadavg[0],
	    loadavg[1],
	    loadavg[2]);
  }

  screen_too_small 	= 0;

  /* Get kernel information */
  if(uname(&_uname) == -1)
    severe_error("uname() failed.");

  sprintf(name[UNAME].text, "%s %s %s", 
	  _uname.sysname, 
	  _uname.nodename, 
	  _uname.release);

  bzero(name[INFO].text, 128);
  memset(name[INFO].text, 32, SCROLL_BOX_WIDTH);

  /* Init curses */
  initscr();

  screen_width 		= COLS;
  screen_height 	= LINES;

  if(has_colors()){
    start_color(); /* VT100 Color init */
    init_pair(1, COLOR_BLACK,	COLOR_BLACK);
    init_pair(2, COLOR_RED,	COLOR_BLACK);
    init_pair(3, COLOR_GREEN,	COLOR_BLACK);
    init_pair(4, COLOR_YELLOW,	COLOR_BLACK);
    init_pair(5, COLOR_BLUE,	COLOR_BLACK);
    init_pair(6, COLOR_MAGENTA,	COLOR_BLACK);
    init_pair(7, COLOR_CYAN,	COLOR_BLACK);
    init_pair(8, COLOR_WHITE,	COLOR_BLACK);
  }
  
  curs_set(0);
  raw();
  nodelay(stdscr, TRUE);
  noecho();
  attron(A_BOLD);

  /* Init locking if enabled */
  if(lock){

    if(geteuid() != 0){ 		/* I'm not suid */
      severe_error("I need to be SUID for VT locking.\n");
    }

    pwd = my_getpwuid(getuid());	/* SUID! */

    sprintf(username, "%s", pwd->pw_name);
    sprintf(userpass, "%s", pwd->pw_passwd);
    
    if(strlen(userpass) < 13) /* Pass strings are always >= 13b */
      severe_error("The password for %s is invalid, "
                   "I can't lock the screen.\n", username);

    vfd = open("/dev/tty", O_RDWR);
    if(vfd < 0)
      severe_error("Could not open /dev/tty.\n");

    c = ioctl(vfd, VT_GETMODE, &vtm);
    if(c < 0)
      severe_error("I can't lock this TTY.\n");

    /* Init signal handling */
    sigprocmask(SIG_SETMASK, NULL, &sig);
    sigdelset(&sig, SIGUSR1);
    sigdelset(&sig, SIGUSR2);
    sigaddset(&sig, SIGTSTP);
    sigaddset(&sig, SIGTTIN);
    sigaddset(&sig, SIGTTOU);
    sigaddset(&sig, SIGHUP);
    sigaddset(&sig, SIGCHLD);
    sigaddset(&sig, SIGQUIT);
    sigaddset(&sig, SIGINT);

    sigprocmask(SIG_SETMASK, &sig, &osig); /* Old set in osig */

    /* Clear old set and define callbacks */
    sigemptyset(&(sig_action.sa_mask));
    sig_action.sa_flags   = SA_RESTART;
    sig_action.sa_handler = vt_release;
    sigaction(SIGUSR1,    &sig_action, NULL);

    /* Ignore normal signals */
    sig_action.sa_handler = signal_ignorer; /* Do nothing */
    sigaction(SIGHUP,     &sig_action, NULL);
    sigaction(SIGQUIT,    &sig_action, NULL);
    sigaction(SIGINT,     &sig_action, NULL);
    sigaction(SIGTSTP,    &sig_action, NULL);

    ovtm		= vtm;          /* backup for restore */
    vtm.mode		= VT_PROCESS;
    vtm.relsig		= SIGUSR1;      /* Handled by vt_release() */
    vtm.acqsig		= SIGUSR1;
    vtm.frsig		= SIGUSR1;  

    if(ioctl(vfd, VT_SETMODE, &vtm)) /* Set it */
      severe_error("VT_SETMODE failed: %s", strerror(errno));

    /* Init terminal */
    tcgetattr(STDIN_FILENO, &oterm);
    term = oterm;
    term.c_iflag &= ~BRKINT;
    term.c_iflag |= IGNBRK;
    term.c_lflag &= ~ISIG;

    /* No autoflush */
    /* term.c_lflag |= (ECHO & ECHOCTL); */
    /* term.c_lflag &= ~(ECHO | ECHOCTL); otherwise */
    tcsetattr(STDIN_FILENO, TCSANOW, &term);

  }

  /* Drop SUID */ 
  setuid(getuid());
  setgid(getgid());

  /* check files and directories */
  if(!file_set){ /* Skip directory and file checks if user set ascii */
    ret = glob(glob_string, GLOB_ERR|GLOB_MARK, NULL, &list);
    if(ret != 0){
      fprintf(stderr, "\nCouldn't read \"%s\".\n", DEFAULT_ASCII_DIR);
      bzero(glob_string, strlen(glob_string));
      sprintf(glob_string, "%s/.tss/*", getenv("HOME"));
      ret = glob(glob_string, GLOB_ERR|GLOB_MARK, NULL, &list);
      fflush(stderr);
      if(ret != 0)
	severe_error("Couldn't read \"%s/.tss/\".\n", getenv("HOME"));
    }

    for(i = 0; i < list.gl_pathc; i++){
      if(strncmp(&list.gl_pathv[i][strlen(list.gl_pathv[i]) - 1], "/", 1) == 0)
	severe_error("Directories are not allowed in \"%s\".\n", 
                     DEFAULT_ASCII_DIR);
    }

    if(list.gl_pathc == 0)
      severe_error("\"%s\" contains no files.\n", DEFAULT_ASCII_DIR);

    if(random)
      sprintf(file_name, "%s", list.gl_pathv[rand()%list.gl_pathc]);
    else{
      glob_string[strlen(glob_string) - 1] = 0;
      sprintf(file_name, "%s%s", glob_string, DEFAULT_ASCII);
    }
  }

  if(stat(file_name, &sc) == -1){
    severe_error("Cannot stat \"%s\": %s\n", 
                 file_name,
                 strerror(errno));
  }else{
    if(! S_ISREG(sc.st_mode))
      severe_error("\"%s\" is not a regular file.\n", file_name);
  }
 
  fd_ascii = fopen(file_name, "rb");
  if(!fd_ascii)
    severe_error("\"%s\" could not be read: %s\n", 
                 file_name,
                 strerror(errno));
 
  if(lof(fd_ascii) == 0)
    severe_error("\"%s\" is empty.\n", file_name);

  if(lof(fd_ascii) > MAX_ASCII_SIZE)
    severe_error("\"%s\" is too large(max %db allowed)\n",
                 file_name,
                 MAX_ASCII_SIZE);

  /* Read files */
  /* Read ascii object */
  ascii_obj.data = calloc(lof(fd_ascii), 1);
  fread(ascii_obj.data, 1, lof(fd_ascii), fd_ascii);

  /* Check first two bytes for controls */
  if(lof(fd_ascii) > 2){
    if(ascii_obj.data[0] == 27){
      switch(ascii_obj.data[1]){
      case 'n': 
	mirror = 0;
	special = 1;
	break;
      case 'l': 
	forced_direction = -1;
	special = 1;
	break;
      case 'r': 
	forced_direction = 1;
	special = 1;
	break;
      }
      if(special){
	strncpy(ascii_obj.data, &ascii_obj.data[2], lof(fd_ascii) - 2);
	ascii_obj.data[lof(fd_ascii) - 2] = '\0';
      }
    }
  }
  	 
  fclose(fd_ascii);
  fd_ascii = NULL;
  
  /* Load object in to buffers */
  ascii_obj.width	= 0;
  ascii_obj.height	= 0;
  offset		= 0;

  for(i = 0; i < strlen(ascii_obj.data); i++){
    if(ascii_obj.data[i] == '\n'){
      /*ascii_obj.line[ascii_obj.height] = calloc(offset + 1, 1);*/
      ascii_obj.line[ascii_obj.height] = calloc(512, 1);
      memcpy(ascii_obj.line[ascii_obj.height], 
	     &ascii_obj.data[i - offset], 
	     offset);
      obj_check[ascii_obj.height++] = offset;
      offset = 0;
    }else{
      offset++;
    }
  }

  free(ascii_obj.data);
  ascii_obj.data	= NULL;

  /* Check widest line */
  for(i = 0; i < ascii_obj.height; i++){
    ansi_check[i] = obj_check[i];
    for(a = 0; a < strlen(ascii_obj.line[i]); a++)
      if(ascii_obj.line[i][a] == 27)
	obj_check[i] -= 2;
  }

  for(i = 0; i < ascii_obj.height; i++)
    for(a = 0; a <= i; a++)
      if(obj_check[a] > ascii_obj.width)
	ascii_obj.width = obj_check[a];
  
  for(i = 0; i < ascii_obj.height; i++)
    for(a = 0; a <= i; a++)
      if(ansi_check[a] > ascii_obj.width_real)
	ascii_obj.width_real = ansi_check[a];

  if(ascii_obj.width == 0)
    ascii_obj.width = strlen(ascii_obj.line[0]);

  /* Autopad spacing and allocate blanking area*/
  for(i = 0; i < ascii_obj.height; i++){
    offset = ascii_obj.width - obj_check[i];
    if(offset > 0){
      ascii_obj.line[i] = realloc(ascii_obj.line[i], ansi_check[i] + offset + 1);
      memset(&ascii_obj.line[i][ansi_check[i]], 32, offset);
      ascii_obj.line[i][ansi_check[i] + offset] = '\0';
    }
    /* fprintf(stderr, "Padding line %2d %2d bytes. (%2db in total)\n", i, offset, ansi_check[i] + offset); */
    ascii_obj.blank[i] = calloc(ascii_obj.width + 1, 1);
    memset(ascii_obj.blank[i], 32, ascii_obj.width);
    ascii_obj.blank[i][ascii_obj.width] = '\0';
    
  }

  /* FIXME: Needs to be in same place as nonexistent resizing handler */
  /* Check if terminal is big enough */
  if(screen_width <= ascii_obj.width + 1)
    screen_too_small = 1;

  if(screen_height <= ascii_obj.height + 1)
    screen_too_small = 1;

  if(screen_width < strlen(name[UNAME].text))
    screen_too_small = 1;

  if(lock){
    if(screen_width <= 75) /* Smaller than max usernamee + pwdbox */
      screen_too_small = 1;
    if(screen_height <= 4) /* Smaller than pwdbox height */
      screen_too_small = 1;
  }

  if(screen_too_small)
    severe_error("This terminal is currently too small.\n");

  for(i = 0; i < name_count; i++){
    /* Set blanking areas */
    bzero(name[i].blank, 128);
    memset(name[i].blank, 32, strlen(name[i].text));

    /* Init objs */
    name[i].width		= strlen(name[i].text);
    name[i].height		= 1;
    name[i].max_x		= (screen_width - name[i].width);
    name[i].max_y		= (screen_height - name[i].height);
    name[i].x			= 1 + rand()%(name[i].max_x - 2);
    name[i].y			= 1 + rand()%(name[i].max_y - 2);
  }
    
  name[UNAME].direction_x	= rand()%2?-name[UNAME].speed:name[UNAME].speed;
  name[UNAME].direction_y	= rand()%2?-name[UNAME].speed:name[UNAME].speed;
  
  name[INFO].direction_x	= rand()%2?-name[INFO].speed:name[INFO].speed;
  name[INFO].direction_y	= rand()%2?-name[INFO].speed:name[INFO].speed;

  ascii_obj.max_x	= (screen_width - ascii_obj.width);
  ascii_obj.max_y	= (screen_height - ascii_obj.height);

  ascii_obj.x		=  1 + rand()%(ascii_obj.max_x - 1);
  ascii_obj.y		=  1 + rand()%(ascii_obj.max_y - 1);
  ascii_obj.direction_x	= rand()%2?-ascii_obj.speed:ascii_obj.speed;
  ascii_obj.direction_y	= rand()%2?-ascii_obj.speed:ascii_obj.speed;

  if(forced_direction != 0)
    if(ascii_obj.direction_x != forced_direction)
      perform_mirror();

  /* Init scroller */
  scroll_length = strlen(scroll_buffer);
  scroll_count = 0;
  scroll_begin = tickcount();

  /* Main run */
  busy = 1;
  while(busy){

    /* Scroll check */
    scroll_end = tickcount() - scroll_begin;
    if(scroll_end >= scroll_delay){
      scroll_begin = tickcount();
      schedule_scroll_replace = 1;
    }

    /* Blank */
    for(i = 0; i < name_count; i++)
      mvprintw(name[i].y, name[i].x, "%s", name[i].blank);

    for(i = 0; i < ascii_obj.height; i++)
      mvprintw((ascii_obj.y + i), ascii_obj.x, "%s", ascii_obj.blank[i]);
    
    /* Update vars */
    for(i = 0; i < name_count; i++){
      name[i].x += name[i].direction_x;
      name[i].y += name[i].direction_y;
   
      if(name[i].x < 1 || name[i].x >= name[i].max_x)
	name[i].direction_x = -name[i].direction_x;
 
      if(name[i].y < 1 || name[i].y >= name[i].max_y)
	name[i].direction_y = -name[i].direction_y;
    }

    ascii_obj.x += ascii_obj.direction_x;
    ascii_obj.y += ascii_obj.direction_y;
    
    if(ascii_obj.x < 1 || ascii_obj.x >= ascii_obj.max_x){
      ascii_obj.direction_x = -ascii_obj.direction_x;
	
      /* Mirror ascii */
      if(mirror)
	perform_mirror();
      
    }
    
    if(ascii_obj.y < 1 || ascii_obj.y >= ascii_obj.max_y)
      ascii_obj.direction_y = -ascii_obj.direction_y;

    /* Draw */
    for(i = 0; i < ascii_obj.height; i++)
      colormvprintw((ascii_obj.y + i), ascii_obj.x, ascii_obj.line[i]);
    
    for(i = 0; i < name_count; i++)
      mvprintw(name[i].y, name[i].x, "%s", name[i].text);

    refresh();

    /* Rotate scrolltext */
    if(name_count == 2){
      for(i = 0; i < SCROLL_BOX_WIDTH; i++)
	name[INFO].text[i] = scroll_buffer[(i + scroll_count) % scroll_length];
      name[INFO].text[0] = '[';
      name[INFO].text[SCROLL_BOX_WIDTH-1] = ']';

      if(++scroll_count > scroll_length){
	if(schedule_scroll_replace){
	  /* Get new text */
	  if(default_scrolltext == 1){
	    getloadavg(loadavg, 3);
	    sprintf(scroll_buffer, "Load average: %.2f, %.2f, %.2f ", 
		    loadavg[0],
		    loadavg[1],
		    loadavg[2]);
	  }

	  scroll_length = strlen(scroll_buffer);
	  scroll_count = 0;
	  schedule_scroll_replace = 0;
	}
	    
	scroll_count = 1;
      }
    }

    usleep(delay);
    
    if(getch() != EOF){
      if(lock == 1)
	busy = lock_screen(screen_width, screen_height);
      else
	busy = 0;
    }

  }

  /* Restore signals and terminal if locked */
  if(lock){
    sigprocmask(SIG_SETMASK, &osig, NULL); /* Restore old signals */
    restore_terminal();
  }
  
  endwin();
  cleanup();

  if(failed_logins > 0)
    printf("%d failed login attempts.\n", failed_logins);

  return 0;
}
