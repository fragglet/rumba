/* 
   Unix SMB/Netbios implementation.
   Version 1.9.
   printcap parsing
   Copyright (C) Karl Auer 1993-1998

   Re-working by Martin Kiff, 1994
   
   Re-written again by Andrew Tridgell

   Modified for SVID support by Norm Jacobs, 1997
   
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
 *  Parse printcap file.
 *
 *  This module does exactly one thing - it looks into the printcap file
 *  and tells callers if a specified string appears as a printer name.
 *
 *  The way this module looks at the printcap file is very simplistic.
 *  Only the local printcap file is inspected (no searching of NIS
 *  databases etc).
 *
 *  There are assumed to be one or more printer names per record, held
 *  as a set of sub-fields separated by vertical bar symbols ('|') in the
 *  first field of the record. The field separator is assumed to be a colon
 *  ':' and the record separator a newline.
 * 
 *  Lines ending with a backspace '\' are assumed to flag that the following
 *  line is a continuation line so that a set of lines can be read as one
 *  printcap entry.
 *
 *  A line stating with a hash '#' is assumed to be a comment and is ignored
 *  Comments are discarded before the record is strung together from the
 *  set of continuation lines.
 *
 *  Opening a pipe for "lpc status" and reading that would probably 
 *  be pretty effective. Code to do this already exists in the freely
 *  distributable PCNFS server code.
 *
 *  Modified to call SVID/XPG4 support if printcap name is set to "lpstat"
 *  in smb.conf under Solaris.
 */

#include "includes.h"

#include "smb.h"

extern int DEBUGLEVEL;

/***************************************************************************
Scan printcap file pszPrintcapname for a printer called pszPrintername. 
Return True if found, else False. Returns False on error, too, after logging 
the error at level 0. For generality, the printcap name may be passed - if
passed as NULL, the configuration will be queried for the name.
***************************************************************************/
BOOL pcap_printername_ok(char *pszPrintername, char *pszPrintcapname)
{
  char *line=NULL;
  char *psz;
  char *p,*q;
  FILE *pfile;

  if (pszPrintername == NULL || pszPrintername[0] == '\0')
    {
      DEBUG(0,( "Attempt to locate null printername! Internal error?\n"));
      return(False);
    }

  /* only go looking if no printcap name supplied */
  if ((psz = pszPrintcapname) == NULL || psz[0] == '\0')
    if (((psz = lp_printcapname()) == NULL) || (psz[0] == '\0'))
      {
	DEBUG(0,( "No printcap file name configured!\n"));
	return(False);
      }



  if ((pfile = fopen(psz, "r")) == NULL)
    {
      DEBUG(0,( "Unable to open printcap file %s for read!\n", psz));
      return(False);
    }

  for (;(line = fgets_slash(NULL,sizeof(pstring),pfile)); free(line))
    {
      if (*line == '#' || *line == 0)
	continue;

      /* now we have a real printer line - cut it off at the first : */      
      p = strchr(line,':');
      if (p) *p = 0;
      
      /* now just check if the name is in the list */
      /* NOTE: I avoid strtok as the fn calling this one may be using it */
      for (p=line; p; p=q)
	{
	  if ((q = strchr(p,'|'))) *q++ = 0;

	  if (strequal(p,pszPrintername))
	    {
	      /* normalise the case */
	      pstrcpy(pszPrintername,p);
	      free(line);
	      fclose(pfile);
	      return(True);	      
	    }
	  p = q;
	}
    }

  fclose(pfile);
  return(False);
}


/***************************************************************************
run a function on each printer name in the printcap file. The function is 
passed the primary name and the comment (if possible)
***************************************************************************/
void pcap_printer_fn(void (*fn)(char *, char*))
{
  pstring name,comment;
  char *line;
  char *psz;
  char *p,*q;
  FILE *pfile;

  /* only go looking if no printcap name supplied */
  if (((psz = lp_printcapname()) == NULL) || (psz[0] == '\0'))
    {
      DEBUG(0,( "No printcap file name configured!\n"));
      return;
    }



  if ((pfile = fopen(psz, "r")) == NULL)
    {
      DEBUG(0,( "Unable to open printcap file %s for read!\n", psz));
      return;
    }

  for (;(line = fgets_slash(NULL,sizeof(pstring),pfile)); free(line))
    {
      if (*line == '#' || *line == 0)
	continue;

      /* now we have a real printer line - cut it off at the first : */      
      p = strchr(line,':');
      if (p) *p = 0;
      
      /* now find the most likely printer name and comment 
       this is pure guesswork, but it's better than nothing */
      *name = 0;
      *comment = 0;
      for (p=line; p; p=q)
	{
	  BOOL has_punctuation;
	  if ((q = strchr(p,'|'))) *q++ = 0;

	  has_punctuation = (strchr(p,' ') || strchr(p,'(') || strchr(p,')'));

	  if (strlen(p)>strlen(comment) && has_punctuation)
	    {
	      StrnCpy(comment,p,sizeof(comment)-1);
	      continue;
	    }

	  if (strlen(p) <= MAXPRINTERLEN && strlen(p)>strlen(name) && !has_punctuation)
	    {
	      if (!*comment) pstrcpy(comment,name);
	      pstrcpy(name,p);
	      continue;
	    }

	  if (!strchr(comment,' ') && 
	      strlen(p) > strlen(comment))
	    {
	      StrnCpy(comment,p,sizeof(comment)-1);
	      continue;
	    }
	}

      comment[60] = 0;
      name[MAXPRINTERLEN] = 0;

      if (*name)
	fn(name,comment);
    }
  fclose(pfile);
}
