/*
cilium sampler without gui 2012.11
gcc -shared -lm -lsndfile -ljack cilium.c -o cilium.so
indent -kr -br -brf -nut -i2 b.c
*/

#include <stdlib.h>
#include <sndfile.h>
#include <jack/jack.h>
#include <sys/fcntl.h>          /* for O_RDWR */
#include <math.h>               /* exp2f */
#include <string.h>	        /* strcpy */
#include <unistd.h>		/* read, sleep */
#define MAXJACKPORTS 8
#define MAXSAMPLES 128
#define MAXVOICES 32
#define STEREOIFY 0

struct sample {
  char filename[80];
  int rate;
  long nframes;
  int nchannels;
  float *wave;
  int voicenum;                 /* Specific voice or 0 for dyn alloc.  Allows cutting off, eg. hi hats */
  int trig;                     /* 0=loop as long as held 1=gate keydown max one loop 2=trigger once thru */
  int out;                      /* Consecutive ports for multichannel sample */
  int midichan;
  float temper;
  float notebase;               /* notebase is also used for fine tuning as a float */
  int notel, noteh;
  float attack, release, vol[MAXJACKPORTS]; /* attack and release in seconds */
} sarray[MAXSAMPLES];

struct voice {
  int state;                    /* off, attack, sustain, release, record = -1 */
  int framestart;		/* where to place sample in first period then set to zero */
  float here, pitch, ratio;     /* DANGER! here = frame unit * s->rate/rate */
  float attack[MAXJACKPORTS];   /* Temporary increment scaled by sample rate and volume */
  float release[MAXJACKPORTS];
  float amp[MAXJACKPORTS];
  float volvel[MAXJACKPORTS];   /* product of sample volume and note velocity */
  struct sample *s;             /* pointer back to the sample from whence it came */
} varray[MAXVOICES];

jack_client_t *client;
jack_port_t *jackport[MAXJACKPORTS];
jack_port_t *inputport[2];
int srate=1;                    /* global sampling rate 1=jack not initialized yet */
float pitchwheel[16];		/* pitchwheel for each channel */
unsigned globframe=0;
unsigned *p;			/* hack daw voice scheduler pointer */
unsigned schedule[100]={0xffffffff};
unsigned mark, loop, converttoframes=48000, npeak=0, lag=0;
char sampleactive, playplay, timer, fileformat=0;
float peak;

void logit (char *s) {
  /*FILE *outf;
  if ((outf=fopen("cilium.log","a"))!=NULL) fputs(s, outf);
  fclose(outf);*/
  fputs(s,stdout);
}

void load_sndfile (struct sample *s, int stereoify) {
  SNDFILE *infile;
  SF_INFO sfinfo;
  int i;
  if ((infile = sf_open(s->filename, SFM_READ, &sfinfo)) != NULL)
    if (s->wave = malloc(sfinfo.frames *
                         ( (sfinfo.channels==1 && stereoify)?2:sfinfo.channels) * sizeof (float)))
      if (sf_readf_float(infile, s->wave, sfinfo.frames) == sfinfo.frames) {
        s->rate = sfinfo.samplerate; /* banish the SF_INFO structure hereonafter */
        s->nframes = sfinfo.frames;
        if (sfinfo.channels==1 && stereoify) {
          for (i=s->nframes-1; i>=0; i--) s->wave[(i+1)*2-1] = s->wave[(i+1)*2-2] = s->wave[i];
          s->nchannels = 2;
        }
	else s->nchannels = sfinfo.channels;
      } else logit ("load_sndfile couldn't sf_readf_float\n");
    else logit ("load_sndfile couldn't malloc\n");
  else logit ("can't load file\n");
  sf_close (infile);
}

void save_sndfile (struct sample *s, char *str) {
  SNDFILE *infile;
  SF_INFO sfinfo;
  static int formats[]={
    SF_FORMAT_WAV  | SF_FORMAT_FLOAT,
    SF_FORMAT_WAV  | SF_FORMAT_PCM_16,
    SF_FORMAT_FLAC | SF_FORMAT_PCM_24 };
  sfinfo.frames=s->nframes; /* required for flac */
  sfinfo.samplerate=s->rate;
  sfinfo.channels=s->nchannels;
  sfinfo.format=formats[fileformat];
  if ((infile = sf_open(str, SFM_WRITE, &sfinfo)) != NULL) {
    sf_writef_float(infile, s->wave, s->nframes);
  } else printf("Couldn't save file %s\n",sf_strerror(infile));
  sf_close (infile);
}

struct sample * load_samples(char *filename, struct sample *s, int soxrate, int soxify) {
  FILE *inf;
  char str[180];
  char basename[180];
  char notebase=36;
  if (rindex(filename,'/')) {
    strncpy(basename,filename,rindex(filename,'/')-filename+1);
    basename[rindex(filename,'/')-filename+1]='\0';
    printf("basename %s\n",basename); }
  if ((inf=fopen(filename,"r")) != NULL) 
    while (1) {
      s->temper=1.0;
      if (1!=(fscanf(inf,"%s",str))) break; /* filename spaces are for pedestrian peabrains */
      if (rindex(str,'/')) {
	strncpy(basename,str,rindex(str,'/')-str+1);
	basename[rindex(str,'/')-str+1]='\0';
	if (soxify) strcpy(s->filename,rindex(str,'/')+1);
	printf("basename %s\n",basename);
      } else strcpy(s->filename,str);
      if (soxify) {
	sprintf(str, "sox -v 0.97 %s%s -e float /tmp/b.wav rate -v %d", basename, s->filename, soxrate);
        printf("str=%s\n",str);
        system(str);		/* sox provides higher quality interpolated samples */
        strcpy(s->filename,"/tmp/b.wav");
      } else strcpy(s->filename,str);
      printf("load %s\n",s->filename);
      load_sndfile(s,STEREOIFY);
      fscanf(inf,"%d %f %d %d",&s->midichan,&s->notebase,&s->notel,&s->noteh);
      fscanf(inf,"%d %f %f %d %f %f",&s->trig, &s->attack, &s->release, &s->out, &s->vol[0], &(s->vol[1]));
      fscanf(inf,"%d",&s->voicenum);
      if (s->notebase==0) s->notebase = notebase; else notebase = s->notebase;
      if (s->notel==0) s->notel = s->notebase;
      if (s->noteh==0) s->noteh = s->notel;
      printf("attack %f release %f vol0 %f  vol1 %f\n",s->attack,s->release,s->vol[0],s->vol[1]);
      notebase++; s++;
    }
  else (logit("load could't fopen file\n"));
  return(s);
}

void updateamplitude (struct voice *v) {
  int i;
  for (i=0; i< v->s->nchannels; i++) {
    if (v->state==1) {
      if ((v->amp[i] += v->attack[i]) >= v->volvel[i]) {
        v->amp[i] = v->volvel[i];
        if (i == v->s->nchannels -1) v->state = 2; /* change state only if last channel */
      }
    } else {
      if (v->state==3)
        if ((v->amp[i] -= v->release[i]) < 0.0) {
          v->amp[i] = 0.0;
	  if (i == v->s->nchannels -1) v->state = 0;
        }}}}			/* Lisp habit */

float interpolate (struct voice *v, int chan) {
  int x0, x1; float f;
  x0 = ((int) v->here) * v->s->nchannels + chan;
  x1 = x0 + v->s->nchannels;
  f = v->here;
  f = f - ((int) (f));
  return(v->s->wave[x0] * (1.0-f) + v->s->wave[x1] * f);
}

void voicerecord (jack_default_audio_sample_t **port, int frames,
	       struct voice *v) {
  int i, j;
  for (i=v->framestart; i<frames; i++, v->here += v->ratio * pitchwheel[v->s->midichan]) {
    if (v->here >= v->s->nframes) {  /* inefficient but simple */
      v->here=0.0;
    }
    for (j=0; j< v->s->nchannels; j++) {
      v->s->wave[((int) v->here - lag) * v->s->nchannels + j]=port[j][i];
      if (port[j][i] > peak) {       /* could use a fabs() */
	peak=port[j][i];
	if (peak > 0.99) npeak++;    /* 0.99 magic fudge */
      }}}
  v->framestart=0;                   /* offset framestart only on first period */
}

void voicejack (jack_default_audio_sample_t **port, int frames,
	       struct voice *v) {
  int i, j;
  float f, ratio;
  ratio=v->s->vol[1] / v->s->vol[0];  /* hack to get pan working on mono track */
  for (i=v->framestart; i<frames; i++, v->here += v->ratio * pitchwheel[v->s->midichan]) {
    if (v->here >= v->s->nframes) {   /* inefficient but simple */
      v->here=0.0;
      if (v->s->trig > 0) { v->state = 0; return; }
    }
    updateamplitude(v);
    for (j=0; j< v->s->nchannels; j++) {
	f = interpolate(v,j) * v->amp[j];
	port[j + v->s->out][i] += f;
	if ((v->s->nchannels)==1)     /* monophonic output to two ports */
	  port[1 + v->s->out][i] += f * ratio;
    }}
  v->framestart=0;		      /* offset framestart only on first period */
}

void vocalise (struct sample *s, struct voice *v, int framestart,
	       float herestart, float pitch, float vel) {
  int i;
  /*printf("vocalize %d %d %f\n", s-sarray, framestart, herestart);*/
  v->s=s;                       /* point back to sample from whence it came */
  v->framestart=framestart;     /* framestart set to zero after first period played */
  v->here=herestart;
  v->pitch = pitch;
  v->ratio = exp2f((pitch - s->notebase)*s->temper/12.0) * s->rate/srate; /* too many multiplies */
  for (i=0; i< v->s->nchannels; i++) {
    v->volvel[i] = v->s->vol[i] * vel/127.0; /* max of amplitude envelope */
    v->attack[i] = (v->s->vol[i]) / ((v->s->attack  + 1e-6) * srate);
    v->release[i]= (v->s->vol[i]) / ((v->s->release + 1e-6) * srate);
    v->amp[i]=0.0;		/* begin with envelope closed */
    /*printf("noteon %f %f %f\n",v->s->attack,v->attack[i],v->release[i]);*/
  }
  v->state=1;
}

int process (jack_nframes_t nframes, void *arg) {
  jack_position_t pos;
  jack_default_audio_sample_t *port[MAXJACKPORTS], *inport[2];
  int i, j;
  unsigned frametostart;
  struct sample *s;
  
  switch (jack_transport_query(client, &pos)) {
  case JackTransportRolling:
    while ((frametostart = *p) != 0xffffffff) { /* schedule only when rolling! */
      globframe=pos.frame;
      /*printf("bar %d globframe %d frametostart %d\n",(int)pos.bar,globframe,frametostart);*/
      if (globframe >= frametostart) {
	s= sarray+ *++p; p++;	/* very tricky cast below, float on s->rate/srate */
        for (i=0; i < MAXVOICES && varray[i].state!=0; i++);
	if (i<MAXVOICES) vocalise(s,varray+i, 0,(float)(globframe-frametostart)*s->rate/srate,36.0,127.0);
      } /* else if (globframe + nframes > frametostart) {
	   s= sarray+ *++p; p++;
	//noteon   vocalise(s,varray+ s->voicenum, frametostart - globframe, 0.0,36.0,127.0);
	   } too much unecessary work.  May lose a fraction of period from sound  */
      else break;	    /* vocalise all possible, then break out of while */
    }

    for (i=0; i<MAXVOICES; i++) {                    /* record before clearing */
      if (varray[i].state == -1) {
	inport[0]=(jack_default_audio_sample_t *) jack_port_get_buffer(inputport[0],nframes);
	inport[1]=(jack_default_audio_sample_t *) jack_port_get_buffer(inputport[1],nframes);
	voicerecord(inport, nframes, varray+i);
      }
    }
    for (i=0; i< MAXJACKPORTS; i++) {                /* clear out old accumulations */
      port[i]= (jack_default_audio_sample_t *) jack_port_get_buffer (jackport[i], nframes);
      for (j=0; j<nframes; j++) port[i][j]=0;
    }
    for (i=0; i<MAXVOICES; i++) {                    /* heart of the matter */
      if (varray[i].state > 0) voicejack(port, nframes, varray+i);
    }
    break;
  case JackTransportStopped: break;
  }

  if (!(timer++ %10)) {
    printf("\033[s \033[0;0H \033[33m  %-8.0f %8d %d %40d %8d\033[u", 20.0f * log10f(peak), npeak,
 	   sampleactive, pos.frame/converttoframes, pos.frame/srate);
    peak=0;
  }
  fflush(stdout);
  if (loop && pos.frame >= loop) jack_transport_locate(client, mark);
  return(0);
}

void noteoff (unsigned char *mev) {
  struct voice *v;
  for (v=varray; v< varray + MAXVOICES; v++)
    if (v->state > 0 && mev[1] == v->pitch
	 && v->s->trig < 2
	 && mev[0]-0x90 == v->s->midichan) { v->state=3;}
}

void noteon (unsigned char *mev) {
  struct sample *s;
  struct voice *v;
  for (s=sarray; s < sarray + MAXSAMPLES; s++) {
    if (mev[1]>=s->notel && mev[1]<=s->noteh && (mev[0]-0x90) == s->midichan) 
      if (s->voicenum) vocalise(s,varray + s->voicenum, 0, 0.0, (float) mev[1], (float) mev[2]);
      else
        for (v=varray; v < varray + MAXVOICES; v++)
	  if (v->state==0) { vocalise(s, v, 0, 0.0, (float) mev[1], (float) mev[2]); break;}
  }
}

void continuouscontroller (unsigned char *mev) { /* korg nanokontrol */
  unsigned char temp[10];
  struct sample *s;
  struct voice *v;
  switch (mev[1]) {
  case 0x11: /* lower button */
    if (mev[2]==0x7f) {
      temp[0]=mev[0] - 0xb0 + 0x90; temp[1]=36; temp[2]=0x7f;
      noteon(temp);
    } else {
      temp[0]=mev[0] - 0xb0 + 0x90; temp[1]=36; temp[2]=0;
      noteoff(temp);
    }
    break;
  case 0x7: /* slider (cc volume) use for channel volume */
    for (s=sarray; s < sarray + MAXSAMPLES; s++)
      if (mev[0]-0xb0 == s->midichan) s->vol[0]=s->vol[1]= mev[2]/128.0; /* breaks pan FIX THIS */
    for (v=varray; v< varray + MAXVOICES; v++)
      if (v->state > 0 /* && 36 == v->pitch && */ && mev[0]-0xb0 == v->s->midichan)
	v->amp[0] = v->amp[1] = mev[2]/128.0; /* breaks pan FIX THIS */
    break;
  case 0xa: /* knob (cc pan) use for pitch */
    pitchwheel[(mev[0]-0xb0)]= exp2f( (mev[2] - 64)/128.0); break;
  }}

void recordtoggle () {
  int i;
  for (i=0; i<MAXVOICES; i++) {               /* search for voice playing given sample */
    if (varray[i].state != 0 && sampleactive== (varray[i].s-sarray)) {
      if (varray[i].state > 0) { varray[i].state= -1; npeak=0;}
      else varray[i].state=1;	/* back to play */
      return;
    }
  }
  printf("sample must be playing to allow recording onto it\n");
}

void readmidi (int mipo) {
  int j;
  static int bytn=0;
  static unsigned char mev[10];
  while (read (mipo, &j, 1) > 0) {
    if (bytn == 0 && (! (j & 0x80))) bytn++; /* running status expect only two bytes */
    mev[bytn]=j;
    if (bytn == 2) {            /* crudely assume all messages are three bytes */
      bytn = 0;
      printf("midi %02x %02x %02x\n",mev[0],mev[1],mev[2]);
      switch (mev[0] & 0xf0) {
      case 0x80: noteoff(mev); break;
      case 0x90:
	if (mev[2]>0) switch (mev[1]) {
	  case 0x54: return; /* exit out of midi handler loop */
	  case 0x53: recordtoggle(); break;
	  case 0x52: sampleactive++; break;
	  case 0x51: sampleactive--; break;
	  default: noteon(mev);
	}
	else noteoff(mev);
	break;
      case 0xb0: continuouscontroller(mev); break;		/* controller */
      case 0xe0:
	pitchwheel[(mev[0]-0xe0)]= exp2f( ((mev[2]<<7) + mev[1] - 8192)/8192.0);
	/*printf("pitch %f\n",pitchwheel[0]);*/
	break;
      }
    }
    else bytn++;
  }
}

int sync_callback (jack_transport_state_t state, jack_position_t * pos, void *arg) {
  int i;
  switch (state) {
   case JackTransportStarting:   /* handle starting here, avoid in process() */
     /*printf("sync %d %d %d\n",(int)(varray[30].here),(int)(varray[31].here),
       (int)(varray[30].here - varray[31].here));*/
    for (i=0; i<MAXVOICES; i++) (varray+i)->state=0; /* turn off current voices */
    p=schedule;					     /* and start new voices */
    break;
  }
  return(1);
}

void jack_init () {
  int i;
  char s[10];
  client = jack_client_open("cilium", JackNullOption, NULL);
  srate = jack_get_sample_rate (client);
  jack_set_process_callback(client, process, 0);
  jack_set_sync_callback (client, sync_callback, 0);
  for (i=0; i<MAXJACKPORTS; i++) {
    sprintf(s,"%d",i);
    jackport[i] = jack_port_register(client, s, JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
  }
  inputport[0] = jack_port_register(client, "in1",JACK_DEFAULT_AUDIO_TYPE,JackPortIsInput, 0);
  inputport[1] = jack_port_register(client, "in2",JACK_DEFAULT_AUDIO_TYPE,JackPortIsInput, 0);
  jack_activate(client);
}

int converttoindex(struct sample *s, int pos) {
return(converttoframes * s->nchannels);
}

#define EXIT 999
#define nmenu 1
#define nchoice 25
char *menu[nmenu][nchoice] = {
  {
   "?",
   "exit",
   "%",
   "!",
   "pipe       s",
   "r",
   "ssampleactive     d",
   "p",
   "locate",
   "mark       d",
   "mipo",
   "lpoint     d",
   "ls",
   "conversion bpmeas bpm",
   "schedule   d d ..",
   "sf         file",
   "cd path",
   "get file",
   "sp mc nb nl nh tr a d jo v1 v2",
   "ns         sec, channels",
   "inc",
   "nframes   f",
   "fileformat c",
   "shift    d",
   "lag   d"
  }
};

int choice (int m, char *s, FILE *fil) {
  int i=0, j;
  char c,inp[20];
  do {
    //printf ("%s", s);
    do {
      c=fgetc(fil);
      if (c=='\n') return(EXIT);
    } while (c==0x20);
    do {
      inp[i++]=c;
      c=fgetc(fil);
    } while (c!=0x20 && c!='\n');
    if (c=='\n') ungetc('\n',fil); /* ensure EXIT next time choice called */
    /*printf("%02x %02x %02x\n",inp[0],inp[1],inp[2]);*/
    inp[i] = 0;                    /* terminate string with null */
    for (i = 0; i < nchoice && strncmp (inp, menu[m][i], 2); i++);
    if (i == 0)
      for (j = 1; j < nchoice && *menu[m][j] != 0; j++)
	printf (" %s\n", menu[m][j]);
    else if (i == nchoice)
      printf (" Unknown command\n");
  } while (i == nchoice);
  return (i);
}

int main (int argc, char **argv) {
  int i, j, k, l, mipo, cmd, stakptr=0; /* no static vars need initialization */
  char str[80], path[80], fil[80];
  float f;
  char notebase=36;
  struct sample *sa;
  FILE *cfil, *stak[10];
  p=schedule;
  
  for (i=0; i<16; i++) pitchwheel[i]=1.0; /* one wheel for each midi channel */
  if ((mipo = open ("/dev/snd/midiC7D1", O_RDWR)) < 0) {
    perror ("fopen C7D1");
  }
  jack_init();			/* do before loading samples and noteons so sampling rate known */

  for (i=argc, argv++; i>1; i--, argv++) {
    printf("argc %d loading %s\n",i,*argv);
    load_samples(*argv, sarray, 96000,1);
  }

  path[0]=0;
  cfil=stak[0]=stdin; 
  while (1) {
  printf("> ");
  while ((cmd=choice (0, "> ", cfil)) != EXIT) {
    switch (cmd) {
    case 1: exit (-1);
    case 2:fgets(str, 80, cfil); break;
    case 3:
      fgets(str, 60, cfil); str[strlen(str)-1]=0;
      system(str);
      break;
    case 4:			/* pipe */
      fscanf(cfil,"%s",str);
      if (!strcmp(str,"stdin")) { /* pop */
	if (stakptr>0) {
	  fclose (stak[stakptr]);
	  cfil= stak[--stakptr];
	} else puts("pi stak error");
      } else cfil=stak[++stakptr]=fopen(str,"rt");
      break;

    case 5: sleep(1); recordtoggle(); break;
    case 6: fscanf(cfil, "%d", &sampleactive); break;
    case 7:
      if (!playplay) jack_transport_start(client);
      else jack_transport_stop(client);
      playplay = (playplay+1)%2;
      break;
    case 8: jack_transport_locate(client, mark); break;
    case 9: fscanf(cfil, "%d", &mark); mark *= converttoframes; break;
    case 10: readmidi(mipo); break;
    case 11: fscanf(cfil, "%d", &loop); loop *= converttoframes; break;
    case 12:
      //fscanf(cfil, "%s %d",str,&i); load_samples(str, sarray+i, 96000,0); break;
      for (i=0; i<MAXSAMPLES; i++) if (sarray[i].nchannels != 0)
	  printf("%d %-12s %d %d %d %d %d\n",i,sarray[i].filename, sarray[i].nframes/srate,
		 sarray[i].nchannels,sarray[i].trig,sarray[i].out,sarray[i].midichan);
      break;
    case 13: fscanf(cfil, "%d %f",&i,&f); converttoframes = i * 60 * srate / f; break;
    case 14:
      i=0;
      do {
	fscanf(cfil, "%d", &j);
	schedule[i++]=j;
      } while (j!= -1);
      for (j=0; j<=i; j++) printf("%d ",schedule[j]);
      break;
    case 15:
      fscanf(cfil, "%s", str);
      save_sndfile(sarray+sampleactive,str);
      break;
    case 16: fscanf(cfil, "%s", &path); break;
    case 17:
      fscanf(cfil, "%s", &str);
      fil[0]=0;
      strcat(fil,path); strcat(fil,str);
      printf("%s\n",fil);
      strcpy((sarray+sampleactive)->filename,fil);
      load_sndfile (sarray+sampleactive, STEREOIFY);
      break;
    case 18:
      sa=sarray+sampleactive;
      fscanf(cfil,"%d %f %d %d",&sa->midichan,&sa->notebase,&sa->notel,&sa->noteh);
      fscanf(cfil,"%d %f %f %d %f %f",&sa->trig, &sa->attack, &sa->release, &sa->out, &sa->vol[0], &(sa->vol[1]));
      fscanf(cfil,"%d",&sa->voicenum);
      if (sa->notebase==0) sa->notebase = notebase; else notebase = sa->notebase;
      if (sa->notel==0) sa->notel = sa->notebase;
      if (sa->noteh==0) sa->noteh = sa->notel;
      sa->temper=1.0;
      notebase++; //sa++;
      break;
    case 19:
      fscanf(cfil, "%d%d", &i,&j);
      sa=sarray+sampleactive;
      sa->rate = srate;
      sa->nframes = srate * i;
      sa->nchannels=j;
      if (!(sa->wave = calloc(sa->nframes, j * sizeof(float)))) printf("bad calloc\n");
      break;
    case 20: sampleactive++; printf("sa %d\n",sampleactive); break;
    case 21:
      sa=sarray+sampleactive;
      fscanf(cfil,"%f",&f);
      sa->nframes = f * converttoframes;
      break;
    case 22: fscanf(cfil, "%d", &fileformat); break;
    case 23:
/*      fscanf(cfil, "%d", &j);
      j *= converttoframes;
      sa=sarray+sampleactive;
      for (i=0; i < (sa->nframes - j) * sa->nchannels; i++)
	sa->wave[i] = sa->wave[i+j];
      for (i= (sa->nframes - j) * sa->nchannels; i >= 0; i--)
	sa->wave[i+j] = sa->wave[i];*/
      fscanf(cfil, "%d%d%d", &j,&k,&l);
      sa=sarray+sampleactive;
      j *= converttoindex(sa, j);
      k *= converttoindex(sa, k);
      l *= converttoindex(sa, l);
      if (j>l) {
      for (i=0; i < k; i++)
	sa->wave[l+i] = sa->wave[j+i];
      } else {
      for (i= k; i >= 0; i--)
	sa->wave[l+i] = sa->wave[j+i];
      }
      break;
    case 24: fscanf(cfil, "%d", &lag); break; 
    }
  }
  }
  return(0);
}
