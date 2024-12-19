/* 
 *  Unix SMB/Netbios implementation.
 *  Version 1.9.
 *  RPC Pipe client / server routines
 *  Copyright (C) Andrew Tridgell              1992-1997,
 *  Copyright (C) Luke Kenneth Casson Leighton 1996-1997,
 *  Copyright (C) Paul Ashton                       1997.
 *  
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *  
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *  
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "../includes.h"

extern int DEBUGLEVEL;

#ifdef NTDOMAIN

#ifdef USE_ARCFOUR
void arcfour(unsigned char data[16], unsigned char data_out[16], unsigned char data_in[16]);
#endif

/*******************************************************************
makes an LSA_Q_OPEN_POL structure.
********************************************************************/
void make_q_open_pol(LSA_Q_OPEN_POL *r_q, char *server_name,
			uint32 attributes, uint32 sec_qos,
			uint16 desired_access)
{
	if (r_q == NULL) return;

	DEBUG(5,("make_open_pol\n"));

	r_q->ptr = 1; /* undocumented pointer */

	make_unistr2 (&(r_q->uni_server_name), server_name, strlen(server_name));
	make_obj_attr(&(r_q->attr           ), attributes, sec_qos);

	r_q->des_access = desired_access;
}

/*******************************************************************
reads or writes an LSA_Q_OPEN_POL structure.
********************************************************************/
char* lsa_io_q_open_pol(BOOL io, LSA_Q_OPEN_POL *r_q, char *q, char *base, int align, int depth)
{
	if (r_q == NULL) return NULL;

	DEBUG(5,("%s%04x lsa_io_q_open_pol\n", tab_depth(depth), PTR_DIFF(q, base)));
	depth++;

	DBG_RW_IVAL("ptr       ", depth, base, io, q, r_q->ptr       ); q += 4;

	q = smb_io_unistr2 (io, &(r_q->uni_server_name), q, base, align, depth);
	q = smb_io_obj_attr(io, &(r_q->attr           ), q, base, align, depth);

	DBG_RW_SVAL("des_access", depth, base, io, q, r_q->des_access); q += 2;

	return q;
}

/*******************************************************************
reads or writes an LSA_R_OPEN_POL structure.
********************************************************************/
char* lsa_io_r_open_pol(BOOL io, LSA_R_OPEN_POL *r_p, char *q, char *base, int align, int depth)
{
	if (r_p == NULL) return NULL;

	DEBUG(5,("%s%04x lsa_io_r_open_pol\n", tab_depth(depth), PTR_DIFF(q, base)));
	depth++;


	q = smb_io_pol_hnd(io, &(r_p->pol), q, base, align, depth);

	DBG_RW_IVAL("status", depth, base, io, q, r_p->status); q += 4;

	return q;
}

/*******************************************************************
makes an LSA_Q_QUERY_INFO structure.
********************************************************************/
void make_q_query(LSA_Q_QUERY_INFO *q_q, LSA_POL_HND *hnd, uint16 info_class)
{
	if (q_q == NULL || hnd == NULL) return;

	DEBUG(5,("make_q_query\n"));

	memcpy(&(q_q->pol), hnd, sizeof(q_q->pol));

	q_q->info_class = info_class;
}

/*******************************************************************
reads or writes an LSA_Q_QUERY_INFO structure.
********************************************************************/
char* lsa_io_q_query(BOOL io, LSA_Q_QUERY_INFO *q_q, char *q, char *base, int align, int depth)
{
	if (q_q == NULL) return NULL;

	DEBUG(5,("%s%04x lsa_io_q_query\n", tab_depth(depth), PTR_DIFF(q, base)));
	depth++;

	q = smb_io_pol_hnd(io, &(q_q->pol), q, base, align, depth);

	DBG_RW_SVAL("info_class", depth, base, io, q, q_q->info_class); q += 2;

	return q;
}

/*******************************************************************
reads or writes an LSA_Q_ENUM_TRUST_DOM structure.
********************************************************************/
char* lsa_io_q_enum_trust_dom(BOOL io, LSA_Q_ENUM_TRUST_DOM *q_e, char *q, char *base, int align, int depth)
{
	if (q_e == NULL) return NULL;

	DEBUG(5,("%s%04x lsa_io_q_enum_trust_dom\n", tab_depth(depth), PTR_DIFF(q, base)));
	depth++;


	q = smb_io_pol_hnd(io, &(q_e->pol), q, base, align, depth);

	DBG_RW_IVAL("enum_context ", depth, base, io, q, q_e->enum_context ); q += 4;
	DBG_RW_IVAL("preferred_len", depth, base, io, q, q_e->preferred_len); q += 4;

	return q;
}

/*******************************************************************
makes an LSA_R_ENUM_TRUST_DOM structure.
********************************************************************/
void make_r_enum_trust_dom(LSA_R_ENUM_TRUST_DOM *r_e, LSA_POL_HND *hnd, uint32 status)
{
	if (r_e == NULL || hnd == NULL) return;

	DEBUG(5,("make_r_enum_trust_dom\n"));

	memcpy(&(r_e->pol), hnd, sizeof(r_e->pol));
	r_e->status = status;
}

/*******************************************************************
reads or writes an LSA_R_ENUM_TRUST_DOM structure.
********************************************************************/
char* lsa_io_r_enum_trust_dom(BOOL io, LSA_R_ENUM_TRUST_DOM *r_e, char *q, char *base, int align, int depth)
{
	if (r_e == NULL) return NULL;

	DEBUG(5,("%s%04x lsa_io_r_enum_trust_dom\n", tab_depth(depth), PTR_DIFF(q, base)));
	depth++;

	q = smb_io_pol_hnd(io, &(r_e->pol), q, base, align, depth);

	DBG_RW_IVAL("status", depth, base, io, q, r_e->status); q += 4;

	return q;
}

/*******************************************************************
makes an LSA_Q_CLOSE structure.
********************************************************************/
void make_q_close(LSA_Q_CLOSE *q_c, LSA_POL_HND *hnd)
{
	if (q_c == NULL || hnd == NULL) return;

	DEBUG(5,("make_q_close\n"));

	memcpy(&(q_c->pol), hnd, sizeof(q_c->pol));
}


/*******************************************************************
reads or writes an LSA_Q_CLOSE structure.
********************************************************************/
char* lsa_io_q_close(BOOL io, LSA_Q_CLOSE *q_c, char *q, char *base, int align, int depth)
{
	if (q_c == NULL) return NULL;

	DEBUG(5,("%s%04x lsa_io_q_close\n", tab_depth(depth), PTR_DIFF(q, base)));
	depth++;

	q = smb_io_pol_hnd(io, &(q_c->pol), q, base, align, depth);

	return q;
}

/*******************************************************************
makes an LSA_R_CLOSE structure.
********************************************************************/
void make_r_close(LSA_R_CLOSE *q_r, LSA_POL_HND *hnd)
{
	if (q_r == NULL || hnd == NULL) return;

	DEBUG(5,("make_r_close\n"));

	memcpy(&(q_r->pol), hnd, sizeof(q_r->pol));
}


/*******************************************************************
reads or writes an LSA_R_CLOSE structure.
********************************************************************/
char* lsa_io_r_close(BOOL io, LSA_R_CLOSE *r_c, char *q, char *base, int align, int depth)
{
	if (r_c == NULL) return NULL;

	DEBUG(5,("%s%04x lsa_io_r_close\n", tab_depth(depth), PTR_DIFF(q, base)));
	depth++;

	q = smb_io_pol_hnd(io, &(r_c->pol), q, base, align, depth);

	DBG_RW_IVAL("status", depth, base, io, q, r_c->status); q += 4;

	return q;
}

/*******************************************************************
reads or writes an LSA_Q_QUERY_INFO structure.
********************************************************************/
char* lsa_io_r_query(BOOL io, LSA_R_QUERY_INFO *r_q, char *q, char *base, int align, int depth)
{
	if (r_q == NULL) return NULL;

	DEBUG(5,("%s%04x lsa_io_r_query\n", tab_depth(depth), PTR_DIFF(q, base)));
	depth++;

	DBG_RW_IVAL("undoc_buffer", depth, base, io, q, r_q->undoc_buffer); q += 4;

	if (r_q->undoc_buffer != 0)
	{
		DBG_RW_SVAL("info_class", depth, base, io, q, r_q->info_class); q += 2;

		switch (r_q->info_class)
		{
			case 3:
			{
				q = smb_io_dom_query_3(io, &(r_q->dom.id3), q, base, align, depth);
				break;
			}
			case 5:
			{
				q = smb_io_dom_query_5(io, &(r_q->dom.id3), q, base, align, depth);
				break;
			}
			default:
			{
				/* PANIC! */
				break;
			}
		}
	}

	DBG_RW_IVAL("status", depth, base, io, q, r_q->status); q += 4;

	return q;
}

/*******************************************************************
reads or writes a structure.
********************************************************************/
char* lsa_io_q_lookup_sids(BOOL io, LSA_Q_LOOKUP_SIDS *q_s, char *q, char *base, int align, int depth)
{
	int i;

	if (q_s == NULL) return NULL;

	DEBUG(5,("%s%04x lsa_io_q_lookup_sids\n", tab_depth(depth), PTR_DIFF(q, base)));
	depth++;

	q = align_offset(q, base, align);
	
    q = smb_io_pol_hnd(io, &(q_s->pol_hnd), q, base, align, depth); /* policy handle */

	DBG_RW_IVAL("num_entries          ", depth, base, io, q, q_s->num_entries); q += 4;
	DBG_RW_IVAL("buffer_dom_sid       ", depth, base, io, q, q_s->buffer_dom_sid); q += 4; /* undocumented domain SID buffer pointer */
	DBG_RW_IVAL("buffer_dom_name      ", depth, base, io, q, q_s->buffer_dom_name); q += 4; /* undocumented domain name buffer pointer */

	for (i = 0; i < q_s->num_entries; i++)
	{	
		fstring temp;
		slprintf(temp, sizeof(temp)-1, "buffer_lookup_sids[%d] ", i);
		DBG_RW_IVAL(temp, depth, base, io, q, q_s->buffer_lookup_sids[i]); q += 4; /* undocumented domain SID pointers to be looked up. */
	}

	for (i = 0; i < q_s->num_entries; i++)
	{
		q = smb_io_dom_sid(io, &(q_s->dom_sids[i]), q, base, align, depth); /* domain SIDs to be looked up. */
	}

	DBG_RW_PCVAL(False, "undoc                ", depth, base, io, q, q_s->undoc, 16); q += 16; /* completely undocumented 16 bytes */

	return q;
}

/*******************************************************************
reads or writes a structure.
********************************************************************/
char* lsa_io_r_lookup_sids(BOOL io, LSA_R_LOOKUP_SIDS *r_s, char *q, char *base, int align, int depth)
{
	int i;

	if (r_s == NULL) return NULL;

	DEBUG(5,("%s%04x lsa_io_r_lookup_sids\n", tab_depth(depth), PTR_DIFF(q, base)));
	depth++;

	q = align_offset(q, base, align);
	
	q = smb_io_dom_r_ref(io, &(r_s->dom_ref), q, base, align, depth); /* domain reference info */

	DBG_RW_IVAL("num_entries ", depth, base, io, q, r_s->num_entries); q += 4;
	DBG_RW_IVAL("undoc_buffer", depth, base, io, q, r_s->undoc_buffer); q += 4;
	DBG_RW_IVAL("num_entries2", depth, base, io, q, r_s->num_entries2); q += 4;

	for (i = 0; i < r_s->num_entries2; i++)
	{
		q = smb_io_dom_sid2(io, &(r_s->dom_sid[i]), q, base, align, depth); /* domain SIDs being looked up */
	}

	DBG_RW_IVAL("num_entries3", depth, base, io, q, r_s->num_entries3); q += 4;

	DBG_RW_IVAL("status      ", depth, base, io, q, r_s->status); q += 4;

	return q;
}

/*******************************************************************
reads or writes a structure.
********************************************************************/
char* lsa_io_q_lookup_rids(BOOL io, LSA_Q_LOOKUP_RIDS *q_r, char *q, char *base, int align, int depth)
{
	int i;

	if (q_r == NULL) return NULL;

	DEBUG(5,("%s%04x lsa_io_q_lookup_rids\n", tab_depth(depth), PTR_DIFF(q, base)));
	depth++;

	q = align_offset(q, base, align);
	
    q = smb_io_pol_hnd(io, &(q_r->pol_hnd), q, base, align, depth); /* policy handle */

	DBG_RW_IVAL("num_entries    ", depth, base, io, q, q_r->num_entries); q += 4;
	DBG_RW_IVAL("num_entries2   ", depth, base, io, q, q_r->num_entries2); q += 4;
	DBG_RW_IVAL("buffer_dom_sid ", depth, base, io, q, q_r->buffer_dom_sid); q += 4; /* undocumented domain SID buffer pointer */
	DBG_RW_IVAL("buffer_dom_name", depth, base, io, q, q_r->buffer_dom_name); q += 4; /* undocumented domain name buffer pointer */

	for (i = 0; i < q_r->num_entries; i++)
	{
		q = smb_io_dom_name(io, &(q_r->lookup_name[i]), q, base, align, depth); /* names to be looked up */
	}

	DBG_RW_PCVAL(False, "undoc          ", depth, base, io, q, q_r->undoc, UNKNOWN_LEN); q += UNKNOWN_LEN; /* completely undocumented bytes of unknown length */

	return q;
}

/*******************************************************************
reads or writes a structure.
********************************************************************/
char* lsa_io_r_lookup_rids(BOOL io, LSA_R_LOOKUP_RIDS *r_r, char *q, char *base, int align, int depth)
{
	int i;

	if (r_r == NULL) return NULL;

	DEBUG(5,("%s%04x lsa_io_r_lookup_rids\n", tab_depth(depth), PTR_DIFF(q, base)));
	depth++;

	q = align_offset(q, base, align);
	
	q = smb_io_dom_r_ref(io, &(r_r->dom_ref), q, base, align, depth); /* domain reference info */

	DBG_RW_IVAL("num_entries ", depth, base, io, q, r_r->num_entries); q += 4;
	DBG_RW_IVAL("undoc_buffer", depth, base, io, q, r_r->undoc_buffer); q += 4;
	DBG_RW_IVAL("num_entries2", depth, base, io, q, r_r->num_entries2); q += 4;

	for (i = 0; i < r_r->num_entries2; i++)
	{
		q = smb_io_dom_rid2(io, &(r_r->dom_rid[i]), q, base, align, depth); /* domain RIDs being looked up */
	}

	DBG_RW_IVAL("num_entries3", depth, base, io, q, r_r->num_entries3); q += 4;

	DBG_RW_IVAL("status      ", depth, base, io, q, r_r->status); q += 4;

	return q;
}

/*******************************************************************
makes an LSA_Q_REQ_CHAL structure.
********************************************************************/
void make_q_req_chal(LSA_Q_REQ_CHAL *q_c,
				char *logon_srv, char *logon_clnt,
				DOM_CHAL *clnt_chal)
{
	if (q_c == NULL) return;

	DEBUG(5,("make_q_req_chal: %d\n", __LINE__));

	q_c->undoc_buffer = 1; /* don't know what this buffer is */

	make_unistr2(&(q_c->uni_logon_srv ), logon_srv , strlen(logon_srv ));
	make_unistr2(&(q_c->uni_logon_clnt), logon_clnt, strlen(logon_clnt));

	memcpy(q_c->clnt_chal.data, clnt_chal->data, sizeof(clnt_chal->data));

	DEBUG(5,("make_q_req_chal: %d\n", __LINE__));
}

/*******************************************************************
reads or writes an LSA_Q_REQ_CHAL structure.
********************************************************************/
char* lsa_io_q_req_chal(BOOL io, LSA_Q_REQ_CHAL *q_c, char *q, char *base, int align, int depth)
{
	if (q_c == NULL) return NULL;

	DEBUG(5,("%s%04x lsa_io_q_req_chal\n", tab_depth(depth), PTR_DIFF(q, base)));
	depth++;

	q = align_offset(q, base, align);
    
	DBG_RW_IVAL("undoc_buffer", depth, base, io, q, q_c->undoc_buffer); q += 4;

	q = smb_io_unistr2(io, &(q_c->uni_logon_srv), q, base, align, depth); /* logon server unicode string */
	q = smb_io_unistr2(io, &(q_c->uni_logon_clnt), q, base, align, depth); /* logon client unicode string */

	/* client challenge is _not_ aligned after the unicode strings */
	q = smb_io_chal(io, &(q_c->clnt_chal), q, base, 0, depth); /* client challenge */

	return q;
}

/*******************************************************************
reads or writes a structure.
********************************************************************/
char* lsa_io_r_req_chal(BOOL io, LSA_R_REQ_CHAL *r_c, char *q, char *base, int align, int depth)
{
	if (r_c == NULL) return NULL;

	DEBUG(5,("%s%04x lsa_io_r_req_chal\n", tab_depth(depth), PTR_DIFF(q, base)));
	depth++;

	q = align_offset(q, base, align);
    
	q = smb_io_chal(io, &(r_c->srv_chal), q, base, align, depth); /* server challenge */

	DBG_RW_IVAL("status", depth, base, io, q, r_c->status); q += 4;

	return q;
}


/*******************************************************************
reads or writes a structure.
********************************************************************/
void make_q_auth_2(LSA_Q_AUTH_2 *q_a,
		char *logon_srv, char *acct_name, uint16 sec_chan, char *comp_name,
		DOM_CHAL *clnt_chal, uint32 clnt_flgs)
{
	if (q_a == NULL) return;

	DEBUG(5,("make_q_auth_2: %d\n", __LINE__));

	make_log_info(&(q_a->clnt_id), logon_srv, acct_name, sec_chan, comp_name);
	memcpy(q_a->clnt_chal.data, clnt_chal->data, sizeof(clnt_chal->data));
	q_a->clnt_flgs.neg_flags = clnt_flgs;

	DEBUG(5,("make_q_auth_2: %d\n", __LINE__));
}

/*******************************************************************
reads or writes a structure.
********************************************************************/
char* lsa_io_q_auth_2(BOOL io, LSA_Q_AUTH_2 *q_a, char *q, char *base, int align, int depth)
{
	if (q_a == NULL) return NULL;

	DEBUG(5,("%s%04x lsa_io_q_auth_2\n", tab_depth(depth), PTR_DIFF(q, base)));
	depth++;

	q = align_offset(q, base, align);
    
	q = smb_io_log_info (io, &(q_a->clnt_id), q, base, align, depth); /* client identification info */
	/* client challenge is _not_ aligned */
	q = smb_io_chal     (io, &(q_a->clnt_chal), q, base, 0, depth); /* client-calculated credentials */
	q = smb_io_neg_flags(io, &(q_a->clnt_flgs), q, base, align, depth);

	return q;
}

/*******************************************************************
reads or writes a structure.
********************************************************************/
char* lsa_io_r_auth_2(BOOL io, LSA_R_AUTH_2 *r_a, char *q, char *base, int align, int depth)
{
	if (r_a == NULL) return NULL;

	DEBUG(5,("%s%04x lsa_io_r_auth_2\n", tab_depth(depth), PTR_DIFF(q, base)));
	depth++;

	q = align_offset(q, base, align);
    
	q = smb_io_chal     (io, &(r_a->srv_chal), q, base, align, depth); /* server challenge */
	q = smb_io_neg_flags(io, &(r_a->srv_flgs), q, base, align, depth);

	DBG_RW_IVAL("status", depth, base, io, q, r_a->status); q += 4;

	return q;
}


/*******************************************************************
reads or writes a structure.
********************************************************************/
void make_q_srv_pwset(LSA_Q_SRV_PWSET *q_s, char sess_key[8],
		char *logon_srv, char *acct_name, uint16 sec_chan, char *comp_name,
		DOM_CRED *cred, char nt_cypher[16])
{
	char arc4_nt_owf[16];

	if (q_s == NULL || cred == NULL) return;

	bzero(arc4_nt_owf, sizeof(arc4_nt_owf));

	DEBUG(5,("make_q_srv_pwset\n"));

	make_clnt_info(&(q_s->clnt_id), logon_srv, acct_name, sec_chan, comp_name, cred);

#ifdef USE_ARCFOUR

	if (nt_cypher)
	{
		unsigned char arc4_key[16];
#ifdef DEBUG_PASSWORD
		DEBUG(100,("nt cypher:"));
		dump_data(100, nt_cypher, 16);
#endif

		memset(arc4_key, 0, 16);
		memcpy(arc4_key, sess_key, 8);

		arcfour(arc4_key, arc4_nt_owf, nt_cypher);

#ifdef DEBUG_PASSWORD
		DEBUG(100,("arcfour encrypt of nt owf password:"));
		dump_data(100, arc4_nt_owf, 16);
#endif
		/* set up pointers to cypher blocks */
		nt_cypher = arc4_nt_owf;
	}

#else

	if (nt_cypher)
	{
		/* oops.  can only send what-ever-it-is direct */
		memcpy(arc4_nt_owf, nt_cypher, 16);
		nt_cypher = arc4_nt_owf;
	}

#endif

	memcpy(q_s->pwd, nt_cypher, sizeof(q_s->pwd)); 
}

/*******************************************************************
reads or writes a structure.
********************************************************************/
char* lsa_io_q_srv_pwset(BOOL io, LSA_Q_SRV_PWSET *q_s, char *q, char *base, int align, int depth)
{
	if (q_s == NULL) return NULL;

	DEBUG(5,("%s%04x lsa_io_q_srv_pwset\n", tab_depth(depth), PTR_DIFF(q, base)));
	depth++;

	q = align_offset(q, base, align);
    
	q = smb_io_clnt_info(io, &(q_s->clnt_id), q, base, align, depth); /* client identification/authentication info */
	DBG_RW_PCVAL(False, "pwd", depth, base, io, q, q_s->pwd, 16); q += 16; /* new password - undocumented */

	return q;
}

/*******************************************************************
reads or writes a structure.
********************************************************************/
char* lsa_io_r_srv_pwset(BOOL io, LSA_R_SRV_PWSET *r_s, char *q, char *base, int align, int depth)
{
	if (r_s == NULL) return NULL;

	DEBUG(5,("%s%04x lsa_io_r_srv_pwset\n", tab_depth(depth), PTR_DIFF(q, base)));
	depth++;

	q = align_offset(q, base, align);
    
	q = smb_io_cred(io, &(r_s->srv_cred), q, base, align, depth); /* server challenge */

	DBG_RW_IVAL("status", depth, base, io, q, r_s->status); q += 4;

	return q;
}


/*******************************************************************
reads or writes a structure.
********************************************************************/
char* lsa_io_user_info(BOOL io, LSA_USER_INFO *usr, char *q, char *base, int align, int depth)
{
	int i;

	if (usr == NULL) return NULL;

	DEBUG(5,("%s%04x lsa_io_user_info\n", tab_depth(depth), PTR_DIFF(q, base)));
	depth++;

	q = align_offset(q, base, align);
	
	DBG_RW_IVAL("ptr_user_info ", depth, base, io, q, usr->ptr_user_info); q += 4;

	if (usr->ptr_user_info != 0)
	{
		q = smb_io_time(io, &(usr->logon_time)           , q, base, align, depth); /* logon time */
		q = smb_io_time(io, &(usr->logoff_time)          , q, base, align, depth); /* logoff time */
		q = smb_io_time(io, &(usr->kickoff_time)         , q, base, align, depth); /* kickoff time */
		q = smb_io_time(io, &(usr->pass_last_set_time)   , q, base, align, depth); /* password last set time */
		q = smb_io_time(io, &(usr->pass_can_change_time) , q, base, align, depth); /* password can change time */
		q = smb_io_time(io, &(usr->pass_must_change_time), q, base, align, depth); /* password must change time */

		q = smb_io_unihdr(io, &(usr->hdr_user_name)   , q, base, align, depth); /* username unicode string header */
		q = smb_io_unihdr(io, &(usr->hdr_full_name)   , q, base, align, depth); /* user's full name unicode string header */
		q = smb_io_unihdr(io, &(usr->hdr_logon_script), q, base, align, depth); /* logon script unicode string header */
		q = smb_io_unihdr(io, &(usr->hdr_profile_path), q, base, align, depth); /* profile path unicode string header */
		q = smb_io_unihdr(io, &(usr->hdr_home_dir)    , q, base, align, depth); /* home directory unicode string header */
		q = smb_io_unihdr(io, &(usr->hdr_dir_drive)   , q, base, align, depth); /* home directory drive unicode string header */

		DBG_RW_SVAL("logon_count   ", depth, base, io, q, usr->logon_count ); q += 2;  /* logon count */
		DBG_RW_SVAL("bad_pw_count  ", depth, base, io, q, usr->bad_pw_count); q += 2; /* bad password count */

		DBG_RW_IVAL("user_id       ", depth, base, io, q, usr->user_id      ); q += 4;       /* User ID */
		DBG_RW_IVAL("group_id      ", depth, base, io, q, usr->group_id     ); q += 4;      /* Group ID */
		DBG_RW_IVAL("num_groups    ", depth, base, io, q, usr->num_groups   ); q += 4;    /* num groups */
		DBG_RW_IVAL("buffer_groups ", depth, base, io, q, usr->buffer_groups); q += 4; /* undocumented buffer pointer to groups. */
		DBG_RW_IVAL("user_flgs     ", depth, base, io, q, usr->user_flgs    ); q += 4;     /* user flags */

		DBG_RW_PCVAL(False, "user_sess_key", depth, base, io, q, usr->user_sess_key, 16); q += 16; /* unused user session key */

		q = smb_io_unihdr(io, &(usr->hdr_logon_srv), q, base, align, depth); /* logon server unicode string header */
		q = smb_io_unihdr(io, &(usr->hdr_logon_dom), q, base, align, depth); /* logon domain unicode string header */

		DBG_RW_IVAL("buffer_dom_id ", depth, base, io, q, usr->buffer_dom_id); q += 4; /* undocumented logon domain id pointer */
		DBG_RW_PCVAL(False, "padding       ", depth, base, io, q, usr->padding, 40); q += 40; /* unused padding bytes? */

		DBG_RW_IVAL("num_other_sids", depth, base, io, q, usr->num_other_sids); q += 4; /* 0 - num_sids */
		DBG_RW_IVAL("buffer_other_sids", depth, base, io, q, usr->buffer_other_sids); q += 4; /* NULL - undocumented pointer to SIDs. */
		
		q = smb_io_unistr2(io, &(usr->uni_user_name)   , q, base, align, depth); /* username unicode string */
		q = smb_io_unistr2(io, &(usr->uni_full_name)   , q, base, align, depth); /* user's full name unicode string */
		q = smb_io_unistr2(io, &(usr->uni_logon_script), q, base, align, depth); /* logon script unicode string */
		q = smb_io_unistr2(io, &(usr->uni_profile_path), q, base, align, depth); /* profile path unicode string */
		q = smb_io_unistr2(io, &(usr->uni_home_dir)    , q, base, align, depth); /* home directory unicode string */
		q = smb_io_unistr2(io, &(usr->uni_dir_drive)   , q, base, align, depth); /* home directory drive unicode string */

		DBG_RW_IVAL("num_groups2   ", depth, base, io, q, usr->num_groups2); q += 4;        /* num groups */
		for (i = 0; i < usr->num_groups2; i++)
		{
			q = smb_io_gid(io, &(usr->gids[i]), q, base, align, depth); /* group info */
		}

		q = smb_io_unistr2(io, &( usr->uni_logon_srv), q, base, align, depth); /* logon server unicode string */
		q = smb_io_unistr2(io, &( usr->uni_logon_dom), q, base, align, depth); /* logon domain unicode string */

		q = smb_io_dom_sid(io, &(usr->dom_sid), q, base, align, depth);           /* domain SID */

		for (i = 0; i < usr->num_other_sids; i++)
		{
			q = smb_io_dom_sid(io, &(usr->other_sids[i]), q, base, align, depth); /* other domain SIDs */
		}
	}

	return q;
}

/*******************************************************************
reads or writes a structure.
********************************************************************/
char* lsa_io_q_sam_logon(BOOL io, LSA_Q_SAM_LOGON *q_l, char *q, char *base, int align, int depth)
{
	if (q_l == NULL) return NULL;

	DEBUG(5,("%s%04x lsa_io_q_sam_logon\n", tab_depth(depth), PTR_DIFF(q, base)));
	depth++;

	q = align_offset(q, base, align);
	
	q = smb_io_sam_info(io, &(q_l->sam_id), q, base, align, depth);           /* domain SID */

	return q;
}

/*******************************************************************
reads or writes a structure.
********************************************************************/
char* lsa_io_r_sam_logon(BOOL io, LSA_R_SAM_LOGON *r_l, char *q, char *base, int align, int depth)
{
	if (r_l == NULL) return NULL;

	DEBUG(5,("%s%04x lsa_io_r_sam_logon\n", tab_depth(depth), PTR_DIFF(q, base)));
	depth++;

	q = align_offset(q, base, align);
	
	DBG_RW_IVAL("buffer_creds", depth, base, io, q, r_l->buffer_creds); q += 4; /* undocumented buffer pointer */
	q = smb_io_cred(io, &(r_l->srv_creds), q, base, align, depth); /* server credentials.  server time stamp appears to be ignored. */

	DBG_RW_SVAL("switch_value", depth, base, io, q, r_l->switch_value); q += 2;
	q = align_offset(q, base, align);
	q = lsa_io_user_info(io, r_l->user, q, base, align, depth);
	DBG_RW_IVAL("auth_resp   ", depth, base, io, q, r_l->auth_resp); q += 4; /* 1 - Authoritative response; 0 - Non-Auth? */

	DBG_RW_IVAL("status      ", depth, base, io, q, r_l->status); q += 4;

	return q;
}

/*******************************************************************
reads or writes a structure.
********************************************************************/
char* lsa_io_q_sam_logoff(BOOL io, LSA_Q_SAM_LOGOFF *q_l, char *q, char *base, int align, int depth)
{
	if (q_l == NULL) return NULL;

	DEBUG(5,("%s%04x lsa_io_q_sam_logoff\n", tab_depth(depth), PTR_DIFF(q, base)));
	depth++;

	q = align_offset(q, base, align);
	
	q = smb_io_sam_info(io, &(q_l->sam_id), q, base, align, depth);           /* domain SID */

	return q;
}

/*******************************************************************
reads or writes a structure.
********************************************************************/
char* lsa_io_r_sam_logoff(BOOL io, LSA_R_SAM_LOGOFF *r_l, char *q, char *base, int align, int depth)
{
	if (r_l == NULL) return NULL;

	DEBUG(5,("%s%04x lsa_io_r_sam_logoff\n", tab_depth(depth), PTR_DIFF(q, base)));
	depth++;

	q = align_offset(q, base, align);
	
	DBG_RW_IVAL("buffer_creds", depth, base, io, q, r_l->buffer_creds); q += 4; /* undocumented buffer pointer */
	q = smb_io_cred(io, &(r_l->srv_creds), q, base, align, depth); /* server credentials.  server time stamp appears to be ignored. */

	DBG_RW_IVAL("status      ", depth, base, io, q, r_l->status); q += 4;

	return q;
}

#if 0
/*******************************************************************
reads or writes a structure.
********************************************************************/
 char* lsa_io_(BOOL io, *, char *q, char *base, int align, int depth)
{
	if (== NULL) return NULL;

	q = align_offset(q, base, align);
	
	DBG_RW_IVAL("", depth, base, io, q, ); q += 4;

	return q;
}
#endif

#endif
