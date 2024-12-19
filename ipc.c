/* 
   Unix SMB/Netbios implementation.
   Version 1.9.
   Inter-process communication and named pipe handling
   Copyright (C) Andrew Tridgell 1992-1998

   SMB Version handling
   Copyright (C) John H Terpstra 1995-1998
   
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.
   
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
   
   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
   */
/*
   This file handles the named pipe and mailslot calls
   in the SMBtrans protocol
   */

#include "includes.h"

#ifdef CHECK_TYPES
#undef CHECK_TYPES
#endif
#define CHECK_TYPES 0

extern int DEBUGLEVEL;
extern int max_send;
extern files_struct Files[];
extern connection_struct Connections[];

extern fstring local_machine;
extern fstring myworkgroup;

#define NERR_Success 0
#define NERR_badpass 86
#define NERR_notsupported 50

#define NERR_BASE (2100)
#define NERR_BufTooSmall (NERR_BASE+23)
#define NERR_JobNotFound (NERR_BASE+51)
#define NERR_DestNotFound (NERR_BASE+52)
#define ERROR_INVALID_LEVEL 124
#define ERROR_MORE_DATA 234

#define ACCESS_READ 0x01
#define ACCESS_WRITE 0x02
#define ACCESS_CREATE 0x04

#define SHPWLEN 8		/* share password length */
#define NNLEN 12		/* 8.3 net name length */
#define SNLEN 15		/* service name length */
#define QNLEN 12		/* queue name maximum length */

extern int Client;
extern int oplock_sock;
extern int smb_read_error;

static BOOL api_Unsupported(int cnum,uint16 vuid, char *param,char *data,
			    int mdrcnt,int mprcnt,
			    char **rdata,char **rparam,
			    int *rdata_len,int *rparam_len);
static BOOL api_TooSmall(int cnum,uint16 vuid, char *param,char *data,
			 int mdrcnt,int mprcnt,
			 char **rdata,char **rparam,
			 int *rdata_len,int *rparam_len);


static int CopyExpanded(int cnum, int snum, char** dst, char* src, int* n)
{
  pstring buf;
  int l;

  if (!src || !dst || !n || !(*dst)) return(0);

  StrnCpy(buf,src,sizeof(buf)/2);
  string_sub(buf,"%S",lp_servicename(snum));
  standard_sub(cnum,buf);
  StrnCpy(*dst,buf,*n);
  l = strlen(*dst) + 1;
  (*dst) += l;
  (*n) -= l;
  return l;
}

static int CopyAndAdvance(char** dst, char* src, int* n)
{
  int l;
  if (!src || !dst || !n || !(*dst)) return(0);
  StrnCpy(*dst,src,*n);
  l = strlen(*dst) + 1;
  (*dst) += l;
  (*n) -= l;
  return l;
}

static int StrlenExpanded(int cnum, int snum, char* s)
{
  pstring buf;
  if (!s) return(0);
  StrnCpy(buf,s,sizeof(buf)/2);
  string_sub(buf,"%S",lp_servicename(snum));
  standard_sub(cnum,buf);
  return strlen(buf) + 1;
}

static char* Expand(int cnum, int snum, char* s)
{
  static pstring buf;
  if (!s) return(NULL);
  StrnCpy(buf,s,sizeof(buf)/2);
  string_sub(buf,"%S",lp_servicename(snum));
  standard_sub(cnum,buf);
  return &buf[0];
}

/*******************************************************************
  check a API string for validity when we only need to check the prefix
  ******************************************************************/
static BOOL prefix_ok(char *str,char *prefix)
{
  return(strncmp(str,prefix,strlen(prefix)) == 0);
}


/****************************************************************************
  send a trans reply
  ****************************************************************************/
static void send_trans_reply(char *outbuf,char *data,char *param,uint16 *setup,
			     int ldata,int lparam,int lsetup)
{
  int i;
  int this_ldata,this_lparam;
  int tot_data=0,tot_param=0;
  int align;

  this_lparam = MIN(lparam,max_send - (500+lsetup*SIZEOFWORD)); /* hack */
  this_ldata = MIN(ldata,max_send - (500+lsetup*SIZEOFWORD+this_lparam));

#ifdef CONFUSE_NETMONITOR_MSRPC_DECODING
  /* if you don't want Net Monitor to decode your packets, do this!!! */
  align = ((this_lparam+1)%4);
#else
  align = (this_lparam%4);
#endif

  set_message(outbuf,10+lsetup,align+this_ldata+this_lparam,True);
  if (this_lparam)
    memcpy(smb_buf(outbuf),param,this_lparam);
  if (this_ldata)
    memcpy(smb_buf(outbuf)+this_lparam+align,data,this_ldata);

  SSVAL(outbuf,smb_vwv0,lparam);
  SSVAL(outbuf,smb_vwv1,ldata);
  SSVAL(outbuf,smb_vwv3,this_lparam);
  SSVAL(outbuf,smb_vwv4,smb_offset(smb_buf(outbuf),outbuf));
  SSVAL(outbuf,smb_vwv5,0);
  SSVAL(outbuf,smb_vwv6,this_ldata);
  SSVAL(outbuf,smb_vwv7,smb_offset(smb_buf(outbuf)+this_lparam+align,outbuf));
  SSVAL(outbuf,smb_vwv8,0);
  SSVAL(outbuf,smb_vwv9,lsetup);
  for (i=0;i<lsetup;i++)
    SSVAL(outbuf,smb_vwv10+i*SIZEOFWORD,setup[i]);

  show_msg(outbuf);
  send_smb(Client,outbuf);

  tot_data = this_ldata;
  tot_param = this_lparam;

  while (tot_data < ldata || tot_param < lparam)
    {
      this_lparam = MIN(lparam-tot_param,max_send - 500); /* hack */
      this_ldata = MIN(ldata-tot_data,max_send - (500+this_lparam));

      align = (this_lparam%4);

      set_message(outbuf,10,this_ldata+this_lparam+align,False);
      if (this_lparam)
	memcpy(smb_buf(outbuf),param+tot_param,this_lparam);
      if (this_ldata)
	memcpy(smb_buf(outbuf)+this_lparam+align,data+tot_data,this_ldata);

      SSVAL(outbuf,smb_vwv3,this_lparam);
      SSVAL(outbuf,smb_vwv4,smb_offset(smb_buf(outbuf),outbuf));
      SSVAL(outbuf,smb_vwv5,tot_param);
      SSVAL(outbuf,smb_vwv6,this_ldata);
      SSVAL(outbuf,smb_vwv7,smb_offset(smb_buf(outbuf)+this_lparam+align,outbuf));
      SSVAL(outbuf,smb_vwv8,tot_data);
      SSVAL(outbuf,smb_vwv9,0);

      show_msg(outbuf);
      send_smb(Client,outbuf);

      tot_data += this_ldata;
      tot_param += this_lparam;
    }
}

struct pack_desc {
  char* format;	    /* formatstring for structure */
  char* subformat;  /* subformat for structure */
  char* base;	    /* baseaddress of buffer */
  int buflen;	   /* remaining size for fixed part; on init: length of base */
  int subcount;	    /* count of substructures */
  char* structbuf;  /* pointer into buffer for remaining fixed part */
  int stringlen;    /* remaining size for variable part */		
  char* stringbuf;  /* pointer into buffer for remaining variable part */
  int neededlen;    /* total needed size */
  int usedlen;	    /* total used size (usedlen <= neededlen and usedlen <= buflen) */
  char* curpos;	    /* current position; pointer into format or subformat */
  int errcode;
};

static int get_counter(char** p)
{
  int i, n;
  if (!p || !(*p)) return(1);
  if (!isdigit(**p)) return 1;
  for (n = 0;;) {
    i = **p;
    if (isdigit(i))
      n = 10 * n + (i - '0');
    else
      return n;
    (*p)++;
  }
}

static int getlen(char* p)
{
  int n = 0;
  if (!p) return(0);
  while (*p) {
    switch( *p++ ) {
    case 'W':			/* word (2 byte) */
      n += 2;
      break;
    case 'N':			/* count of substructures (word) at end */
      n += 2;
      break;
    case 'D':			/* double word (4 byte) */
    case 'z':			/* offset to zero terminated string (4 byte) */
    case 'l':			/* offset to user data (4 byte) */
      n += 4;
      break;
    case 'b':			/* offset to data (with counter) (4 byte) */
      n += 4;
      get_counter(&p);
      break;
    case 'B':			/* byte (with optional counter) */
      n += get_counter(&p);
      break;
    }
  }
  return n;
}

static BOOL init_package(struct pack_desc* p, int count, int subcount)
{
  int n = p->buflen;
  int i;

  if (!p->format || !p->base) return(False);

  i = count * getlen(p->format);
  if (p->subformat) i += subcount * getlen(p->subformat);
  p->structbuf = p->base;
  p->neededlen = 0;
  p->usedlen = 0;
  p->subcount = 0;
  p->curpos = p->format;
  if (i > n) {
    p->neededlen = i;
    i = n = 0;
    p->errcode = ERROR_MORE_DATA;
  }
  else
    p->errcode = NERR_Success;
  p->buflen = i;
  n -= i;
  p->stringbuf = p->base + i;
  p->stringlen = n;
  return(p->errcode == NERR_Success);
}

#ifdef __STDC__
static int package(struct pack_desc* p, ...)
{
#else
static int package(va_alist)
va_dcl
{
  struct pack_desc* p;
#endif
  va_list args;
  int needed=0, stringneeded;
  char* str=NULL;
  int is_string=0, stringused;
  int32 temp;

#ifdef __STDC__
  va_start(args,p);
#else
  va_start(args);
  p = va_arg(args,struct pack_desc *);
#endif

  if (!*p->curpos) {
    if (!p->subcount)
      p->curpos = p->format;
    else {
      p->curpos = p->subformat;
      p->subcount--;
    }
  }
#if CHECK_TYPES
  str = va_arg(args,char*);
  if (strncmp(str,p->curpos,strlen(str)) != 0) {
    DEBUG(2,("type error in package: %s instead of %*s\n",str,
 	     strlen(str),p->curpos));
    va_end(args);
#if AJT
    ajt_panic();
#endif  
    return 0;
  }
#endif
  stringneeded = -1;

  if (!p->curpos) return(0);

  switch( *p->curpos++ ) {
  case 'W':			/* word (2 byte) */
    needed = 2;
    temp = va_arg(args,int);
    if (p->buflen >= needed) SSVAL(p->structbuf,0,temp);
    break;
  case 'N':			/* count of substructures (word) at end */
    needed = 2;
    p->subcount = va_arg(args,int);
    if (p->buflen >= needed) SSVAL(p->structbuf,0,p->subcount);
    break;
  case 'D':			/* double word (4 byte) */
    needed = 4;
    temp = va_arg(args,int);
    if (p->buflen >= needed) SIVAL(p->structbuf,0,temp);
    break;
  case 'B':			/* byte (with optional counter) */
    needed = get_counter(&p->curpos);
    {
      char *s = va_arg(args,char*);
      if (p->buflen >= needed) StrnCpy(p->structbuf,s?s:"",needed);
    }
    break;
  case 'z':			/* offset to zero terminated string (4 byte) */
    str = va_arg(args,char*);
    stringneeded = (str ? strlen(str)+1 : 0);
    is_string = 1;
    break;
  case 'l':			/* offset to user data (4 byte) */
    str = va_arg(args,char*);
    stringneeded = va_arg(args,int);
    is_string = 0;
    break;
  case 'b':			/* offset to data (with counter) (4 byte) */
    str = va_arg(args,char*);
    stringneeded = get_counter(&p->curpos);
    is_string = 0;
    break;
  }
  va_end(args);
  if (stringneeded >= 0) {
    needed = 4;
    if (p->buflen >= needed) {
      stringused = stringneeded;
      if (stringused > p->stringlen) {
	stringused = (is_string ? p->stringlen : 0);
	if (p->errcode == NERR_Success) p->errcode = ERROR_MORE_DATA;
      }
      if (!stringused)
	SIVAL(p->structbuf,0,0);
      else {
	SIVAL(p->structbuf,0,PTR_DIFF(p->stringbuf,p->base));
	memcpy(p->stringbuf,str?str:"",stringused);
	if (is_string) p->stringbuf[stringused-1] = '\0';
	p->stringbuf += stringused;
	p->stringlen -= stringused;
	p->usedlen += stringused;
      }
    }
    p->neededlen += stringneeded;
  }
  p->neededlen += needed;
  if (p->buflen >= needed) {
    p->structbuf += needed;
    p->buflen -= needed;
    p->usedlen += needed;
  }
  else {
    if (p->errcode == NERR_Success) p->errcode = ERROR_MORE_DATA;
  }
  return 1;
}

#if CHECK_TYPES
#define PACK(desc,t,v) package(desc,t,v,0,0,0,0)
#define PACKl(desc,t,v,l) package(desc,t,v,l,0,0,0,0)
#else
#define PACK(desc,t,v) package(desc,v)
#define PACKl(desc,t,v,l) package(desc,v,l)
#endif

static void PACKI(struct pack_desc* desc,char *t,int v)
{
  PACK(desc,t,v);
}

static void PACKS(struct pack_desc* desc,char *t,char *v)
{
  PACK(desc,t,v);
}


/****************************************************************************
  get info level for a server list query
  ****************************************************************************/
static BOOL check_server_info(int uLevel, char* id)
{
  switch( uLevel ) {
  case 0:
    if (strcmp(id,"B16") != 0) return False;
    break;
  case 1:
    if (strcmp(id,"B16BBDz") != 0) return False;
    break;
  default: 
    return False;
  }
  return True;
}

struct srv_info_struct
{
  fstring name;
  uint32 type;
  fstring comment;
  fstring domain;
  BOOL server_added;
};


/*******************************************************************
  get server info lists from the files saved by nmbd. Return the
  number of entries
  ******************************************************************/
static int get_server_info(uint32 servertype, 
			   struct srv_info_struct **servers,
			   char *domain)
{
  FILE *f;
  pstring fname;
  int count=0;
  int alloced=0;
  pstring line;
  BOOL local_list_only;

  pstrcpy(fname,lp_lockdir());
  trim_string(fname,NULL,"/");
  pstrcat(fname,"/");
  pstrcat(fname,SERVER_LIST);

  f = fopen(fname,"r");

  if (!f) {
    DEBUG(4,("Can't open %s - %s\n",fname,strerror(errno)));
    return(0);
  }

  /* request for everything is code for request all servers */
  if (servertype == SV_TYPE_ALL) 
	servertype &= ~(SV_TYPE_DOMAIN_ENUM|SV_TYPE_LOCAL_LIST_ONLY);

  local_list_only = (servertype & SV_TYPE_LOCAL_LIST_ONLY);

  DEBUG(4,("Servertype search: %8x\n",servertype));

  while (!feof(f))
  {
    fstring stype;
    struct srv_info_struct *s;
    char *ptr = line;
    BOOL ok = True;
    *ptr = 0;

    fgets(line,sizeof(line)-1,f);
    if (!*line) continue;
    
    if (count == alloced) {
      alloced += 10;
      (*servers) = (struct srv_info_struct *)
	Realloc(*servers,sizeof(**servers)*alloced);
      if (!(*servers)) return(0);
      bzero((char *)((*servers)+count),sizeof(**servers)*(alloced-count));
    }
    s = &(*servers)[count];
    
    if (!next_token(&ptr,s->name   , NULL)) continue;
    if (!next_token(&ptr,stype     , NULL)) continue;
    if (!next_token(&ptr,s->comment, NULL)) continue;
    if (!next_token(&ptr,s->domain , NULL)) {
      /* this allows us to cope with an old nmbd */
      pstrcpy(s->domain,myworkgroup); 
    }
    
    if (sscanf(stype,"%X",&s->type) != 1) { 
      DEBUG(4,("r:host file ")); 
      ok = False; 
    }
    
	/* Filter the servers/domains we return based on what was asked for. */

	/* Check to see if we are being asked for a local list only. */
	if(local_list_only && ((s->type & SV_TYPE_LOCAL_LIST_ONLY) == 0)) {
	  DEBUG(4,("r: local list only"));
	  ok = False;
	}

    /* doesn't match up: don't want it */
    if (!(servertype & s->type)) { 
      DEBUG(4,("r:serv type ")); 
      ok = False; 
    }
    
    if ((servertype & SV_TYPE_DOMAIN_ENUM) != 
	(s->type & SV_TYPE_DOMAIN_ENUM))
      {
	DEBUG(4,("s: dom mismatch "));
	ok = False;
      }
    
    if (!strequal(domain, s->domain) && !(servertype & SV_TYPE_DOMAIN_ENUM))
      {
	ok = False;
      }
    
	/* We should never return a server type with a SV_TYPE_LOCAL_LIST_ONLY set. */
	s->type &= ~SV_TYPE_LOCAL_LIST_ONLY;

    if (ok)
      {
    	DEBUG(4,("**SV** %20s %8x %25s %15s\n",
		 s->name, s->type, s->comment, s->domain));
	
    	s->server_added = True;
    	count++;
      }
    else
      {
	DEBUG(4,("%20s %8x %25s %15s\n",
		 s->name, s->type, s->comment, s->domain));
      }
  }
  
  fclose(f);
  return(count);
}


/*******************************************************************
  fill in a server info structure
  ******************************************************************/
static int fill_srv_info(struct srv_info_struct *service, 
			 int uLevel, char **buf, int *buflen, 
			 char **stringbuf, int *stringspace, char *baseaddr)
{
  int struct_len;
  char* p;
  char* p2;
  int l2;
  int len;
 
  switch (uLevel) {
  case 0: struct_len = 16; break;
  case 1: struct_len = 26; break;
  default: return -1;
  }  
 
  if (!buf)
    {
      len = 0;
      switch (uLevel) 
	{
	case 1:
	  len = strlen(service->comment)+1;
	  break;
	}

      if (buflen) *buflen = struct_len;
      if (stringspace) *stringspace = len;
      return struct_len + len;
    }
  
  len = struct_len;
  p = *buf;
  if (*buflen < struct_len) return -1;
  if (stringbuf)
    {
      p2 = *stringbuf;
      l2 = *stringspace;
    }
  else
    {
      p2 = p + struct_len;
      l2 = *buflen - struct_len;
    }
  if (!baseaddr) baseaddr = p;
  
  switch (uLevel)
    {
    case 0:
      StrnCpy(p,service->name,15);
      break;

    case 1:
      StrnCpy(p,service->name,15);
      SIVAL(p,18,service->type);
      SIVAL(p,22,PTR_DIFF(p2,baseaddr));
      len += CopyAndAdvance(&p2,service->comment,&l2);
      break;
    }

  if (stringbuf)
    {
      *buf = p + struct_len;
      *buflen -= struct_len;
      *stringbuf = p2;
      *stringspace = l2;
    }
  else
    {
      *buf = p2;
      *buflen -= len;
    }
  return len;
}


static BOOL srv_comp(struct srv_info_struct *s1,struct srv_info_struct *s2)
{
  return(strcmp(s1->name,s2->name));
}

/****************************************************************************
  view list of servers available (or possibly domains). The info is
  extracted from lists saved by nmbd on the local host
  ****************************************************************************/
static BOOL api_RNetServerEnum(int cnum, uint16 vuid, char *param, char *data,
			       int mdrcnt, int mprcnt, char **rdata, 
			       char **rparam, int *rdata_len, int *rparam_len)
{
  char *str1 = param+2;
  char *str2 = skip_string(str1,1);
  char *p = skip_string(str2,1);
  int uLevel = SVAL(p,0);
  int buf_len = SVAL(p,2);
  uint32 servertype = IVAL(p,4);
  char *p2;
  int data_len, fixed_len, string_len;
  int f_len = 0, s_len = 0;
  struct srv_info_struct *servers=NULL;
  int counted=0,total=0;
  int i,missed;
  fstring domain;
  BOOL domain_request;
  BOOL local_request;

  /* If someone sets all the bits they don't really mean to set
     DOMAIN_ENUM and LOCAL_LIST_ONLY, they just want all the
     known servers. */

  if (servertype == SV_TYPE_ALL) 
    servertype &= ~(SV_TYPE_DOMAIN_ENUM|SV_TYPE_LOCAL_LIST_ONLY);

  /* If someone sets SV_TYPE_LOCAL_LIST_ONLY but hasn't set
     any other bit (they may just set this bit on it's own) they 
     want all the locally seen servers. However this bit can be 
     set on its own so set the requested servers to be 
     ALL - DOMAIN_ENUM. */

  if ((servertype & SV_TYPE_LOCAL_LIST_ONLY) && !(servertype & SV_TYPE_DOMAIN_ENUM)) 
    servertype = SV_TYPE_ALL & ~(SV_TYPE_DOMAIN_ENUM);

  domain_request = ((servertype & SV_TYPE_DOMAIN_ENUM) != 0);
  local_request = ((servertype & SV_TYPE_LOCAL_LIST_ONLY) != 0);

  p += 8;

  if (!prefix_ok(str1,"WrLehD")) return False;
  if (!check_server_info(uLevel,str2)) return False;
  
  DEBUG(4, ("server request level: %s %8x ", str2, servertype));
  DEBUG(4, ("domains_req:%s ", BOOLSTR(domain_request)));
  DEBUG(4, ("local_only:%s\n", BOOLSTR(local_request)));

  if (strcmp(str1, "WrLehDz") == 0) {
    StrnCpy(domain, p, sizeof(fstring)-1);
  } else {
    StrnCpy(domain, myworkgroup, sizeof(fstring)-1);    
  }

  if (lp_browse_list())
    total = get_server_info(servertype,&servers,domain);

  data_len = fixed_len = string_len = 0;
  missed = 0;

  qsort(servers,total,sizeof(servers[0]),QSORT_CAST srv_comp);

  {
    char *lastname=NULL;

    for (i=0;i<total;i++)
    {
      struct srv_info_struct *s = &servers[i];
      if (lastname && strequal(lastname,s->name)) continue;
      lastname = s->name;
      data_len += fill_srv_info(s,uLevel,0,&f_len,0,&s_len,0);
      DEBUG(4,("fill_srv_info %20s %8x %25s %15s\n",
	       s->name, s->type, s->comment, s->domain));
      
      if (data_len <= buf_len) {
	  counted++;
	  fixed_len += f_len;
	  string_len += s_len;
      } else {
	missed++;
      }
    }
  }

  *rdata_len = fixed_len + string_len;
  *rdata = REALLOC(*rdata,*rdata_len);
  bzero(*rdata,*rdata_len);
  
  p2 = (*rdata) + fixed_len;	/* auxilliary data (strings) will go here */
  p = *rdata;
  f_len = fixed_len;
  s_len = string_len;

  {
    char *lastname=NULL;
    int count2 = counted;
    for (i = 0; i < total && count2;i++)
      {
	struct srv_info_struct *s = &servers[i];
	if (lastname && strequal(lastname,s->name)) continue;
	lastname = s->name;
	fill_srv_info(s,uLevel,&p,&f_len,&p2,&s_len,*rdata);
	DEBUG(4,("fill_srv_info %20s %8x %25s %15s\n",
		 s->name, s->type, s->comment, s->domain));
	count2--;
      }
  }
  
  *rparam_len = 8;
  *rparam = REALLOC(*rparam,*rparam_len);
  SSVAL(*rparam,0,(missed == 0 ? NERR_Success : ERROR_MORE_DATA));
  SSVAL(*rparam,2,0);
  SSVAL(*rparam,4,counted);
  SSVAL(*rparam,6,counted+missed);

  if (servers) free(servers);

  DEBUG(3,("NetServerEnum domain = %s uLevel=%d counted=%d total=%d\n",
	   domain,uLevel,counted,counted+missed));

  return(True);
}


/****************************************************************************
  get info about a share
  ****************************************************************************/
static BOOL check_share_info(int uLevel, char* id)
{
  switch( uLevel ) {
  case 0:
    if (strcmp(id,"B13") != 0) return False;
    break;
  case 1:
    if (strcmp(id,"B13BWz") != 0) return False;
    break;
  case 2:
    if (strcmp(id,"B13BWzWWWzB9B") != 0) return False;
    break;
  case 91:
    if (strcmp(id,"B13BWzWWWzB9BB9BWzWWzWW") != 0) return False;
    break;
  default: return False;
  }
  return True;
}

static int fill_share_info(int cnum, int snum, int uLevel,
 			   char** buf, int* buflen,
 			   char** stringbuf, int* stringspace, char* baseaddr)
{
  int struct_len;
  char* p;
  char* p2;
  int l2;
  int len;
 
  switch( uLevel ) {
  case 0: struct_len = 13; break;
  case 1: struct_len = 20; break;
  case 2: struct_len = 40; break;
  case 91: struct_len = 68; break;
  default: return -1;
  }
  
 
  if (!buf)
    {
      len = 0;
      if (uLevel > 0) len += StrlenExpanded(cnum,snum,lp_comment(snum));
      if (uLevel > 1) len += strlen(lp_pathname(snum)) + 1;
      if (buflen) *buflen = struct_len;
      if (stringspace) *stringspace = len;
      return struct_len + len;
    }
  
  len = struct_len;
  p = *buf;
  if ((*buflen) < struct_len) return -1;
  if (stringbuf)
    {
      p2 = *stringbuf;
      l2 = *stringspace;
    }
  else
    {
      p2 = p + struct_len;
      l2 = (*buflen) - struct_len;
    }
  if (!baseaddr) baseaddr = p;
  
  StrnCpy(p,lp_servicename(snum),13);
  
  if (uLevel > 0)
    {
      int type;
      CVAL(p,13) = 0;
      type = STYPE_DISKTREE;
      if (strequal("IPC$",lp_servicename(snum))) type = STYPE_IPC;
      SSVAL(p,14,type);		/* device type */
      SIVAL(p,16,PTR_DIFF(p2,baseaddr));
      len += CopyExpanded(cnum,snum,&p2,lp_comment(snum),&l2);
    }
  
  if (uLevel > 1)
    {
      SSVAL(p,20,ACCESS_READ|ACCESS_WRITE|ACCESS_CREATE); /* permissions */
      SSVALS(p,22,-1);		/* max uses */
      SSVAL(p,24,1); /* current uses */
      SIVAL(p,26,PTR_DIFF(p2,baseaddr)); /* local pathname */
      len += CopyAndAdvance(&p2,lp_pathname(snum),&l2);
      memset(p+30,0,SHPWLEN+2); /* passwd (reserved), pad field */
    }
  
  if (uLevel > 2)
    {
      memset(p+40,0,SHPWLEN+2);
      SSVAL(p,50,0);
      SIVAL(p,52,0);
      SSVAL(p,56,0);
      SSVAL(p,58,0);
      SIVAL(p,60,0);
      SSVAL(p,64,0);
      SSVAL(p,66,0);
    }
       
  if (stringbuf)
    {
      (*buf) = p + struct_len;
      (*buflen) -= struct_len;
      (*stringbuf) = p2;
      (*stringspace) = l2;
    }
  else
    {
      (*buf) = p2;
      (*buflen) -= len;
    }
  return len;
}

static BOOL api_RNetShareGetInfo(int cnum,uint16 vuid, char *param,char *data,
				 int mdrcnt,int mprcnt,
				 char **rdata,char **rparam,
				 int *rdata_len,int *rparam_len)
{
  char *str1 = param+2;
  char *str2 = skip_string(str1,1);
  char *netname = skip_string(str2,1);
  char *p = skip_string(netname,1);
  int uLevel = SVAL(p,0);
  int snum = find_service(netname);
  
  if (snum < 0) return False;
  
  /* check it's a supported varient */
  if (!prefix_ok(str1,"zWrLh")) return False;
  if (!check_share_info(uLevel,str2)) return False;
 
  *rdata = REALLOC(*rdata,mdrcnt);
  p = *rdata;
  *rdata_len = fill_share_info(cnum,snum,uLevel,&p,&mdrcnt,0,0,0);
  if (*rdata_len < 0) return False;
 
  *rparam_len = 6;
  *rparam = REALLOC(*rparam,*rparam_len);
  SSVAL(*rparam,0,NERR_Success);
  SSVAL(*rparam,2,0);		/* converter word */
  SSVAL(*rparam,4,*rdata_len);
 
  return(True);
}

/****************************************************************************
  view list of shares available
  ****************************************************************************/
static BOOL api_RNetShareEnum(int cnum,uint16 vuid, char *param,char *data,
  			      int mdrcnt,int mprcnt,
  			      char **rdata,char **rparam,
  			      int *rdata_len,int *rparam_len)
{
  char *str1 = param+2;
  char *str2 = skip_string(str1,1);
  char *p = skip_string(str2,1);
  int uLevel = SVAL(p,0);
  int buf_len = SVAL(p,2);
  char *p2;
  int count=lp_numservices();
  int total=0,counted=0;
  BOOL missed = False;
  int i;
  int data_len, fixed_len, string_len;
  int f_len = 0, s_len = 0;
 
  if (!prefix_ok(str1,"WrLeh")) return False;
  if (!check_share_info(uLevel,str2)) return False;
  
  data_len = fixed_len = string_len = 0;
  for (i=0;i<count;i++)
    if (lp_browseable(i) && lp_snum_ok(i))
    {
      total++;
      data_len += fill_share_info(cnum,i,uLevel,0,&f_len,0,&s_len,0);
      if (data_len <= buf_len)
      {
        counted++;
        fixed_len += f_len;
        string_len += s_len;
      }
      else
        missed = True;
    }
  *rdata_len = fixed_len + string_len;
  *rdata = REALLOC(*rdata,*rdata_len);
  memset(*rdata,0,*rdata_len);
  
  p2 = (*rdata) + fixed_len;	/* auxillery data (strings) will go here */
  p = *rdata;
  f_len = fixed_len;
  s_len = string_len;
  for (i = 0; i < count;i++)
    if (lp_browseable(i) && lp_snum_ok(i))
      if (fill_share_info(cnum,i,uLevel,&p,&f_len,&p2,&s_len,*rdata) < 0)
 	break;
  
  *rparam_len = 8;
  *rparam = REALLOC(*rparam,*rparam_len);
  SSVAL(*rparam,0,missed ? ERROR_MORE_DATA : NERR_Success);
  SSVAL(*rparam,2,0);
  SSVAL(*rparam,4,counted);
  SSVAL(*rparam,6,total);
  
  DEBUG(3,("RNetShareEnum gave %d entries of %d (%d %d %d %d)\n",
 	   counted,total,uLevel,
  	   buf_len,*rdata_len,mdrcnt));
  return(True);
}



/****************************************************************************
  get the time of day info
  ****************************************************************************/
static BOOL api_NetRemoteTOD(int cnum,uint16 vuid, char *param,char *data,
			     int mdrcnt,int mprcnt,
			     char **rdata,char **rparam,
			     int *rdata_len,int *rparam_len)
{
  char *p;
  *rparam_len = 4;
  *rparam = REALLOC(*rparam,*rparam_len);

  *rdata_len = 21;
  *rdata = REALLOC(*rdata,*rdata_len);

  SSVAL(*rparam,0,NERR_Success);
  SSVAL(*rparam,2,0);		/* converter word */

  p = *rdata;

  {
    struct tm *t;
    time_t unixdate = time(NULL);

    put_dos_date3(p,0,unixdate); /* this is the time that is looked at
				    by NT in a "net time" operation,
				    it seems to ignore the one below */

    /* the client expects to get localtime, not GMT, in this bit 
       (I think, this needs testing) */
    t = LocalTime(&unixdate);

    SIVAL(p,4,0);		/* msecs ? */
    CVAL(p,8) = t->tm_hour;
    CVAL(p,9) = t->tm_min;
    CVAL(p,10) = t->tm_sec;
    CVAL(p,11) = 0;		/* hundredths of seconds */
    SSVALS(p,12,TimeDiff(unixdate)/60); /* timezone in minutes from GMT */
    SSVAL(p,14,10000);		/* timer interval in 0.0001 of sec */
    CVAL(p,16) = t->tm_mday;
    CVAL(p,17) = t->tm_mon + 1;
    SSVAL(p,18,1900+t->tm_year);
    CVAL(p,20) = t->tm_wday;
  }


  return(True);
}

/****************************************************************************
  get info about the server
  ****************************************************************************/
static BOOL api_RNetServerGetInfo(int cnum,uint16 vuid, char *param,char *data,
				  int mdrcnt,int mprcnt,
				  char **rdata,char **rparam,
				  int *rdata_len,int *rparam_len)
{
  char *str1 = param+2;
  char *str2 = skip_string(str1,1);
  char *p = skip_string(str2,1);
  int uLevel = SVAL(p,0);
  char *p2;
  int struct_len;

  DEBUG(4,("NetServerGetInfo level %d\n",uLevel));

  /* check it's a supported varient */
  if (!prefix_ok(str1,"WrLh")) return False;
  switch( uLevel ) {
  case 0:
    if (strcmp(str2,"B16") != 0) return False;
    struct_len = 16;
    break;
  case 1:
    if (strcmp(str2,"B16BBDz") != 0) return False;
    struct_len = 26;
    break;
  case 2:
    if (strcmp(str2,"B16BBDzDDDWWzWWWWWWWBB21zWWWWWWWWWWWWWWWWWWWWWWz")
	!= 0) return False;
    struct_len = 134;
    break;
  case 3:
    if (strcmp(str2,"B16BBDzDDDWWzWWWWWWWBB21zWWWWWWWWWWWWWWWWWWWWWWzDWz")
	!= 0) return False;
    struct_len = 144;
    break;
  case 20:
    if (strcmp(str2,"DN") != 0) return False;
    struct_len = 6;
    break;
  case 50:
    if (strcmp(str2,"B16BBDzWWzzz") != 0) return False;
    struct_len = 42;
    break;
  default: return False;
  }

  *rdata_len = mdrcnt;
  *rdata = REALLOC(*rdata,*rdata_len);

  p = *rdata;
  p2 = p + struct_len;
  if (uLevel != 20) {
    StrnCpy(p,local_machine,16);
    strupper(p);
  }
  p += 16;
  if (uLevel > 0)
    {
      struct srv_info_struct *servers=NULL;
      int i,count;
      pstring comment;
      uint32 servertype= lp_default_server_announce();

      pstrcpy(comment,lp_serverstring());

      if ((count=get_server_info(SV_TYPE_ALL,&servers,myworkgroup))>0) {
	for (i=0;i<count;i++)
	  if (strequal(servers[i].name,local_machine))
      {
	    servertype = servers[i].type;
	    pstrcpy(comment,servers[i].comment);	    
	  }
      }
      if (servers) free(servers);

      SCVAL(p,0,lp_major_announce_version());
      SCVAL(p,1,lp_minor_announce_version());
      SIVAL(p,2,servertype);

      if (mdrcnt == struct_len) {
	SIVAL(p,6,0);
      } else {
	SIVAL(p,6,PTR_DIFF(p2,*rdata));
	standard_sub(cnum,comment);
	StrnCpy(p2,comment,MAX(mdrcnt - struct_len,0));
	p2 = skip_string(p2,1);
      }
    }
  if (uLevel > 1)
    {
      return False;		/* not yet implemented */
    }

  *rdata_len = PTR_DIFF(p2,*rdata);

  *rparam_len = 6;
  *rparam = REALLOC(*rparam,*rparam_len);
  SSVAL(*rparam,0,NERR_Success);
  SSVAL(*rparam,2,0);		/* converter word */
  SSVAL(*rparam,4,*rdata_len);

  return(True);
}


/****************************************************************************
  get info about the server
  ****************************************************************************/
static BOOL api_NetWkstaGetInfo(int cnum,uint16 vuid, char *param,char *data,
				int mdrcnt,int mprcnt,
				char **rdata,char **rparam,
				int *rdata_len,int *rparam_len)
{
  char *str1 = param+2;
  char *str2 = skip_string(str1,1);
  char *p = skip_string(str2,1);
  char *p2;
  extern pstring sesssetup_user;
  int level = SVAL(p,0);

  DEBUG(4,("NetWkstaGetInfo level %d\n",level));

  *rparam_len = 6;
  *rparam = REALLOC(*rparam,*rparam_len);

  /* check it's a supported varient */
  if (!(level==10 && strcsequal(str1,"WrLh") && strcsequal(str2,"zzzBBzz")))
    return(False);

  *rdata_len = mdrcnt + 1024;
  *rdata = REALLOC(*rdata,*rdata_len);

  SSVAL(*rparam,0,NERR_Success);
  SSVAL(*rparam,2,0);		/* converter word */

  p = *rdata;
  p2 = p + 22;


  SIVAL(p,0,PTR_DIFF(p2,*rdata)); /* host name */
  pstrcpy(p2,local_machine);
  strupper(p2);
  p2 = skip_string(p2,1);
  p += 4;

  SIVAL(p,0,PTR_DIFF(p2,*rdata));
  pstrcpy(p2,sesssetup_user);
  p2 = skip_string(p2,1);
  p += 4;

  SIVAL(p,0,PTR_DIFF(p2,*rdata)); /* login domain */
  pstrcpy(p2,myworkgroup);
  strupper(p2);
  p2 = skip_string(p2,1);
  p += 4;

  SCVAL(p,0,lp_major_announce_version()); /* system version - e.g 4 in 4.1 */
  SCVAL(p,1,lp_minor_announce_version()); /* system version - e.g .1 in 4.1 */
  p += 2;

  SIVAL(p,0,PTR_DIFF(p2,*rdata));
  pstrcpy(p2,myworkgroup);	/* don't know.  login domain?? */
  p2 = skip_string(p2,1);
  p += 4;

  SIVAL(p,0,PTR_DIFF(p2,*rdata)); /* don't know */
  pstrcpy(p2,"");
  p2 = skip_string(p2,1);
  p += 4;

  *rdata_len = PTR_DIFF(p2,*rdata);

  SSVAL(*rparam,4,*rdata_len);

  return(True);
}

/****************************************************************************
  get info about a user

    struct user_info_11 {
        char                usri11_name[21];  0-20 
        char                usri11_pad;       21 
        char                *usri11_comment;  22-25 
        char            *usri11_usr_comment;  26-29
        unsigned short      usri11_priv;      30-31
        unsigned long       usri11_auth_flags; 32-35
        long                usri11_password_age; 36-39
        char                *usri11_homedir; 40-43
        char            *usri11_parms; 44-47
        long                usri11_last_logon; 48-51
        long                usri11_last_logoff; 52-55
        unsigned short      usri11_bad_pw_count; 56-57
        unsigned short      usri11_num_logons; 58-59
        char                *usri11_logon_server; 60-63
        unsigned short      usri11_country_code; 64-65
        char            *usri11_workstations; 66-69
        unsigned long       usri11_max_storage; 70-73
        unsigned short      usri11_units_per_week; 74-75
        unsigned char       *usri11_logon_hours; 76-79
        unsigned short      usri11_code_page; 80-81
    };

where:

  usri11_name specifies the user name for which information is retireved

  usri11_pad aligns the next data structure element to a word boundary

  usri11_comment is a null terminated ASCII comment

  usri11_user_comment is a null terminated ASCII comment about the user

  usri11_priv specifies the level of the privilege assigned to the user.
       The possible values are:

Name             Value  Description
USER_PRIV_GUEST  0      Guest privilege
USER_PRIV_USER   1      User privilege
USER_PRV_ADMIN   2      Administrator privilege

  usri11_auth_flags specifies the account operator privileges. The
       possible values are:

Name            Value   Description
AF_OP_PRINT     0       Print operator


Leach, Naik                                        [Page 28]


INTERNET-DRAFT   CIFS Remote Admin Protocol     January 10, 1997


AF_OP_COMM      1       Communications operator
AF_OP_SERVER    2       Server operator
AF_OP_ACCOUNTS  3       Accounts operator


  usri11_password_age specifies how many seconds have elapsed since the
       password was last changed.

  usri11_home_dir points to a null terminated ASCII string that contains
       the path name of the user's home directory.

  usri11_parms points to a null terminated ASCII string that is set
       aside for use by applications.

  usri11_last_logon specifies the time when the user last logged on.
       This value is stored as the number of seconds elapsed since
       00:00:00, January 1, 1970.

  usri11_last_logoff specifies the time when the user last logged off.
       This value is stored as the number of seconds elapsed since
       00:00:00, January 1, 1970. A value of 0 means the last logoff
       time is unknown.

  usri11_bad_pw_count specifies the number of incorrect passwords
       entered since the last successful logon.

  usri11_log1_num_logons specifies the number of times this user has
       logged on. A value of -1 means the number of logons is unknown.

  usri11_logon_server points to a null terminated ASCII string that
       contains the name of the server to which logon requests are sent.
       A null string indicates logon requests should be sent to the
       domain controller.

  usri11_country_code specifies the country code for the user's language
       of choice.

  usri11_workstations points to a null terminated ASCII string that
       contains the names of workstations the user may log on from.
       There may be up to 8 workstations, with the names separated by
       commas. A null strings indicates there are no restrictions.

  usri11_max_storage specifies the maximum amount of disk space the user
       can occupy. A value of 0xffffffff indicates there are no
       restrictions.

  usri11_units_per_week specifies the equal number of time units into
       which a week is divided. This value must be equal to 168.

  usri11_logon_hours points to a 21 byte (168 bits) string that
       specifies the time during which the user can log on. Each bit
       represents one unique hour in a week. The first bit (bit 0, word
       0) is Sunday, 0:00 to 0:59, the second bit (bit 1, word 0) is



Leach, Naik                                        [Page 29]


INTERNET-DRAFT   CIFS Remote Admin Protocol     January 10, 1997


       Sunday, 1:00 to 1:59 and so on. A null pointer indicates there
       are no restrictions.

  usri11_code_page specifies the code page for the user's language of
       choice

All of the pointers in this data structure need to be treated
specially. The  pointer is a 32 bit pointer. The higher 16 bits need
to be ignored. The converter word returned in the parameters section
needs to be subtracted from the lower 16 bits to calculate an offset
into the return buffer where this ASCII string resides.

There is no auxiliary data in the response.

  ****************************************************************************/

#define usri11_name           0 
#define usri11_pad            21
#define usri11_comment        22
#define usri11_usr_comment    26
#define usri11_full_name      30
#define usri11_priv           34
#define usri11_auth_flags     36
#define usri11_password_age   40
#define usri11_homedir        44
#define usri11_parms          48
#define usri11_last_logon     52
#define usri11_last_logoff    56
#define usri11_bad_pw_count   60
#define usri11_num_logons     62
#define usri11_logon_server   64
#define usri11_country_code   68
#define usri11_workstations   70
#define usri11_max_storage    74
#define usri11_units_per_week 78
#define usri11_logon_hours    80
#define usri11_code_page      84
#define usri11_end            86

#define USER_PRIV_GUEST 0
#define USER_PRIV_USER 1
#define USER_PRIV_ADMIN 2

#define AF_OP_PRINT     0 
#define AF_OP_COMM      1
#define AF_OP_SERVER    2
#define AF_OP_ACCOUNTS  3


static BOOL api_RNetUserGetInfo(int cnum,uint16 vuid, char *param,char *data,
				int mdrcnt,int mprcnt,
				char **rdata,char **rparam,
				int *rdata_len,int *rparam_len)
{
	char *str1 = param+2;
	char *str2 = skip_string(str1,1);
	char *UserName = skip_string(str2,1);
	char *p = skip_string(UserName,1);
	int uLevel = SVAL(p,0);
	char *p2;

    /* get NIS home of a previously validated user - simeon */
    /* With share level security vuid will always be zero.
       Don't depend on vuser being non-null !!. JRA */
    user_struct *vuser = get_valid_user_struct(vuid);
    if(vuser != NULL)
      DEBUG(3,("  Username of UID %d is %s\n", vuser->uid, vuser->name));

    *rparam_len = 6;
    *rparam = REALLOC(*rparam,*rparam_len);

    DEBUG(4,("RNetUserGetInfo level=%d\n", uLevel));
  
	/* check it's a supported variant */
	if (strcmp(str1,"zWrLh") != 0) return False;
	switch( uLevel )
	{
		case 0: p2 = "B21"; break;
		case 1: p2 = "B21BB16DWzzWz"; break;
		case 2: p2 = "B21BB16DWzzWzDzzzzDDDDWb21WWzWW"; break;
		case 10: p2 = "B21Bzzz"; break;
		case 11: p2 = "B21BzzzWDDzzDDWWzWzDWb21W"; break;
		default: return False;
	}

	if (strcmp(p2,str2) != 0) return False;

	*rdata_len = mdrcnt + 1024;
	*rdata = REALLOC(*rdata,*rdata_len);

	SSVAL(*rparam,0,NERR_Success);
	SSVAL(*rparam,2,0);		/* converter word */

	p = *rdata;
	p2 = p + usri11_end;

	memset(p,0,21); 
	fstrcpy(p+usri11_name,UserName); /* 21 bytes - user name */

	if (uLevel > 0)
	{
		SCVAL(p,usri11_pad,0); /* padding - 1 byte */
		*p2 = 0;
	}
	if (uLevel >= 10)
	{
		SIVAL(p,usri11_comment,PTR_DIFF(p2,p)); /* comment */
		pstrcpy(p2,"Comment");
		p2 = skip_string(p2,1);

		SIVAL(p,usri11_usr_comment,PTR_DIFF(p2,p)); /* user_comment */
		pstrcpy(p2,"UserComment");
		p2 = skip_string(p2,1);

		/* EEK! the cifsrap.txt doesn't have this in!!!! */
		SIVAL(p,usri11_full_name,PTR_DIFF(p2,p)); /* full name */
		pstrcpy(p2,((vuser != NULL) ? vuser->real_name : UserName));	/* simeon */
		p2 = skip_string(p2,1);
	}

	if (uLevel == 11) /* modelled after NTAS 3.51 reply */
	{         
		SSVAL(p,usri11_priv,Connections[cnum].admin_user?USER_PRIV_ADMIN:USER_PRIV_USER); 
		SIVAL(p,usri11_auth_flags,AF_OP_PRINT);		/* auth flags */
		SIVALS(p,usri11_password_age,-1);		/* password age */
		SIVAL(p,usri11_homedir,PTR_DIFF(p2,p)); /* home dir */
		pstrcpy(p2, lp_logon_path());
		p2 = skip_string(p2,1);
		SIVAL(p,usri11_parms,PTR_DIFF(p2,p)); /* parms */
		pstrcpy(p2,"");
		p2 = skip_string(p2,1);
		SIVAL(p,usri11_last_logon,0);		/* last logon */
		SIVAL(p,usri11_last_logoff,0);		/* last logoff */
		SSVALS(p,usri11_bad_pw_count,-1);	/* bad pw counts */
		SSVALS(p,usri11_num_logons,-1);		/* num logons */
		SIVAL(p,usri11_logon_server,PTR_DIFF(p2,p)); /* logon server */
		pstrcpy(p2,"\\\\*");
		p2 = skip_string(p2,1);
		SSVAL(p,usri11_country_code,0);		/* country code */

		SIVAL(p,usri11_workstations,PTR_DIFF(p2,p)); /* workstations */
		pstrcpy(p2,"");
		p2 = skip_string(p2,1);

		SIVALS(p,usri11_max_storage,-1);		/* max storage */
		SSVAL(p,usri11_units_per_week,168);		/* units per week */
		SIVAL(p,usri11_logon_hours,PTR_DIFF(p2,p)); /* logon hours */

		/* a simple way to get logon hours at all times. */
		memset(p2,0xff,21);
		SCVAL(p2,21,0);           /* fix zero termination */
		p2 = skip_string(p2,1);

		SSVAL(p,usri11_code_page,0);		/* code page */
	}
	if (uLevel == 1 || uLevel == 2)
	{
		memset(p+22,' ',16);	/* password */
		SIVALS(p,38,-1);		/* password age */
		SSVAL(p,42,
		Connections[cnum].admin_user?USER_PRIV_ADMIN:USER_PRIV_USER);
		SIVAL(p,44,PTR_DIFF(p2,*rdata)); /* home dir */
		pstrcpy(p2,lp_logon_path());
		p2 = skip_string(p2,1);
		SIVAL(p,48,PTR_DIFF(p2,*rdata)); /* comment */
		*p2++ = 0;
		SSVAL(p,52,0);		/* flags */
		SIVAL(p,54,0);		/* script_path */
		if (uLevel == 2)
		{
			SIVAL(p,60,0);		/* auth_flags */
			SIVAL(p,64,PTR_DIFF(p2,*rdata)); /* full_name */
   			pstrcpy(p2,((vuser != NULL) ? vuser->real_name : UserName));
			p2 = skip_string(p2,1);
			SIVAL(p,68,0);		/* urs_comment */
			SIVAL(p,72,PTR_DIFF(p2,*rdata)); /* parms */
			pstrcpy(p2,"");
			p2 = skip_string(p2,1);
			SIVAL(p,76,0);		/* workstations */
			SIVAL(p,80,0);		/* last_logon */
			SIVAL(p,84,0);		/* last_logoff */
			SIVALS(p,88,-1);		/* acct_expires */
			SIVALS(p,92,-1);		/* max_storage */
			SSVAL(p,96,168);	/* units_per_week */
			SIVAL(p,98,PTR_DIFF(p2,*rdata)); /* logon_hours */
			memset(p2,-1,21);
			p2 += 21;
			SSVALS(p,102,-1);	/* bad_pw_count */
			SSVALS(p,104,-1);	/* num_logons */
			SIVAL(p,106,PTR_DIFF(p2,*rdata)); /* logon_server */
			pstrcpy(p2,"\\\\%L");
			standard_sub_basic(p2);
			p2 = skip_string(p2,1);
			SSVAL(p,110,49);	/* country_code */
			SSVAL(p,112,860);	/* code page */
		}
	}

	*rdata_len = PTR_DIFF(p2,*rdata);

	SSVAL(*rparam,4,*rdata_len);	/* is this right?? */

	return(True);
}

/*******************************************************************
  get groups that a user is a member of
  ******************************************************************/
static BOOL api_NetUserGetGroups(int cnum,uint16 vuid, char *param,char *data,
				 int mdrcnt,int mprcnt,
				 char **rdata,char **rparam,
				 int *rdata_len,int *rparam_len)
{
  char *str1 = param+2;
  char *str2 = skip_string(str1,1);
  char *UserName = skip_string(str2,1);
  char *p = skip_string(UserName,1);
  int uLevel = SVAL(p,0);
  char *p2;
  int count=0;

  *rparam_len = 8;
  *rparam = REALLOC(*rparam,*rparam_len);

  /* check it's a supported varient */
  if (strcmp(str1,"zWrLeh") != 0) return False;
  switch( uLevel ) {
  case 0: p2 = "B21"; break;
  default: return False;
  }
  if (strcmp(p2,str2) != 0) return False;

  *rdata_len = mdrcnt + 1024;
  *rdata = REALLOC(*rdata,*rdata_len);

  SSVAL(*rparam,0,NERR_Success);
  SSVAL(*rparam,2,0);		/* converter word */

  p = *rdata;

  /* XXXX we need a real SAM database some day */
  pstrcpy(p,"Users"); p += 21; count++;
  pstrcpy(p,"Domain Users"); p += 21; count++;
  pstrcpy(p,"Guests"); p += 21; count++;
  pstrcpy(p,"Domain Guests"); p += 21; count++;

  *rdata_len = PTR_DIFF(p,*rdata);

  SSVAL(*rparam,4,count);	/* is this right?? */
  SSVAL(*rparam,6,count);	/* is this right?? */

  return(True);
}


static BOOL api_WWkstaUserLogon(int cnum,uint16 vuid, char *param,char *data,
				int mdrcnt,int mprcnt,
				char **rdata,char **rparam,
				int *rdata_len,int *rparam_len)
{
  char *str1 = param+2;
  char *str2 = skip_string(str1,1);
  char *p = skip_string(str2,1);
  int uLevel;
  struct pack_desc desc;
  char* name;
  char* logon_script;

  uLevel = SVAL(p,0);
  name = p + 2;

  bzero(&desc,sizeof(desc));

  DEBUG(3,("WWkstaUserLogon uLevel=%d name=%s\n",uLevel,name));

  /* check it's a supported varient */
  if (strcmp(str1,"OOWb54WrLh") != 0) return False;
  if (uLevel != 1 || strcmp(str2,"WB21BWDWWDDDDDDDzzzD") != 0) return False;
  if (mdrcnt > 0) *rdata = REALLOC(*rdata,mdrcnt);
  desc.base = *rdata;
  desc.buflen = mdrcnt;
  desc.subformat = NULL;
  desc.format = str2;
  
  if (init_package(&desc,1,0))
  {
    PACKI(&desc,"W",0);		/* code */
    PACKS(&desc,"B21",name);	/* eff. name */
    PACKS(&desc,"B","");		/* pad */
    PACKI(&desc,"W",
	  Connections[cnum].admin_user?USER_PRIV_ADMIN:USER_PRIV_USER);
    PACKI(&desc,"D",0);		/* auth flags XXX */
    PACKI(&desc,"W",0);		/* num logons */
    PACKI(&desc,"W",0);		/* bad pw count */
    PACKI(&desc,"D",0);		/* last logon */
    PACKI(&desc,"D",-1);		/* last logoff */
    PACKI(&desc,"D",-1);		/* logoff time */
    PACKI(&desc,"D",-1);		/* kickoff time */
    PACKI(&desc,"D",0);		/* password age */
    PACKI(&desc,"D",0);		/* password can change */
    PACKI(&desc,"D",-1);		/* password must change */
    {
      fstring mypath;
      fstrcpy(mypath,"\\\\");
      fstrcat(mypath,local_machine);
      strupper(mypath);
      PACKS(&desc,"z",mypath); /* computer */
    }
    PACKS(&desc,"z",myworkgroup);/* domain */

/* JHT - By calling lp_logon_script() and standard_sub() we have */
/* made sure all macros are fully substituted and available */
    logon_script = lp_logon_script();
    standard_sub( cnum, logon_script );
    PACKS(&desc,"z", logon_script);		/* script path */
/* End of JHT mods */

    PACKI(&desc,"D",0x00000000);		/* reserved */
  }

  *rdata_len = desc.usedlen;
  *rparam_len = 6;
  *rparam = REALLOC(*rparam,*rparam_len);
  SSVALS(*rparam,0,desc.errcode);
  SSVAL(*rparam,2,0);
  SSVAL(*rparam,4,desc.neededlen);

  DEBUG(4,("WWkstaUserLogon: errorcode %d\n",desc.errcode));
  return(True);
}


/****************************************************************************
  api_WAccessGetUserPerms
  ****************************************************************************/
static BOOL api_WAccessGetUserPerms(int cnum,uint16 vuid, char *param,char *data,
				    int mdrcnt,int mprcnt,
				    char **rdata,char **rparam,
				    int *rdata_len,int *rparam_len)
{
  char *str1 = param+2;
  char *str2 = skip_string(str1,1);
  char *user = skip_string(str2,1);
  char *resource = skip_string(user,1);

  DEBUG(3,("WAccessGetUserPerms user=%s resource=%s\n",user,resource));

  /* check it's a supported varient */
  if (strcmp(str1,"zzh") != 0) return False;
  if (strcmp(str2,"") != 0) return False;

  *rparam_len = 6;
  *rparam = REALLOC(*rparam,*rparam_len);
  SSVALS(*rparam,0,0);		/* errorcode */
  SSVAL(*rparam,2,0);		/* converter word */
  SSVAL(*rparam,4,0x7f);	/* permission flags */

  return(True);
}

struct
{
  char * name;
  char * pipe_clnt_name;
#ifdef NTDOMAIN
  char * pipe_srv_name;
#endif
  int subcommand;
  BOOL (*fn)(int,...);
} api_fd_commands [] =
  {
#ifdef NTDOMAIN
    { "TransactNmPipe",     "lsarpc",	"lsass",	0x26,	api_ntLsarpcTNP },
    { "TransactNmPipe",     "samr",	"lsass",	0x26,	api_samrTNP },
    { "TransactNmPipe",     "srvsvc",	"lsass",	0x26,	api_srvsvcTNP },
    { "TransactNmPipe",     "wkssvc",	"ntsvcs",	0x26,	api_wkssvcTNP },
    { "TransactNmPipe",     "NETLOGON",	"NETLOGON",	0x26,	api_netlogrpcTNP },
    { NULL,		            NULL,       NULL,	-1,	api_Unsupported }
#else
    { "TransactNmPipe"  ,	"lsarpc",	0x26,	(BOOL (*)(int,...)) api_LsarpcTNP },
    { NULL,		NULL,		-1,	(BOOL (*)(int,...)) api_Unsupported }
#endif
  };

/****************************************************************************
  handle remote api calls delivered to a named pipe already opened.
  ****************************************************************************/
static int api_fd_reply(int cnum,uint16 vuid,char *outbuf,
		 	uint16 *setup,char *data,char *params,
		 	int suwcnt,int tdscnt,int tpscnt,int mdrcnt,int mprcnt)
{
  char *rdata = NULL;
  char *rparam = NULL;
  int rdata_len = 0;
  int rparam_len = 0;

  BOOL reply    = False;
  BOOL bind_req = False;
  BOOL set_nphs = False;

  int i;
  int fd;
  int subcommand;
  char *pipe_name;
  
  DEBUG(5,("api_fd_reply\n"));
  /* First find out the name of this file. */
  if (suwcnt != 2)
    {
      DEBUG(0,("Unexpected named pipe transaction.\n"));
      return(-1);
    }
  
  /* Get the file handle and hence the file name. */
  fd = setup[1];
  subcommand = setup[0];
  pipe_name = get_rpc_pipe_hnd_name(fd);

  if (pipe_name == NULL)
  {
    DEBUG(1,("api_fd_reply: INVALID PIPE HANDLE: %x\n", fd));
  }

  DEBUG(3,("Got API command %d on pipe %s (fd %x)",
            subcommand, pipe_name, fd));
  DEBUG(3,("(tdscnt=%d,tpscnt=%d,mdrcnt=%d,mprcnt=%d,cnum=%d,vuid=%d)\n",
	   tdscnt,tpscnt,mdrcnt,mprcnt,cnum,vuid));
  
  for (i = 0; api_fd_commands[i].name; i++)
  {
    if (strequal(api_fd_commands[i].pipe_clnt_name, pipe_name) &&
	    api_fd_commands[i].subcommand == subcommand &&
	    api_fd_commands[i].fn)
    {
	  DEBUG(3,("Doing %s\n", api_fd_commands[i].name));
	  break;
    }
  }
  
  rdata  = (char *)malloc(1024); if (rdata ) bzero(rdata ,1024);
  rparam = (char *)malloc(1024); if (rparam) bzero(rparam,1024);
  
#ifdef NTDOMAIN
  /* RPC Pipe command 0x26. */
  if (data != NULL && api_fd_commands[i].subcommand == 0x26)
  {
    RPC_HDR hdr;

    /* process the rpc header */
    char *q = smb_io_rpc_hdr(True, &hdr, data, data, 4, 0);
    
	/* bind request received */
    if ((bind_req = ((q != NULL) && (hdr.pkt_type == RPC_BIND))))
    {
      RPC_HDR_RB hdr_rb;

      /* decode the bind request */
      char *p = smb_io_rpc_hdr_rb(True, &hdr_rb, q, data, 4, 0);

      if ((bind_req = (p != NULL)))
      {
        RPC_HDR_BA hdr_ba;
        fstring ack_pipe_name;

        /* name has to be \PIPE\xxxxx */
        pstrcpy(ack_pipe_name, "\\PIPE\\");
        pstrcat(ack_pipe_name, api_fd_commands[i].pipe_srv_name);

        /* make a bind acknowledgement */
        make_rpc_hdr_ba(&hdr_ba,
               hdr_rb.bba.max_tsize, hdr_rb.bba.max_rsize, hdr_rb.bba.assoc_gid,
               ack_pipe_name,
               0x1, 0x0, 0x0,
               &(hdr_rb.transfer));

        p = smb_io_rpc_hdr_ba(False, &hdr_ba, rdata + 0x10, rdata, 4, 0);

		rdata_len = PTR_DIFF(p, rdata);

        make_rpc_hdr(&hdr, RPC_BINDACK, 0x0, hdr.call_id, rdata_len);

        p = smb_io_rpc_hdr(False, &hdr, rdata, rdata, 4, 0);
        
        reply = (p != NULL);
      }
    }
  }
#endif

  /* Set Named Pipe Handle state */
  if (subcommand == 0x1)
  {
    set_nphs = True;
    reply = api_LsarpcSNPHS(fd, cnum, params);
  }

  if (!bind_req && !set_nphs)
  {
    DEBUG(10,("calling api_fd_command\n"));

    reply = api_fd_commands[i].fn(cnum,vuid,params,data,mdrcnt,mprcnt,
			        &rdata,&rparam,&rdata_len,&rparam_len);
    DEBUG(10,("called api_fd_command\n"));
  }

  if (rdata_len > mdrcnt || rparam_len > mprcnt)
  {
    reply = api_TooSmall(cnum,vuid,params,data,mdrcnt,mprcnt,
			   &rdata,&rparam,&rdata_len,&rparam_len);
  }
  
  /* if we get False back then it's actually unsupported */
  if (!reply)
  {
    api_Unsupported(cnum,vuid,params,data,mdrcnt,mprcnt,
		    &rdata,&rparam,&rdata_len,&rparam_len);
  }
  
  /* now send the reply */
  send_trans_reply(outbuf,rdata,rparam,NULL,rdata_len,rparam_len,0);
  
  if (rdata ) free(rdata );
  if (rparam) free(rparam);
  
  return(-1);
}



/****************************************************************************
  the buffer was too small
  ****************************************************************************/
static BOOL api_TooSmall(int cnum,uint16 vuid, char *param,char *data,
			 int mdrcnt,int mprcnt,
			 char **rdata,char **rparam,
			 int *rdata_len,int *rparam_len)
{
  *rparam_len = MIN(*rparam_len,mprcnt);
  *rparam = REALLOC(*rparam,*rparam_len);

  *rdata_len = 0;

  SSVAL(*rparam,0,NERR_BufTooSmall);

  DEBUG(3,("Supplied buffer too small in API command\n"));

  return(True);
}


/****************************************************************************
  the request is not supported
  ****************************************************************************/
static BOOL api_Unsupported(int cnum,uint16 vuid, char *param,char *data,
			    int mdrcnt,int mprcnt,
			    char **rdata,char **rparam,
			    int *rdata_len,int *rparam_len)
{
  *rparam_len = 4;
  *rparam = REALLOC(*rparam,*rparam_len);

  *rdata_len = 0;

  SSVAL(*rparam,0,NERR_notsupported);
  SSVAL(*rparam,2,0);		/* converter word */

  DEBUG(3,("Unsupported API command\n"));

  return(True);
}




struct
{
  char *name;
  int id;
  BOOL (*fn)(int,uint16,char *,char *,int,int,char **,char **,int *,int *);
  int flags;
} api_commands[] = {
  {"RNetShareEnum",	0,	api_RNetShareEnum,0},
  {"RNetShareGetInfo",	1,	api_RNetShareGetInfo,0},
  {"RNetServerGetInfo",	13,	api_RNetServerGetInfo,0},
  {"RNetUserGetInfo",	56,	api_RNetUserGetInfo,0},
  {"NetUserGetGroups",	59,	api_NetUserGetGroups,0},
  {"NetWkstaGetInfo",	63,	api_NetWkstaGetInfo,0},
  {"NetRemoteTOD",	91,	api_NetRemoteTOD,0},
  {"NetServerEnum",	104,	api_RNetServerEnum,0},
  {"WAccessGetUserPerms",105,	api_WAccessGetUserPerms,0},
  {"WWkstaUserLogon",	132,	api_WWkstaUserLogon,0},
  {NULL,		-1,	api_Unsupported,0}};


/****************************************************************************
  handle remote api calls
  ****************************************************************************/
static int api_reply(int cnum,uint16 vuid,char *outbuf,char *data,char *params,
		     int tdscnt,int tpscnt,int mdrcnt,int mprcnt)
{
  int api_command = SVAL(params,0);
  char *rdata = NULL;
  char *rparam = NULL;
  int rdata_len = 0;
  int rparam_len = 0;
  BOOL reply=False;
  int i;

  DEBUG(3,("Got API command %d of form <%s> <%s> (tdscnt=%d,tpscnt=%d,mdrcnt=%d,mprcnt=%d)\n",
	   api_command,params+2,skip_string(params+2,1),
	   tdscnt,tpscnt,mdrcnt,mprcnt));

  for (i=0;api_commands[i].name;i++)
    if (api_commands[i].id == api_command && api_commands[i].fn)
      {
	DEBUG(3,("Doing %s\n",api_commands[i].name));
	break;
      }

  rdata = (char *)malloc(1024); if (rdata) bzero(rdata,1024);
  rparam = (char *)malloc(1024); if (rparam) bzero(rparam,1024);

  reply = api_commands[i].fn(cnum,vuid,params,data,mdrcnt,mprcnt,
			     &rdata,&rparam,&rdata_len,&rparam_len);


  if (rdata_len > mdrcnt ||
      rparam_len > mprcnt)
    {
      reply = api_TooSmall(cnum,vuid,params,data,mdrcnt,mprcnt,
			   &rdata,&rparam,&rdata_len,&rparam_len);
    }
	    

  /* if we get False back then it's actually unsupported */
  if (!reply)
    api_Unsupported(cnum,vuid,params,data,mdrcnt,mprcnt,
		    &rdata,&rparam,&rdata_len,&rparam_len);

      

  /* now send the reply */
  send_trans_reply(outbuf,rdata,rparam,NULL,rdata_len,rparam_len,0);

  if (rdata)
    free(rdata);
  if (rparam)
    free(rparam);
  
  return(-1);
}

/****************************************************************************
  handle named pipe commands
  ****************************************************************************/
static int named_pipe(int cnum,uint16 vuid, char *outbuf,char *name,
		      uint16 *setup,char *data,char *params,
		      int suwcnt,int tdscnt,int tpscnt,
		      int msrcnt,int mdrcnt,int mprcnt)
{
	DEBUG(3,("named pipe command on <%s> name\n", name));

	if (strequal(name,"LANMAN"))
	{
		return api_reply(cnum,vuid,outbuf,data,params,tdscnt,tpscnt,mdrcnt,mprcnt);
	}

	if (strlen(name) < 1)
	{
		return api_fd_reply(cnum,vuid,outbuf,setup,data,params,suwcnt,tdscnt,tpscnt,mdrcnt,mprcnt);
	}

	if (setup)
	{
		DEBUG(3,("unknown named pipe: setup 0x%X setup1=%d\n", (int)setup[0],(int)setup[1]));
	}

	return 0;
}


/****************************************************************************
  reply to a SMBtrans
  ****************************************************************************/
int reply_trans(char *inbuf,char *outbuf, int size, int bufsize)
{
  fstring name;

  char *data=NULL,*params=NULL;
  uint16 *setup=NULL;

  int outsize = 0;
  int cnum = SVAL(inbuf,smb_tid);
  uint16 vuid = SVAL(inbuf,smb_uid);

  int tpscnt = SVAL(inbuf,smb_vwv0);
  int tdscnt = SVAL(inbuf,smb_vwv1);
  int mprcnt = SVAL(inbuf,smb_vwv2);
  int mdrcnt = SVAL(inbuf,smb_vwv3);
  int msrcnt = CVAL(inbuf,smb_vwv4);
  BOOL close_on_completion = BITSETW(inbuf+smb_vwv5,0);
  BOOL one_way = BITSETW(inbuf+smb_vwv5,1);
  int pscnt = SVAL(inbuf,smb_vwv9);
  int psoff = SVAL(inbuf,smb_vwv10);
  int dscnt = SVAL(inbuf,smb_vwv11);
  int dsoff = SVAL(inbuf,smb_vwv12);
  int suwcnt = CVAL(inbuf,smb_vwv13);

  bzero(name, sizeof(name));
  fstrcpy(name,smb_buf(inbuf));

  if (dscnt > tdscnt || pscnt > tpscnt) {
	  exit_server("invalid trans parameters\n");
  }
  
  if (tdscnt)
    {
      data = (char *)malloc(tdscnt);
      memcpy(data,smb_base(inbuf)+dsoff,dscnt);
    }
  if (tpscnt)
    {
      params = (char *)malloc(tpscnt);
      memcpy(params,smb_base(inbuf)+psoff,pscnt);
    }

  if (suwcnt)
    {
      int i;
      setup = (uint16 *)malloc(suwcnt*sizeof(setup[0]));
      for (i=0;i<suwcnt;i++)
	setup[i] = SVAL(inbuf,smb_vwv14+i*SIZEOFWORD);
    }


  if (pscnt < tpscnt || dscnt < tdscnt)
    {
      /* We need to send an interim response then receive the rest
	 of the parameter/data bytes */
      outsize = set_message(outbuf,0,0,True);
      show_msg(outbuf);
      send_smb(Client,outbuf);
    }

  /* receive the rest of the trans packet */
  while (pscnt < tpscnt || dscnt < tdscnt)
    {
      BOOL ret;
      int pcnt,poff,dcnt,doff,pdisp,ddisp;
      
      ret = receive_next_smb(Client,oplock_sock,inbuf,bufsize,SMB_SECONDARY_WAIT);

      if ((ret && (CVAL(inbuf, smb_com) != SMBtrans)) || !ret)
	{
          if(ret)
            DEBUG(0,("reply_trans: Invalid secondary trans packet\n"));
          else
            DEBUG(0,("reply_trans: %s in getting secondary trans response.\n",
              (smb_read_error == READ_ERROR) ? "error" : "timeout" ));
	  if (params) free(params);
	  if (data) free(data);
	  if (setup) free(setup);
	  return(ERROR(ERRSRV,ERRerror));
	}

      show_msg(inbuf);
      
      tpscnt = SVAL(inbuf,smb_vwv0);
      tdscnt = SVAL(inbuf,smb_vwv1);

      pcnt = SVAL(inbuf,smb_vwv2);
      poff = SVAL(inbuf,smb_vwv3);
      pdisp = SVAL(inbuf,smb_vwv4);
      
      dcnt = SVAL(inbuf,smb_vwv5);
      doff = SVAL(inbuf,smb_vwv6);
      ddisp = SVAL(inbuf,smb_vwv7);
      
      pscnt += pcnt;
      dscnt += dcnt;

      if (dscnt > tdscnt || pscnt > tpscnt) {
	      exit_server("invalid trans parameters\n");
      }

      if (pcnt)
	memcpy(params+pdisp,smb_base(inbuf)+poff,pcnt);
      if (dcnt)
	memcpy(data+ddisp,smb_base(inbuf)+doff,dcnt);      
    }


  DEBUG(3,("trans <%s> data=%d params=%d setup=%d\n",name,tdscnt,tpscnt,suwcnt));

  if (strncmp(name,"\\PIPE\\",strlen("\\PIPE\\")) == 0)
  {
    DEBUG(5,("calling named_pipe\n"));
    outsize = named_pipe(cnum,vuid,outbuf,name+strlen("\\PIPE\\"),setup,data,params,
			 suwcnt,tdscnt,tpscnt,msrcnt,mdrcnt,mprcnt);
  }
  else
  {
    DEBUG(3,("invalid pipe name\n"));
    outsize = 0;
  }


  if (data) free(data);
  if (params) free(params);
  if (setup) free(setup);

  if (close_on_completion)
    close_cnum(cnum,vuid);

  if (one_way)
    return(-1);
  
  if (outsize == 0)
    return(ERROR(ERRSRV,ERRnosupport));

  return(outsize);
}
