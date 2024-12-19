
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


#ifdef SYSLOG
#undef SYSLOG
#endif

#include "../includes.h"

extern int DEBUGLEVEL;
extern pstring username;
extern pstring workgroup;

#define CLIENT_TIMEOUT (30*1000)

#ifdef NTDOMAIN

/****************************************************************************
  open an rpc pipe (\NETLOGON or \srvsvc for example)
  ****************************************************************************/
uint16 rpc_pipe_open(char *inbuf, char *outbuf, char *rname, int Client, int cnum)
{
	int fnum;
	char *p;

	DEBUG(5,("open_rpc_pipe: %s\n", rname));

	bzero(outbuf,smb_size);
	set_message(outbuf,15,1 + strlen(rname),True);

	CVAL(outbuf,smb_com) = SMBopenX;
	SSVAL(outbuf,smb_tid, cnum);
	cli_setup_pkt(outbuf);

	SSVAL(outbuf,smb_vwv0,0xFF);
	SSVAL(outbuf,smb_vwv2,1);
	SSVAL(outbuf,smb_vwv3,(DENY_NONE<<4));
	SSVAL(outbuf,smb_vwv4,aSYSTEM | aHIDDEN);
	SSVAL(outbuf,smb_vwv5,aSYSTEM | aHIDDEN);
	SSVAL(outbuf,smb_vwv8,1);

	p = smb_buf(outbuf);
	pstrcpy(p,rname);
	p = skip_string(p,1);

	send_smb(Client,outbuf);
	receive_smb(Client,inbuf,CLIENT_TIMEOUT);

	if (CVAL(inbuf,smb_rcls) != 0)
	{
		if (CVAL(inbuf,smb_rcls) == ERRSRV &&
		    SVAL(inbuf,smb_err) == ERRnoresource &&
		    cli_reopen_connection(inbuf,outbuf))
		{
			return rpc_pipe_open(inbuf, outbuf, rname, Client, cnum);
		}
		DEBUG(0,("opening remote pipe %s - error %s\n", rname, smb_errstr(inbuf)));

		return 0xffff;
	}

	fnum = SVAL(inbuf, smb_vwv2);

	DEBUG(5,("opening pipe: fnum %d\n", fnum));

	return fnum;
}

struct
{
	char *client;
	char *server;
} pipe_names [] =
{
	{ PIPE_LSARPC  , PIPE_LSASS },
	{ PIPE_NETLOGON, PIPE_NETLOGON },
	{ PIPE_SRVSVC  , PIPE_SRVSVC   },
	{ NULL         , NULL          }
};

/****************************************************************************
do an rpc bind
****************************************************************************/
BOOL rpc_pipe_set_hnd_state(char *pipe_name, uint16 fnum, uint16 device_state)
{
	char *rparam = NULL;
	char *rdata = NULL;
	int rdrcnt,rprcnt;
	char pipe_str[11]; /* only 2 bytes */
	char param [2]; /* only 2 bytes */
	uint16 setup[2]; /* only need 2 uint16 setup parameters */
	BOOL state_set = False;
	int pipe_name_len = 0;

	if (pipe_name == NULL) return False;

	DEBUG(5,("Bind RPC Pipe[%x]: %s - device state:%x\n",
	          fnum, pipe_name, device_state));

	/* create data parameters: device state */
	SSVAL(param, 0, device_state);

	/* create setup parameters. */
	setup[0] = 0x0001; /* 0x01 indicates "set named pipe handle state" */
	setup[1] = fnum; /* file handle, from the SMBcreateX pipe, earlier */

	safe_strcpy(pipe_str, "\\PIPE\\", sizeof(pipe_str)-1);

	pipe_str[8] = 0;
	pipe_str[9] = 0;
	pipe_str[10] = 0;

	pipe_name_len = strlen(pipe_str);

	if (strcmp(pipe_name, PIPE_NETLOGON) == 0)
	{
		/* create param parameters.  no idea what these are */
		/* lkclXXXX speculating on these two values... */
		pipe_str[pipe_name_len+1] = 0x72;
		pipe_str[pipe_name_len+2] = 0x70;
		pipe_name_len += 2;
	}
	if (strcmp(pipe_name, PIPE_LSARPC) == 0)
	{
		/* create param parameters.  no idea what these are */
		pipe_str[pipe_name_len+1] = 0x72;
		pipe_str[pipe_name_len+2] = 0x70;
		pipe_name_len += 2;
	}
	else if (strcmp(pipe_name, PIPE_SRVSVC) == 0)
	{
		/* create param parameters.  no idea what these are */
		pipe_str[pipe_name_len+1] = 0x73;
		pipe_str[pipe_name_len+2] = 0x76;
		pipe_name_len += 2;
	}

	/* send the data on \PIPE\ */
	if (cli_call_api(pipe_str, pipe_name_len, 2, 0, 2,
	            1024, BUFFER_SIZE,
				&rprcnt, &rdrcnt,
				param, 0, setup,
				&rparam, &rdata))
	{
		DEBUG(5, ("cli_call_api: return OK\n"));
		state_set = True;
	}

	if (rparam) free(rparam);
	if (rdata ) free(rdata);

	return state_set;
}
/****************************************************************************
do an rpc bind
****************************************************************************/
BOOL rpc_pipe_bind(char *pipe_name, uint16 fnum, uint32 call_id,
				RPC_IFACE *abstract, RPC_IFACE *transfer)
{
	char *rparam = NULL;
	char *rdata = NULL;
	char *p;
	int rdrcnt,rprcnt;
	int data_len;
	pstring data; /* only 1024 bytes */
	uint16 setup[2]; /* only need 2 uint16 setup parameters */

	RPC_HDR    hdr;

	RPC_HDR_RB hdr_rb;

    BOOL valid_ack = False;

	if (pipe_name == NULL || abstract == NULL || transfer == NULL) return False;

	DEBUG(5,("Bind RPC Pipe[%x]: %s\n", fnum, pipe_name));

	/* create the request RPC_HDR_RB */
	make_rpc_hdr_rb(&hdr_rb, 
	                0x1630, 0x1630, 0x0,
	                0x1, 0x1, 0x1,
					abstract, transfer);

	/* stream the bind request data */
	p = smb_io_rpc_hdr_rb(False, &hdr_rb, data + 0x10, data, 4, 0);

	data_len = PTR_DIFF(p, data);

	/* create the request RPC_HDR */
	make_rpc_hdr(&hdr, RPC_BIND, 0x0, call_id, PTR_DIFF(p, data + 0x10));

	/* stream the header into data */
	p = smb_io_rpc_hdr(False, &hdr, data, data, 4, 0);

	/* create setup parameters. */
	setup[0] = 0x0026; /* 0x26 indicates "transact named pipe" */
	setup[1] = fnum; /* file handle, from the SMBcreateX pipe, earlier */

	/* send the data on \PIPE\ */
	if (cli_call_api("\\PIPE\\", 0, 0, data_len, 2, 1024,
                BUFFER_SIZE,
				&rprcnt, &rdrcnt,
				NULL, data, setup,
				&rparam, &rdata))
	{
		RPC_HDR_BA hdr_ba;
		int hdr_len;
		int pkt_len;
		int i = 0;

		DEBUG(5, ("cli_call_api: return OK\n"));

		p = rdata;

		if (p) p = smb_io_rpc_hdr(True, &hdr, p, rdata, 4, 0);
		if (p) p = align_offset(p, rdata, 4); /* oh, what a surprise */

		hdr_len = PTR_DIFF(p, rdata);

		if (p) p = smb_io_rpc_hdr_ba(True, &hdr_ba, p, rdata, 4, 0);

		pkt_len = PTR_DIFF(p, rdata);
#if 0
		if (p && hdr_len != hdr.hdr.frag_len - hdr.alloc_hint)
		{
			/* header length not same as calculated header length */
			DEBUG(2,("bind_rpc_pipe: hdr_len %x != frag_len-alloc_hint %x\n",
			          hdr_len, hdr.hdr.frag_len - hdr.alloc_hint));
			p = NULL;
		}


		if (p && pkt_len != hdr.hdr.frag_len)
		{
			/* packet data size not same as reported fragment length */
			DEBUG(2,("bind_rpc_pipe: pkt_len %x != frag_len \n",
			                           pkt_len, hdr.hdr.frag_len));
			p = NULL;
		}
#endif

		while (p && (pipe_names[i].server != NULL))
		{
			DEBUG(6,("bind_rpc_pipe: searching pipe name: client:%s server:%s\n",
			          pipe_names[i].client, pipe_names[i].server));

			if ((strcmp(pipe_name      , pipe_names[i].client) == 0))
			{
				if (strcmp(hdr_ba.addr.str, pipe_names[i].server) == 0)
				{
					DEBUG(5,("bind_rpc_pipe: server pipe_name found: %s\n",
					        pipe_names[i].server));
					break;
				}
				else
				{
					DEBUG(2,("bind_rpc_pipe: pipe_name %s != expected pipe %s\n",
					        pipe_names[i].server, hdr_ba.addr.str));
					p = NULL;
				}
			}
			else
			{
				i++;
			}
		}

		if (p && pipe_names[i].server == NULL)
		{
			DEBUG(2,("bind_rpc_pipe: pipe name %s unsupported\n", hdr_ba.addr.str));
			p = NULL;
		}

		if (p)
		{
			/* check the transfer syntax */
			valid_ack = (hdr_ba.transfer.version == transfer->version) &&
			        (memcmp(hdr_ba.transfer.data, transfer->data,
			                sizeof(transfer->version)) ==0);
			if (!valid_ack)
			{
				DEBUG(2,("bind_rpc_pipe: transfer syntax differs\n"));
				p = NULL;
			}
		}
		
		if (p)
		{
			/* check the results */
			valid_ack = (hdr_ba.res.num_results == 0x1) &&
			            (hdr_ba.res.result == 0);
			
			if (!valid_ack)
			{
				DEBUG(2,("bind_rpc_pipe: bind denied results: %d reason: %x\n",
				          hdr_ba.res.num_results,
			              hdr_ba.res.reason));
				p = NULL;
			}
			else
			{
				DEBUG(5,("bind_rpc_pipe: accepted!\n"));
			}
		}
	}

	if (rparam) free(rparam);
	if (rdata) free(rdata);

	return valid_ack;
}
#endif /* NTDOMAIN */
