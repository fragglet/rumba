/*
   Unix SMB/Netbios implementation.
   Version 1.9.
   Password and authentication handling
   Copyright (C) Andrew Tridgell 1992-1998

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

#include "includes.h"


extern int DEBUGLEVEL;
extern int Protocol;

/* users from session setup */
static pstring session_users = "";

/* this holds info on user ids that are already validated for this VC */
/****************************************************************************
add a name to the session users list
****************************************************************************/
void add_session_user(char *user)
{
	fstring suser;
	StrnCpy(suser, user, sizeof(suser) - 1);

	if (!Get_Pwnam(suser, True))
		return;

	if (suser && *suser && !in_list(suser, session_users, False)) {
		if (strlen(suser) + strlen(session_users) + 2 >=
		    sizeof(pstring))
			DEBUG(1, ("Too many session users??\n"));
		else {
			pstrcat(session_users, " ");
			pstrcat(session_users, suser);
		}
	}
}
