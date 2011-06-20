/* m_who.c - Because s_user.c was just crazy.
 *   Copyright (C) 1990 Jarkko Oikarinen and
 *                      University of Oulu, Computing Center
 *
 *   See file AUTHORS in IRC package for additional names of
 *   the programmers.
 *
 *   This program is free softwmare; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 1, or (at your option)
 *   any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/* $Id$ */

#include "struct.h"
#include "common.h"
#include "sys.h"
#include "numeric.h"
#include "msg.h"
#include "channel.h"
#include <sys/stat.h>
#include <utmp.h>
#include <fcntl.h>
#include "h.h"

/* Internally defined stuffs */
SOpts wsopts;
int build_searchopts(aClient *, int, char **);
int chk_who(aClient *, int, int);

/* Externally defined stuffs */
extern int lifesux;
extern int user_modes[];

extern Link *find_channel_link(Link *, aChannel *);

int build_searchopts(aClient *sptr, int parc, char *parv[])
{
  static char *who_oper_help[] =
  {
      "/WHO [+|-][acghimnsuCM] [args]",
      "Flags are specified like channel modes,",
      "The flags cghimnsu all have arguments",
      "Flags are set to a positive check by +, a negative check by -",
      "The flags work as follows:",
      "Flag a: user is away",
      "Flag c <channel>: user is on <channel>,",
      "                  no wildcards accepted",
      "Flag g <gcos/realname>: user has string <gcos> in their GCOS,",
      "                        wildcards accepted, oper only",
      "Flag h <host>: user has string <host> in their hostname,",
      "               wildcards accepted",
      "Flag i <ip>: user is from <ip> wildcards accepted,",
      "Flag l <class>: show users in a specific <class>",
      "Flag m <usermodes>: user has <usermodes> set on them",
      "Flag n <nick>: user has string <nick> in their nickname,",
      "               wildcards accepted",
      "Flag s <server>: user is on server <server>,",
      "                 wildcards not accepted",
      "Flag +t <seconds>: show users on for more than <seconds> seconds,",
      "Flag -t <seconds>: show users on for less than <seconds> seconds,",
      "Flag u <user>: user has string <user> in their username,",
      "               wildcards accepted",
      "Flag H <number>: Limit output to <number> entries,",
      "Flag A <away message>: show users with <away message>, wildcards accepted",
      "Flag x: user is not in any channel", 
      "Behavior flags:",
      "Flag C: show first visible channel user is in",
      "Flag M: check for user in channels I am a member of",
      NULL
  };

  static char *who_user_help[] =
  {
      "/WHO [+|-][achmnsuCM] [args]",
      "Flags are specified like channel modes,",
      "The flags cghimnsu all have arguments",
      "Flags are set to a positive check by +, a negative check by -",
      "The flags work as follows:",
      "Flag a: user is away",
      "Flag c <channel>: user is on <channel>,",
      "                  no wildcards accepted",
      "Flag h <host>: user has string <host> in their hostname,",
      "               wildcards accepted",
      "Flag m <usermodes>: user has <usermodes> set on them,",
      "                    only usermodes o/O/a/A will return a result",
      "Flag n <nick>: user has string <nick> in their nickname,",
      "               wildcards accepted",
      "Flag s <server>: user is on server <server>,",
      "                 wildcards not accepted",
      "Flag u <user>: user has string <user> in their username,",
      "               wildcards accepted",
      "Behavior flags:",
      "Flag C: show first visible channel user is in",
      "Flag M: check for user in channels I am a member of",
      NULL
  };

  char *flags, change=1, *s, *err;
  int args=1, i;

  memset((char *)&wsopts, '\0', sizeof(SOpts));
  /* if we got no extra arguments, send them the help. yeech. */
  /* if it's /who ?, send them the help */
  if(parc < 1 || parv[0][0]=='?')
  {
      /* So we don't confuse users with flags they cannot use,
         a different /who ? output will be given to users and
         opers -srd */

      char **ptr = NULL;

      if (!IsAnOper(sptr))
	  ptr = who_user_help;
      else
	  ptr = who_oper_help;

      for (; *ptr; ptr++)
	  sendto_one(sptr, getreply(RPL_COMMANDSYNTAX), me.name,
		     sptr->name, *ptr);
      sendto_one(sptr, getreply(RPL_ENDOFWHO), me.name, sptr->name, "?");
      return 0;
  }
  /* backwards compatibility */
  else if(parv[0][0]=='0' && parv[0][1]==0)
  {
      if(parc>1 && *parv[1]=='o')
      {
	  wsopts.check_umode=1;
	  wsopts.umode_plus=1;
	  wsopts.umodes=UMODE_o;
      }
      wsopts.host_plus=1;
      wsopts.host="*";
      return 1;
  }
  /* if the first argument isn't a list of stuff */
  else if(parv[0][0]!='+' && parv[0][0]!='-')
  {
      if(parv[0][0]=='#' || parv[0][0]=='&')
      {
	  wsopts.channel=find_channel(parv[0],NullChn);
	  if(wsopts.channel==NULL)
	  {
	      sendto_one(sptr, getreply(ERR_NOSUCHCHANNEL), me.name,
			 sptr->name, parv[0]);
	      return 0;
	  }
      }
      else
      {
	  /* If the arguement has a . in it, treat it as an
	   * address. Otherwise treat it as a nick. -Rak */
	  if (strchr(parv[0], '.'))
	  {
	      wsopts.host_plus=1;
	      wsopts.host=parv[0];
	  }
	  else
	  {
	      wsopts.nick_plus=1;
	      wsopts.nick=parv[0];
	  }
      }
      return 1;
  }
  /* now walk the list (a lot like set_mode) and set arguments
   * as appropriate. */
  flags=parv[0];
  while(*flags)
  {
      switch(*flags)
      {
      case '+':
      case '-':
	  change=(*flags=='+' ? 1 : 0);
	  break;
      case 'a':
	  if(change)
	      wsopts.away_plus=1; /* they want here people */
	  else
	      wsopts.away_plus=0;
	  wsopts.check_away=1;
	  break;
      case 'A':
	  if(parv[args] == NULL)
	  {
	      sendto_one(sptr, getreply(ERR_WHOSYNTAX), me.name, sptr->name);
	      return 0;
	  }
	  wsopts.away_msg_plus = change;
	  wsopts.away_msg = parv[args++];
	  break;
      case 'x':
	  wsopts.check_nochan = 1;
	  break;
      case 'H':
	  if ((parv[args] == NULL) ||
		((wsopts.maxhits = strtol(parv[args], &err, 10)) <= 0) ||
		(*err != '\0'))
	  {
	      sendto_one(sptr, getreply(ERR_WHOSYNTAX), me.name, sptr->name);
	      return 0;
	  }

	break;
      case 'C':
	  wsopts.show_chan = change;
	  break;
      case 'M':
	  wsopts.search_chan = change;
	  break;
      case 'c':
	  if(parv[args]==NULL || !change)
	  {
	      sendto_one(sptr, getreply(ERR_WHOSYNTAX), me.name,
			 sptr->name);
	      return 0;
	  }
	  if(*parv[args] == '@' || *parv[args] == '+' || *parv[args] == '%')
	  {
	      char *cname = parv[args];
	      
	      wsopts.channelflags = 0;
	      
              if(*cname == '@')
	      {
		  wsopts.channelflags = CHFL_CHANOP;
		  cname++;
	      }
	      if (*cname == '%')
	      {
		  wsopts.channelflags = CHFL_HALFOP;
		  cname++;
	      }
	      if (*cname == '+')
	      {
		  wsopts.channelflags = CHFL_VOICE;
		  cname++;
	      }

	      wsopts.channel=find_channel(cname, NullChn);
	  }
	  else
	  {
	      wsopts.channelflags = 0;
	      wsopts.channel=find_channel(parv[args],NullChn);
	  }

	  if(wsopts.channel==NULL)
	  {
	      sendto_one(sptr, getreply(ERR_NOSUCHCHANNEL), me.name,
			 sptr->name, parv[args]);
	      return 0;
	  }
	  wsopts.chan_plus=change;
	  args++;
	  break;
      case 'g':
	  if(parv[args]==NULL || !IsAnOper(sptr))
	  {
	      sendto_one(sptr, getreply(ERR_WHOSYNTAX), me.name,
			 sptr->name);
	      return 0;
	  }
	  wsopts.gcos=parv[args];
	  wsopts.gcos_plus=change;
	  args++;
	  break;
      case 'h':
	  if(parv[args]==NULL)
	  {
	      sendto_one(sptr, getreply(ERR_WHOSYNTAX), me.name,
			 sptr->name);
	      return 0;
	  }
	  wsopts.host=parv[args];
	  wsopts.host_plus=change;
	  args++;
	  break;
      case 'l':
          if(parv[args]==NULL || !IsAnOper(sptr))
          {
              sendto_one(sptr, getreply(ERR_WHOSYNTAX), me.name,
                         sptr->name);
              return 0;
          }
          wsopts.class=strtol(parv[args], NULL, 0);
          wsopts.class_value=change ? 2 : 1;
          args++;
          break;
      case 't': 
	  if(parv[args]==NULL || !IsAnOper(sptr) || 
             strtol(parv[args], &err, 0) == 0 || *err != '\0')
	  {
	      sendto_one(sptr, getreply(ERR_WHOSYNTAX), me.name,
			 sptr->name);
	      return 0;
	  }
	  wsopts.ts=strtol(parv[args], NULL, 0);
	  wsopts.ts_value=change ? 2 : 1;
	  args++;
	  break;
      case 'i':
	  if(parv[args]==NULL || !IsAnOper(sptr))
	  {
	      sendto_one(sptr, getreply(ERR_WHOSYNTAX), me.name,
			 sptr->name);
	      return 0;
	  }
	  wsopts.ip=parv[args];
	  wsopts.ip_plus=change;
	  args++;
	  break;
      case 'm':
	  if(parv[args]==NULL)
	  {
	      sendto_one(sptr, getreply(ERR_WHOSYNTAX), me.name,
			 sptr->name);
	      return 0;
	  }
	  s=parv[args];
	  while(*s)
	  {
	      for(i=1;user_modes[i]!=0x0;i+=2)
	      {
		  if(*s==(char)user_modes[i])
		  {
		      wsopts.umodes|=user_modes[i-1];
		      break;
		  }
	      }
	      s++;
	  }
	  if(!IsAnOper(sptr)) /* only let users search for +/-oOaA */
	      wsopts.umodes=(wsopts.umodes&(UMODE_o|UMODE_O|UMODE_a|UMODE_A));
	  wsopts.umode_plus=change;
	  if(wsopts.umodes)
	      wsopts.check_umode=1;
	  args++;
	  break;
      case 'n':
	  if(parv[args]==NULL)
	  {
	      sendto_one(sptr, getreply(ERR_WHOSYNTAX), me.name,
			 sptr->name);
	      return 0;
	  }
	  wsopts.nick=parv[args];
	  wsopts.nick_plus=change;
	  args++;
	  break;
      case 's':
	  if(parv[args]==NULL || !change)
	  {
	      sendto_one(sptr, getreply(ERR_WHOSYNTAX), me.name,
			 sptr->name);
	      return 0;
	  }
	  wsopts.server=find_server(parv[args],NULL);
	  if(wsopts.server==NULL)
	  {
	      sendto_one(sptr, getreply(ERR_NOSUCHSERVER), me.name,
			 sptr->name, parv[args]);
	      return 0;
	  }
	  wsopts.serv_plus=change;
	  args++;
	  break;
      case 'u':
	  if(parv[args]==NULL)
	  {
	      sendto_one(sptr, getreply(ERR_WHOSYNTAX), me.name,
			 sptr->name);
	      return 0;
	  }
	  wsopts.user=parv[args];
	  wsopts.user_plus=change;
	  args++;
	  break;
      default:
            {
                char **ptr = NULL;

                for (ptr = (!IsAnOper(sptr) ? who_user_help : who_oper_help); *ptr; ptr++)
	                sendto_one(sptr, getreply(RPL_COMMANDSYNTAX), me.name,
		                sptr->name, *ptr);
                sendto_one(sptr, getreply(RPL_ENDOFWHO), me.name, sptr->name, "?");
                return 0;
                break; /* unreachable */
            }
      }
      flags++;
  }
  
  /* if we specified search_chan, we _must_ specify something useful 
   * to go with it. Specifying a channel makes no sense, and no params make no 
   * sense either, as does specifying a nick.
   */
  
  if(wsopts.search_chan && !(wsopts.check_away || wsopts.gcos || wsopts.host ||
			     wsopts.check_umode || wsopts.server ||
			     wsopts.user || wsopts.class || wsopts.away_msg))
  {
      if(parv[args]==NULL || wsopts.channel || wsopts.nick ||
	 parv[args][0] == '#' || parv[args][0] == '&')
      {
	  sendto_one(sptr, getreply(ERR_WHOSYNTAX), me.name, sptr->name);
	  return 0;
      }

      if (strchr(parv[args], '.'))
      {
	  wsopts.host_plus=1;
	  wsopts.host=parv[args];
      }
      else
      {
	  sendto_one(sptr, getreply(ERR_WHOSYNTAX), me.name, sptr->name);
	  return 0;
      }
  } 
  else /* can't show_chan if nothing else is set! */
      if(wsopts.show_chan && !(wsopts.check_away || wsopts.gcos ||
			       wsopts.host || wsopts.check_umode ||
			       wsopts.server || wsopts.user || wsopts.nick ||
			       wsopts.ip || wsopts.channel || wsopts.class || wsopts.away_msg))
      {
	  if(parv[args]==NULL)
	  {
	      sendto_one(sptr, getreply(ERR_WHOSYNTAX), me.name, sptr->name);
	      return 0;
	  }
	  
	  if (strchr(parv[args], '.'))
	  {
	      wsopts.host_plus=1;
	      wsopts.host=parv[args];
	  }
	  else
	  {
	      wsopts.nick_plus=1;
	      wsopts.nick=parv[args];
	  }
      }

  /* hey cool, it all worked! */
  return 1;
}

/* these four are used by chk_who to check gcos/nick/user/host/ip
 * respectively 
 * as well as ip/Yline class -srd */

int (*gchkfn)(char *, char *);
int (*nchkfn)(char *, char *);
int (*uchkfn)(char *, char *);
int (*hchkfn)(char *, char *);
int (*ichkfn)(char *, char *);
int (*achkfn)(char*, char*); /* away check func XXX: maybe unuseful -> match() */

int chk_who(aClient *ac, int showall, int canshowip)
{
    if(!IsClient(ac))
	return 0;
    if(IsInvisible(ac) && !showall)
	return 0;
    if(wsopts.check_umode)
	if((wsopts.umode_plus && 
	    !((ac->umode&wsopts.umodes)==wsopts.umodes)) ||
	   (!wsopts.umode_plus && ((ac->umode&wsopts.umodes)==wsopts.umodes)))
	    return 0;
    if(wsopts.check_away)
	if((wsopts.away_plus && ac->user->away==NULL) ||
	   (!wsopts.away_plus && ac->user->away!=NULL))
	    return 0;
    if(wsopts.check_nochan)
    if(ac->user->channel != NULL)
            return 0;
    /* while this is wasteful now, in the future
     * when clients contain pointers to their servers
     * of origin, this'll become a 4 byte check instead of a mycmp
     * -wd */
    /* welcome to the future... :) - lucas */
    if(wsopts.serv_plus)
	if(wsopts.server != ac->uplink)
	    return 0;
    /* we only call match once, since if the first condition
     * isn't true, most (all?) compilers will never try the
     * second...phew :) */
    if(wsopts.user!=NULL)
	if((wsopts.user_plus && uchkfn(wsopts.user, ac->user->username)) ||
	   (!wsopts.user_plus && !uchkfn(wsopts.user, ac->user->username)))
	    return 0;

    if(wsopts.nick!=NULL)
	if((wsopts.nick_plus && nchkfn(wsopts.nick, ac->name)) ||
	   (!wsopts.nick_plus && !nchkfn(wsopts.nick, ac->name)))
	    return 0;
    
    if(wsopts.host!=NULL)
	if((wsopts.host_plus && hchkfn(wsopts.host, (!canshowip) ? ac->user->virthost : ac->user->host)) ||
	   (!wsopts.host_plus && !hchkfn(wsopts.host, (!canshowip) ? ac->user->virthost : ac->user->host)))
	    return 0;
    
    if(wsopts.ip!=NULL)
	if((wsopts.ip_plus && ichkfn(wsopts.ip, ac->hostip)) ||
	   (!wsopts.ip_plus && !ichkfn(wsopts.ip, ac->hostip)))
	    return 0;
    
    if(wsopts.gcos!=NULL)
	if((wsopts.gcos_plus && gchkfn(wsopts.gcos, ac->info)) ||
	   (!wsopts.gcos_plus && !gchkfn(wsopts.gcos, ac->info)))
	    return 0;

    if(wsopts.away_msg != NULL)
    {
     if(!ac->user->away) 
	return 0;
     else
     {
	if((wsopts.away_plus && !achkfn(wsopts.away_msg, ac->user->away)) ||
	   (!wsopts.away_plus && achkfn(wsopts.away_msg, ac->user->away)))
	    return 0;
     }
    }

    /*
     * For the below options, a value of two means '+', 
     * a value of 1 means '-', and a value of 0 means
     * not speficied. 
     */

    if(wsopts.class_value == 2 &&
        !(MyClient(ac) && get_client_class(ac) == wsopts.class))
        return 0;
    else if(wsopts.class_value == 1 &&
        !(MyClient(ac) && get_client_class(ac) != wsopts.class))
        return 0;

    if(wsopts.ts_value == 2 && /* +t */
        NOW - ac->tsinfo < wsopts.ts)
        return 0;
    else if(wsopts.ts_value == 1 && /* -t */
        NOW - ac->tsinfo > wsopts.ts)
        return 0;

    return 1;
}

inline char *first_visible_channel(aClient *cptr, aClient *sptr)
{
    Link *lp;
    int secret = 0;
    aChannel *chptr = NULL;
    static char chnbuf[CHANNELLEN + 2];

    if(cptr->user->channel)
    {
	if(IsAdmin(sptr) || IsSAdmin(sptr))
	{
	    chptr = cptr->user->channel->value.chptr;
	    if(!(ShowChannel(sptr, chptr)))
		secret = 1;
	}
	else
	{
	    for(lp = cptr->user->channel; lp; lp = lp->next)
	    {
		if(ShowChannel(sptr, lp->value.chptr))
		    break;
	    }
	    if(lp)
		chptr = lp->value.chptr;
	}

	if(chptr)
	{
	    if(!secret)
		return chptr->chname;
	    ircsprintf(chnbuf, "%%%s", chptr->chname);
	    return chnbuf;
	}
    }
    return "*";
}

/* allow lusers only 200 replies from /who */
#define MAXWHOREPLIES 200
#define WHO_HOPCOUNT(s, a) ((IsULine((a)) && !IsOper((s))) ? 0 : a->hopcount)
int m_who(aClient *cptr, aClient *sptr, int parc, char *parv[])
{
    aClient *ac;
    chanMember *cm;
    Link *lp;
    int shown=0, i=0, showall=IsAnOper(sptr);
    char status[5]; /* SSL  --tsk */

    /* drop nonlocal clients */
    if (!MyClient(sptr))
		return 0;

	if (!build_searchopts(sptr, parc-1, parv+1))
		return 0; /* /who was no good */

	if (wsopts.gcos != NULL && !strchr(wsopts.gcos, '?') && !strchr(wsopts.gcos, '*'))
		gchkfn = mycmp;
	else
		gchkfn = match;

	if (wsopts.nick != NULL && !strchr(wsopts.nick, '?') && !strchr(wsopts.nick, '*'))
		nchkfn = mycmp;
	else
		nchkfn = match;

	if (wsopts.user != NULL && !strchr(wsopts.user, '?') && !strchr(wsopts.user, '*'))
		uchkfn = mycmp;
	else
		uchkfn = match;

	if (wsopts.host != NULL && !strchr(wsopts.host, '?') && !strchr(wsopts.host, '*'))
		hchkfn = mycmp;
	else
		hchkfn = match;

	if (wsopts.ip != NULL && !strchr(wsopts.ip, '?') && !strchr(wsopts.ip, '*'))
		ichkfn = mycmp;
	else
		ichkfn = match;

	if (wsopts.away_msg != NULL && !strchr(wsopts.away_msg, '?') && !strchr(wsopts.away_msg, '*'))
		achkfn = mycmp;
	else
		achkfn = match;

    if (wsopts.channel != NULL) {

		if (IsMember(sptr,wsopts.channel))
			showall = 1;

		else if (SecretChannel(wsopts.channel) && (IsAdmin(sptr) || IsSAdmin(sptr)))
			showall = 1;

		else if (!SecretChannel(wsopts.channel) && IsAnOper(sptr))
			showall = 1;

		else
			showall = 0;

		if (showall || !SecretChannel(wsopts.channel)) {

			for (cm = wsopts.channel->members; cm; cm = cm->next) {

				ac = cm->cptr;
				i = 0;

				if (!chk_who(ac,showall, CanShowIP(sptr, ac)))
					continue;

				/* If we have channel flags set, verify they match */
				if (wsopts.channelflags && ((cm->flags & wsopts.channelflags) == 0))
					continue;

				/* get rid of the pidly stuff first */
				/* wow, they passed it all, give them the reply...
				* IF they haven't reached the max, or they're an oper */

				status[i++] = (ac->user->away == NULL ? 'H' : 'G');
				status[i] = (IsAnOper(ac) ? '*' : ((IsInvisible(ac) && IsOper(sptr)) ? '%' : 0));

				/* SSL --tsk */
				status[((status[i]) ? ++i : i)] = (IsUmodeS(ac) ? 'S' : 0);
				status[((status[i]) ? ++i : i)] = ((cm->flags & CHFL_CHANOP) ? '@' : 
								   ((cm->flags & CHFL_HALFOP) ? '%' : 
								   ((cm->flags & CHFL_VOICE) ? '+' : 0)));
				status[++i] = 0;

				sendto_one(sptr, getreply(RPL_WHOREPLY), me.name, sptr->name,
					wsopts.channel->chname, ac->user->username,
					(!CanShowIP(sptr, ac)) ? ac->user->virthost : ac->user->host,
					ac->user->server, ac->name, status, WHO_HOPCOUNT(sptr, ac), ac->info);

			   ++shown;

			   if ((wsopts.maxhits > 0) && (shown >= wsopts.maxhits)) {

				   sendto_one(sptr, getreply(ERR_WHOLIMEXCEED), me.name, sptr->name, MAXWHOREPLIES);
				   break; /* break out of loop so we can send end of who */
			   }
			}
		}

		sendto_one(sptr, getreply(RPL_ENDOFWHO), me.name, sptr->name, wsopts.channel->chname);
		return 0;
	}

	/* if (for whatever reason) they gave us a nick with no
     * wildcards, just do a find_person, bewm! */

	else if(nchkfn == mycmp) {

		ac = find_person(wsopts.nick,NULL);

		if (ac != NULL) {

			if (!chk_who(ac,1,CanShowIP(sptr, ac)))
			{
				sendto_one(sptr, getreply(RPL_ENDOFWHO), me.name, sptr->name, wsopts.host != NULL ? wsopts.host : wsopts.nick);
				return 0;
			}
			else
			{

				i = 0;
				status[i++] = (ac->user->away == NULL ? 'H' : 'G');
				status[i] = (IsAnOper(ac) ? '*' : (IsInvisible(ac) && IsAnOper(sptr) ? '%' : 0));

				/* SSL --tsk */
				status[((status[i]) ? ++i : i)] = (IsUmodeS(ac) ? 'S' : 0);
				status[++i] = 0;

				sendto_one(sptr, getreply(RPL_WHOREPLY), me.name, sptr->name,
					wsopts.show_chan ? first_visible_channel(ac, sptr) : "*", ac->user->username, 
					(!CanShowIP(sptr, ac)) ? ac->user->virthost : ac->user->host,
					ac->user->server, ac->name, status, WHO_HOPCOUNT(sptr, ac), ac->info);

				sendto_one(sptr, getreply(RPL_ENDOFWHO), me.name, sptr->name,
					wsopts.host != NULL ? wsopts.host : wsopts.nick);

				return 0;
			}
		}

		sendto_one(sptr, getreply(RPL_ENDOFWHO), me.name, sptr->name,
			wsopts.host != NULL ? wsopts.host : wsopts.nick);

		return 0;
	}

	/* if HTM, drop this too */
    if (lifesux && !IsAnOper(sptr)) {

		sendto_one(sptr, rpl_str(RPL_LOAD2HI), me.name, sptr->name);
		return 0;
	}

	if (wsopts.search_chan) {

		for (lp = sptr->user->channel; lp; lp = lp->next) {

			for (cm = lp->value.chptr->members; cm; cm = cm->next) {

				ac = cm->cptr;

				if (!chk_who(ac, 1, CanShowIP(sptr, ac)))
					continue;
		

				if (((wsopts.maxhits > 0) && (shown >= wsopts.maxhits)) ||
					((shown >= MAXWHOREPLIES) && !IsAnOper(sptr))) {

					sendto_one(sptr, getreply(ERR_WHOLIMEXCEED), me.name, sptr->name, MAXWHOREPLIES);
					break;
				}

				i = 0;
				status[i++] = (ac->user->away==NULL ? 'H' : 'G');
				status[i] = (IsAnOper(ac) ? '*' : ((IsInvisible(ac) && IsOper(sptr)) ? '-' : 0));

				/* SSL --tsk */
				status[((status[i]) ? ++i : i)] = (IsUmodeS(ac) ? 'S' : 0);
				status[((status[i]) ? ++i : i)] = ((cm->flags & CHFL_CHANOP) ? '@' : 
								   ((cm->flags & CHFL_HALFOP) ? '%' :
								   ((cm->flags & CHFL_VOICE) ? '+' : 0)));

				status[++i] = 0;

				sendto_one(sptr, getreply(RPL_WHOREPLY), me.name, sptr->name, lp->value.chptr->chname, ac->user->username,
					(!CanShowIP(sptr, ac)) ? ac->user->virthost : ac->user->host,
					ac->user->server, ac->name, status, WHO_HOPCOUNT(sptr, ac), ac->info);

				shown++;
			}
		}
	}
    else {

		for (ac = client; ac; ac = ac->next) {

			if (!chk_who(ac,showall,CanShowIP(sptr, ac)))
				continue;

			/* wow, they passed it all, give them the reply...
			 * IF they haven't reached the max, or they're an oper */
			if (((wsopts.maxhits > 0) && (shown >= wsopts.maxhits)) ||
				((shown >= MAXWHOREPLIES) && !IsAnOper(sptr))) {

				sendto_one(sptr, getreply(ERR_WHOLIMEXCEED), me.name, sptr->name, MAXWHOREPLIES);
				break; /* break out of loop so we can send end of who */
			}

			i = 0;
			status[i++] = (ac->user->away==NULL ? 'H' : 'G');
			status[i] = (IsAnOper(ac) ? '*' : (IsInvisible(ac) && IsAnOper(sptr) ? '%' : 0));

			/* SSL --tsk */
			status[((status[i]) ? ++i : i)] = (IsUmodeS(ac) ? 'S' : 0);
			status[++i] = 0;

			sendto_one(sptr, getreply(RPL_WHOREPLY), me.name, sptr->name,
				wsopts.show_chan ? first_visible_channel(ac, sptr) : "*", ac->user->username,
				(!CanShowIP(sptr, ac)) ? ac->user->virthost : ac->user->host,
				ac->user->server, ac->name, status, WHO_HOPCOUNT(sptr, ac), ac->info);

				shown++;
		}
	}

	sendto_one(sptr, getreply(RPL_ENDOFWHO), me.name, sptr->name,
		(wsopts.host != NULL ? wsopts.host : (wsopts.nick != NULL ? wsopts.nick :
		(wsopts.user != NULL ? wsopts.user : (wsopts.gcos != NULL ? wsopts.gcos :
		(wsopts.server != NULL ? wsopts.server->name : "*"))))));

	return 0;
}
