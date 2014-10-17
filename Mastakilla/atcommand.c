// Copyright (c) Athena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "../common/cbasetypes.h"
#include "../common/mmo.h"
#include "../common/timer.h"
#include "../common/nullpo.h"
#include "../common/core.h"
#include "../common/showmsg.h"
#include "../common/malloc.h"
#include "../common/socket.h"
#include "../common/strlib.h"
#include "../common/utils.h"

#include "atcommand.h"
#include "battle.h"
#include "battleground.h"
#include "chat.h"
#include "channel.h"
#include "clif.h"
#include "chrif.h"
#include "intif.h"
#include "itemdb.h"
#include "log.h"
#include "map.h"
#include "pc.h"
#include "status.h"
#include "skill.h"
#include "mob.h"
#include "npc.h"
#include "pet.h"
#include "homunculus.h"
#include "mercenary.h"
#include "elemental.h"
#include "party.h"
#include "guild.h"
#include "script.h"
#include "trade.h"
#include "unit.h"
#include "harmony.h"

#ifndef TXT_ONLY
#include "mail.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

// extern variables
char atcommand_symbol = '@'; // first char of the commands
char charcommand_symbol = '#';
char* msg_table[MAX_MSG]; // Server messages (0-499 reserved for GM commands, 500-999 reserved for others)

// local declarations
#define ACMD_FUNC(x) int atcommand_ ## x (const int fd, struct map_session_data* sd, const char* command, const char* message)

typedef struct AtCommandInfo
{
	const char* command;
	int level;
	int level2;
	int premium;
	AtCommandFunc func;
} AtCommandInfo;

static AtCommandInfo* get_atcommandinfo_byname(const char* name);
static AtCommandInfo* get_atcommandinfo_byfunc(const AtCommandFunc func);
static bool premium_usage = false;

int atcommand_commands(const int fd, struct map_session_data* sd, const char* command, const char* message);


/*=========================================
 * Generic variables
 *-----------------------------------------*/
char atcmd_output[CHAT_SIZE_MAX];
char atcmd_player_name[NAME_LENGTH];
char atcmd_temp[100];

// compare function for sorting high to lowest
int hightolow_compare (const void * a, const void * b)
{
	return ( *(int*)b - *(int*)a );
}

// compare function for sorting lowest to highest
int lowtohigh_compare (const void * a, const void * b)
{
	return ( *(int*)a - *(int*)b );
}

//-----------------------------------------------------------
// Return the message string of the specified number by [Yor]
//-----------------------------------------------------------
char* msg_txt(int msg_number)
{
	if (msg_number >= 0 && msg_number < MAX_MSG &&
	    msg_table[msg_number] != NULL && msg_table[msg_number][0] != '\0')
		return msg_table[msg_number];

	return "??";
}

//-----------------------------------------------------------
// Returns Players title (from msg_athena.conf) [Lupus]
//-----------------------------------------------------------
static char* player_title_txt(int level)
{
	const char* format;
	format = (level >= battle_config.title_lvl8) ? msg_txt(332)
	       : (level >= battle_config.title_lvl7) ? msg_txt(331)
	       : (level >= battle_config.title_lvl6) ? msg_txt(330)
	       : (level >= battle_config.title_lvl5) ? msg_txt(329)
	       : (level >= battle_config.title_lvl4) ? msg_txt(328)
	       : (level >= battle_config.title_lvl3) ? msg_txt(327)
	       : (level >= battle_config.title_lvl2) ? msg_txt(326)
	       : (level >= battle_config.title_lvl1) ? msg_txt(325)
	       : "";
	sprintf(atcmd_temp, format, level);
	return atcmd_temp;
}


/*==========================================
 * Read Message Data
 *------------------------------------------*/
int msg_config_read(const char* cfgName)
{
	int msg_number;
	char line[1024], w1[1024], w2[1024];
	FILE *fp;
	static int called = 1;

	if ((fp = fopen(cfgName, "r")) == NULL) {
		ShowError("Messages file not found: %s\n", cfgName);
		return 1;
	}

	if ((--called) == 0)
		memset(msg_table, 0, sizeof(msg_table[0]) * MAX_MSG);

	while(fgets(line, sizeof(line), fp))
	{
		if (line[0] == '/' && line[1] == '/')
			continue;
		if (sscanf(line, "%[^:]: %[^\r\n]", w1, w2) != 2)
			continue;

		if (strcmpi(w1, "import") == 0)
			msg_config_read(w2);
		else
		{
			msg_number = atoi(w1);
			if (msg_number >= 0 && msg_number < MAX_MSG)
			{
				if (msg_table[msg_number] != NULL)
					aFree(msg_table[msg_number]);
				msg_table[msg_number] = (char *)aMalloc((strlen(w2) + 1)*sizeof (char));
				strcpy(msg_table[msg_number],w2);
			}
		}
	}

	fclose(fp);

	return 0;
}

/*==========================================
 * Cleanup Message Data
 *------------------------------------------*/
void do_final_msg(void)
{
	int i;
	for (i = 0; i < MAX_MSG; i++)
		aFree(msg_table[i]);
}


/*==========================================
 * @partybuff / @spb
 *------------------------------------------*/
int atcommand_partybuff(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	struct party_data* p = NULL;
	nullpo_retr(-1, sd);

    if( !sd->status.party_id ) {
    	clif_displaymessage(fd, "No est�s en party"); // You're not in a party.
    	return -1;
    }

	p = party_search(sd->status.party_id);

	if( sd->state.spb ) {
		sd->state.spb = 0;
    	clif_displaymessage(fd, "Mostrar Buffs de los miembros de party desactivado"); // Displaying party member's buffs disabled.
	} else {
		sd->state.spb = 1;
    	clif_displaymessage(fd, "Mostrar Buffs de los miembros de party activado"); // Displaying party member's buffs enabled.
	}

	clif_party_info(p,sd);
	return 0;
}
/*==========================================
 * @send (used for testing packet sends from the client)
 *------------------------------------------*/
int atcommand_send(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	int len=0,off,end,type;
	long num;
	(void)command; // not used

	// read message type as hex number (without the 0x)
	if(!message || !*message ||
			!((sscanf(message, "len %x", &type)==1 && (len=1))
			|| sscanf(message, "%x", &type)==1) )
	{
		clif_displaymessage(fd, "Usage:");
		clif_displaymessage(fd, "	@send len <packet hex number>");
		clif_displaymessage(fd, "	@send <packet hex number> {<value>}*");
		clif_displaymessage(fd, "	Value: <type=B(default),W,L><number> or S<length>\"<string>\"");
		return -1;
	}

#define PARSE_ERROR(error,p) \
	{\
		clif_displaymessage(fd, (error));\
		sprintf(atcmd_output, ">%s", (p));\
		clif_displaymessage(fd, atcmd_output);\
	}
//define PARSE_ERROR

#define CHECK_EOS(p) \
	if(*(p) == 0){\
		clif_displaymessage(fd, "Unexpected end of string");\
		return -1;\
	}
//define CHECK_EOS

#define SKIP_VALUE(p) \
	{\
		while(*(p) && !ISSPACE(*(p))) ++(p); /* non-space */\
		while(*(p) && ISSPACE(*(p)))  ++(p); /* space */\
	}
//define SKIP_VALUE

#define GET_VALUE(p,num) \
	{\
		if(sscanf((p), "x%lx", &(num)) < 1 && sscanf((p), "%ld ", &(num)) < 1){\
			PARSE_ERROR("Invalid number in:",(p));\
			return -1;\
		}\
	}
//define GET_VALUE

	if (type > 0 && type < MAX_PACKET_DB) {

		if(len)
		{// show packet length
			sprintf(atcmd_output, "Packet 0x%x length: %d", type, packet_db[sd->packet_ver][type].len);
			clif_displaymessage(fd, atcmd_output);
			return 0;
		}

		len=packet_db[sd->packet_ver][type].len;
		off=2;
		if(len == 0)
		{// unknown packet - ERROR
			sprintf(atcmd_output, "Unknown packet: 0x%x", type);
			clif_displaymessage(fd, atcmd_output);
			return -1;
		} else if(len == -1)
		{// dynamic packet
			len=SHRT_MAX-4; // maximum length
			off=4;
		}
		WFIFOHEAD(fd, len);
		WFIFOW(fd,0)=TOW(type);

		// parse packet contents
		SKIP_VALUE(message);
		while(*message != 0 && off < len){
			if(ISDIGIT(*message) || *message == '-' || *message == '+')
			{// default (byte)
				GET_VALUE(message,num);
				WFIFOB(fd,off)=TOB(num);
				++off;
			} else if(TOUPPER(*message) == 'B')
			{// byte
				++message;
				GET_VALUE(message,num);
				WFIFOB(fd,off)=TOB(num);
				++off;
			} else if(TOUPPER(*message) == 'W')
			{// word (2 bytes)
				++message;
				GET_VALUE(message,num);
				WFIFOW(fd,off)=TOW(num);
				off+=2;
			} else if(TOUPPER(*message) == 'L')
			{// long word (4 bytes)
				++message;
				GET_VALUE(message,num);
				WFIFOL(fd,off)=TOL(num);
				off+=4;
			} else if(TOUPPER(*message) == 'S')
			{// string - escapes are valid
				// get string length - num <= 0 means not fixed length (default)
				++message;
				if(*message == '"'){
					num=0;
				} else {
					GET_VALUE(message,num);
					while(*message != '"')
					{// find start of string
						if(*message == 0 || ISSPACE(*message)){
							PARSE_ERROR("Not a string:",message);
							return -1;
						}
						++message;
					}
				}

				// parse string
				++message;
				CHECK_EOS(message);
				end=(num<=0? 0: min(off+((int)num),len));
				for(; *message != '"' && (off < end || end == 0); ++off){
					if(*message == '\\'){
						++message;
						CHECK_EOS(message);
						switch(*message){
							case 'a': num=0x07; break; // Bell
							case 'b': num=0x08; break; // Backspace
							case 't': num=0x09; break; // Horizontal tab
							case 'n': num=0x0A; break; // Line feed
							case 'v': num=0x0B; break; // Vertical tab
							case 'f': num=0x0C; break; // Form feed
							case 'r': num=0x0D; break; // Carriage return
							case 'e': num=0x1B; break; // Escape
							default:  num=*message; break;
							case 'x': // Hexadecimal
							{
								++message;
								CHECK_EOS(message);
								if(!ISXDIGIT(*message)){
									PARSE_ERROR("Not a hexadecimal digit:",message);
									return -1;
								}
								num=(ISDIGIT(*message)?*message-'0':TOLOWER(*message)-'a'+10);
								if(ISXDIGIT(*message)){
									++message;
									CHECK_EOS(message);
									num<<=8;
									num+=(ISDIGIT(*message)?*message-'0':TOLOWER(*message)-'a'+10);
								}
								WFIFOB(fd,off)=TOB(num);
								++message;
								CHECK_EOS(message);
								continue;
							}
							case '0':
							case '1':
							case '2':
							case '3':
							case '4':
							case '5':
							case '6':
							case '7': // Octal
							{
								num=*message-'0'; // 1st octal digit
								++message;
								CHECK_EOS(message);
								if(ISDIGIT(*message) && *message < '8'){
									num<<=3;
									num+=*message-'0'; // 2nd octal digit
									++message;
									CHECK_EOS(message);
									if(ISDIGIT(*message) && *message < '8'){
										num<<=3;
										num+=*message-'0'; // 3rd octal digit
										++message;
										CHECK_EOS(message);
									}
								}
								WFIFOB(fd,off)=TOB(num);
								continue;
							}
						}
					} else
						num=*message;
					WFIFOB(fd,off)=TOB(num);
					++message;
					CHECK_EOS(message);
				}//for
				while(*message != '"')
				{// ignore extra characters
					++message;
					CHECK_EOS(message);
				}

				// terminate the string
				if(off < end)
				{// fill the rest with 0's
					memset(WFIFOP(fd,off),0,end-off);
					off=end;
				}
			} else
			{// unknown
				PARSE_ERROR("Unknown type of value in:",message);
				return -1;
			}
			SKIP_VALUE(message);
		}

		if(packet_db[sd->packet_ver][type].len == -1)
		{// send dynamic packet
			WFIFOW(fd,2)=TOW(off);
			WFIFOSET(fd,off);
		} else
		{// send static packet
			if(off < len)
				memset(WFIFOP(fd,off),0,len-off);
			WFIFOSET(fd,len);
		}
	} else {
		clif_displaymessage(fd, msg_txt(259)); // Invalid packet
		return -1;
	}
	sprintf (atcmd_output, msg_txt(258), type, type); // Sent packet 0x%x (%d)
	clif_displaymessage(fd, atcmd_output);
	return 0;
#undef PARSE_ERROR
#undef CHECK_EOS
#undef SKIP_VALUE
#undef GET_VALUE
}

/*==========================================
 * @rura, @warp, @mapmove
 *------------------------------------------*/
int atcommand_mapmove(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	char map_name[MAP_NAME_LENGTH_EXT];
	unsigned short mapindex;
	int x = 0, y = 0;
	int m = -1;

	nullpo_retr(-1, sd);

	memset(map_name, '\0', sizeof(map_name));

	if (!message || !*message ||
		(sscanf(message, "%15s %d %d", map_name, &x, &y) < 3 &&
		 sscanf(message, "%15[^,],%d,%d", map_name, &x, &y) < 1)) {
		 
			clif_displaymessage(fd, "Please, enter a map (usage: @warp/@rura/@mapmove <mapname> <x> <y>).");
			return -1;
	}

	mapindex = mapindex_name2id(map_name);
	if (mapindex)
		m = map_mapindex2mapid(mapindex);
	
	if (!mapindex) { // m < 0 means on different server! [Kevin]
		clif_displaymessage(fd, msg_txt(1)); // Map not found.
		return -1;
	}

	if ((x || y) && map_getcell(m, x, y, CELL_CHKNOPASS))
  	{	//This is to prevent the pc_setpos call from printing an error.
		clif_displaymessage(fd, msg_txt(2));
		x = y = 0; //Invalid cell, use random spot.
	}
	if (map[m].flag.nowarpto && battle_config.any_warp_GM_min_level > pc_isGM(sd)) {
		clif_displaymessage(fd, msg_txt(247));
		return -1;
	}
	if (sd->bl.m >= 0 && map[sd->bl.m].flag.nowarp && battle_config.any_warp_GM_min_level > pc_isGM(sd)) {
		clif_displaymessage(fd, msg_txt(248));
		return -1;
	}
	if (pc_setpos(sd, mapindex, x, y, 3) != 0) {
		clif_displaymessage(fd, msg_txt(1)); // Map not found.
		return -1;
	}

	clif_displaymessage(fd, msg_txt(0)); // Warped.
	return 0;
}

/*==========================================
 * Displays where a character is. Corrected version by Silent. [Skotlex]
 *------------------------------------------*/
int atcommand_where(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	struct map_session_data* pl_sd;

	nullpo_retr(-1, sd);
	memset(atcmd_player_name, '\0', sizeof atcmd_player_name);

	if (!message || !*message || sscanf(message, "%23[^\n]", atcmd_player_name) < 1) {
		clif_displaymessage(fd, "Please, enter a player name (usage: @where <char name>).");
		return -1;
	}

	pl_sd = map_nick2sd(atcmd_player_name);
	if( pl_sd == NULL
	||  strncmp(pl_sd->status.name,atcmd_player_name,NAME_LENGTH) != 0
	||  (battle_config.hide_GM_session && pc_isGM(sd) < pc_isGM(pl_sd) && !(battle_config.who_display_aid && pc_isGM(sd) >= battle_config.who_display_aid))
	) {
		clif_displaymessage(fd, msg_txt(3)); // Character not found.
		return -1;
	}

	snprintf(atcmd_output, sizeof atcmd_output, "%s %s %d %d", pl_sd->status.name, mapindex_id2name(pl_sd->mapindex), pl_sd->bl.x, pl_sd->bl.y);
	clif_displaymessage(fd, atcmd_output);

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
int atcommand_jumpto(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	struct map_session_data *pl_sd = NULL;

	nullpo_retr(-1, sd);

	if (!message || !*message) {
		clif_displaymessage(fd, "Please, enter a player name (usage: @jumpto/@warpto/@goto <player name/id>).");
		return -1;
	}

	if((pl_sd=map_nick2sd((char *)message)) == NULL && (pl_sd=map_charid2sd(atoi(message))) == NULL)
	{
		clif_displaymessage(fd, msg_txt(3)); // Character not found.
		return -1;
	}
	
	if (pl_sd == sd)
	{
		clif_displaymessage(fd, "But you are already where you are...");
		return -1;
	}
	
	if (pl_sd->bl.m >= 0 && map[pl_sd->bl.m].flag.nowarpto && battle_config.any_warp_GM_min_level > pc_isGM(sd))
	{
		clif_displaymessage(fd, msg_txt(247));	// You are not authorized to warp to this map.
		return -1;
	}
	
	if (sd->bl.m >= 0 && map[sd->bl.m].flag.nowarp && battle_config.any_warp_GM_min_level > pc_isGM(sd))
	{
		clif_displaymessage(fd, msg_txt(248));	// You are not authorized to warp from your current map.
		return -1;
	}

	if( pc_isdead(sd) )
	{
		clif_displaymessage(fd, "You cannot use this command when dead.");
		return -1;
	}

	pc_setpos(sd, pl_sd->mapindex, pl_sd->bl.x, pl_sd->bl.y, 3);
	sprintf(atcmd_output, msg_txt(4), pl_sd->status.name); // Jumped to %s
 	clif_displaymessage(fd, atcmd_output);

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
int atcommand_jump(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	int x = 0, y = 0;

	nullpo_retr(-1, sd);

	memset(atcmd_output, '\0', sizeof(atcmd_output));

	sscanf(message, "%d %d", &x, &y);

	if (map[sd->bl.m].flag.noteleport && battle_config.any_warp_GM_min_level > pc_isGM(sd)) {
		clif_displaymessage(fd, msg_txt(248));	// You are not authorized to warp from your current map.
		return -1;
	}

	if( pc_isdead(sd) )
	{
		clif_displaymessage(fd, "You cannot use this command when dead.");
		return -1;
	}

	if ((x || y) && map_getcell(sd->bl.m, x, y, CELL_CHKNOPASS))
  	{	//This is to prevent the pc_setpos call from printing an error.
		clif_displaymessage(fd, msg_txt(2));
		x = y = 0; //Invalid cell, use random spot.
	}

	pc_setpos(sd, sd->mapindex, x, y, 3);
	sprintf(atcmd_output, msg_txt(5), sd->bl.x, sd->bl.y); // Jumped to %d %d
	clif_displaymessage(fd, atcmd_output);
	return 0;
}

/*==========================================
 * @who3 = Player name, his location
 *------------------------------------------*/
int atcommand_who3(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	char temp0[100];
	struct map_session_data *pl_sd;
	struct s_mapiterator* iter;
	int j, count;
	int pl_GM_level, GM_level;
	char match_text[100];
	char player_name[NAME_LENGTH];

	nullpo_retr(-1, sd);

	memset(atcmd_output, '\0', sizeof(atcmd_output));
	memset(match_text, '\0', sizeof(match_text));
	memset(player_name, '\0', sizeof(player_name));

	if (sscanf(message, "%99[^\n]", match_text) < 1)
		strcpy(match_text, "");
	for (j = 0; match_text[j]; j++)
		match_text[j] = TOLOWER(match_text[j]);

	count = 0;
	GM_level = pc_isGM(sd);

	iter = mapit_getallusers();
	for( pl_sd = (TBL_PC*)mapit_first(iter); mapit_exists(iter); pl_sd = (TBL_PC*)mapit_next(iter) )
	{
		pl_GM_level = pc_isGM(pl_sd);
		if(!( (battle_config.hide_GM_session || (pl_sd->sc.option & OPTION_INVISIBLE)) && pl_GM_level > GM_level ))
		{// you can look only lower or same level
			memcpy(player_name, pl_sd->status.name, NAME_LENGTH);
			for (j = 0; player_name[j]; j++)
				player_name[j] = TOLOWER(player_name[j]);
			if (strstr(player_name, match_text) != NULL) { // search with no case sensitive

				if (battle_config.who_display_aid > 0 && pc_isGM(sd) >= battle_config.who_display_aid) {
					sprintf(atcmd_output, "(CID:%d/AID:%d) ", pl_sd->status.char_id, pl_sd->status.account_id);
				} else {
					atcmd_output[0]=0;
				}
				//Player name
				sprintf(temp0, msg_txt(333), pl_sd->status.name);
				strcat(atcmd_output,temp0);
				//Player title, if exists
				if (pl_GM_level > 0) {
					//sprintf(temp0, "(%s) ", player_title_txt(pl_GM_level) );
					sprintf(temp0, msg_txt(334), player_title_txt(pl_GM_level) );
					strcat(atcmd_output,temp0);
				}
				//Players Location: map x y
				sprintf(temp0, msg_txt(338), mapindex_id2name(pl_sd->mapindex), pl_sd->bl.x, pl_sd->bl.y);
				strcat(atcmd_output,temp0);

				clif_displaymessage(fd, atcmd_output);
				count++;
			}
		}
	}
	mapit_free(iter);

	if (count == 0)
		clif_displaymessage(fd, msg_txt(28)); // No player found.
	else if (count == 1)
		clif_displaymessage(fd, msg_txt(29)); // 1 player found.
	else {
		sprintf(atcmd_output, msg_txt(30), count); // %d players found.
		clif_displaymessage(fd, atcmd_output);
	}

	return 0;
}

/*==========================================
 * Player name, BLevel, Job, 
 *------------------------------------------*/
int atcommand_who2(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	char temp0[100];
	struct map_session_data *pl_sd;
	struct s_mapiterator* iter;
	int j, count;
	int pl_GM_level, GM_level;
	char match_text[100];
	char player_name[NAME_LENGTH];

	nullpo_retr(-1, sd);

	memset(atcmd_output, '\0', sizeof(atcmd_output));
	memset(match_text, '\0', sizeof(match_text));
	memset(player_name, '\0', sizeof(player_name));

	if (sscanf(message, "%99[^\n]", match_text) < 1)
		strcpy(match_text, "");
	for (j = 0; match_text[j]; j++)
		match_text[j] = TOLOWER(match_text[j]);

	count = 0;
	GM_level = pc_isGM(sd);

	iter = mapit_getallusers();
	for( pl_sd = (TBL_PC*)mapit_first(iter); mapit_exists(iter); pl_sd = (TBL_PC*)mapit_next(iter) )
	{
		pl_GM_level = pc_isGM(pl_sd);
		if(!( (battle_config.hide_GM_session || (pl_sd->sc.option & OPTION_INVISIBLE)) && (pl_GM_level > GM_level) ))
		{// you can look only lower or same level
			memcpy(player_name, pl_sd->status.name, NAME_LENGTH);
			for (j = 0; player_name[j]; j++)
				player_name[j] = TOLOWER(player_name[j]);
			if (strstr(player_name, match_text) != NULL) { // search with no case sensitive
				//Players Name
				//sprintf(atcmd_output, "Name: %s ", pl_sd->status.name);
				sprintf(atcmd_output, msg_txt(333), pl_sd->status.name);
				//Player title, if exists
				if (pl_GM_level > 0) {
					//sprintf(temp0, "(%s) ", player_title_txt(pl_GM_level) );
					sprintf(temp0, msg_txt(334), player_title_txt(pl_GM_level) );
					strcat(atcmd_output,temp0);
				}
				//Players Base Level / Job name
				//sprintf(temp0, "| L:%d/%d | Job: %s", pl_sd->status.base_level, pl_sd->status.job_level, job_name(pl_sd->status.class_) );
				sprintf(temp0, msg_txt(337), pl_sd->status.base_level, pl_sd->status.job_level, job_name(pl_sd->status.class_) );
				strcat(atcmd_output,temp0);

				clif_displaymessage(fd, atcmd_output);
				count++;
			}
		}
	}
	mapit_free(iter);
	
	if (count == 0)
		clif_displaymessage(fd, msg_txt(28)); // No player found.
	else if (count == 1)
		clif_displaymessage(fd, msg_txt(29)); // 1 player found.
	else {
		sprintf(atcmd_output, msg_txt(30), count); // %d players found.
		clif_displaymessage(fd, atcmd_output);
	}

	return 0;
}

/*==========================================
 * Player name, Playrs Party / Guild name
 *------------------------------------------*/
int atcommand_who(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	char temp0[100];
	struct map_session_data *pl_sd;
	struct s_mapiterator* iter;
	int j, count;
	int pl_GM_level, GM_level;
	char match_text[100];
	char player_name[NAME_LENGTH];
	struct guild *g;
	struct party_data *p;

	nullpo_retr(-1, sd);

	memset(temp0, '\0', sizeof(temp0));
	memset(atcmd_output, '\0', sizeof(atcmd_output));
	memset(match_text, '\0', sizeof(match_text));
	memset(player_name, '\0', sizeof(player_name));

	if (sscanf(message, "%99[^\n]", match_text) < 1)
		strcpy(match_text, "");
	for (j = 0; match_text[j]; j++)
		match_text[j] = TOLOWER(match_text[j]);

	count = 0;
	GM_level = pc_isGM(sd);

	iter = mapit_getallusers();
	for( pl_sd = (TBL_PC*)mapit_first(iter); mapit_exists(iter); pl_sd = (TBL_PC*)mapit_next(iter) )
	{
		pl_GM_level = pc_isGM(pl_sd);
		if(!( (battle_config.hide_GM_session || (pl_sd->sc.option & OPTION_INVISIBLE)) && pl_GM_level > GM_level ))
		{// you can look only lower or same level
			memcpy(player_name, pl_sd->status.name, NAME_LENGTH);
			for (j = 0; player_name[j]; j++)
				player_name[j] = TOLOWER(player_name[j]);
			if (strstr(player_name, match_text) != NULL) { // search with no case sensitive
				g = guild_search(pl_sd->status.guild_id);
				p = party_search(pl_sd->status.party_id);
				//Players Name
				sprintf(atcmd_output, msg_txt(333), pl_sd->status.name);
				//Player title, if exists
				if (pl_GM_level > 0) {
					sprintf(temp0, msg_txt(334), player_title_txt(pl_GM_level) );
					strcat(atcmd_output,temp0);
				}
				//Players Party if exists
				if (p != NULL) {
					//sprintf(temp0," | Party: '%s'", p->name);
					sprintf(temp0, msg_txt(335), p->party.name);
					strcat(atcmd_output,temp0);
				}
				//Players Guild if exists
				if (g != NULL) {
					//sprintf(temp0," | Guild: '%s'", g->name);
					sprintf(temp0, msg_txt(336), g->name);
					strcat(atcmd_output,temp0);
				}
				clif_displaymessage(fd, atcmd_output);
				count++;
			}
		}
	}
	mapit_free(iter);

	if (count == 0)
		clif_displaymessage(fd, msg_txt(28)); // No player found.
	else if (count == 1)
		clif_displaymessage(fd, msg_txt(29)); // 1 player found.
	else {
		sprintf(atcmd_output, msg_txt(30), count); // %d players found.
		clif_displaymessage(fd, atcmd_output);
	}

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
int atcommand_whomap3(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	struct map_session_data *pl_sd;
	struct s_mapiterator* iter;
	int count;
	int pl_GM_level, GM_level;
	int map_id;
	char map_name[MAP_NAME_LENGTH_EXT];

	memset(atcmd_output, '\0', sizeof(atcmd_output));
	memset(map_name, '\0', sizeof(map_name));

	if (!message || !*message)
		map_id = sd->bl.m;
	else {
		sscanf(message, "%15s", map_name);
		if ((map_id = map_mapname2mapid(map_name)) < 0)
			map_id = sd->bl.m;
	}

	count = 0;
	GM_level = pc_isGM(sd);

	iter = mapit_getallusers();
	for( pl_sd = (TBL_PC*)mapit_first(iter); mapit_exists(iter); pl_sd = (TBL_PC*)mapit_next(iter) )
	{
		pl_GM_level = pc_isGM(pl_sd);
		if( pl_sd->bl.m != map_id )
			continue;
		if( (battle_config.hide_GM_session || (pl_sd->sc.option & OPTION_INVISIBLE)) && (pl_GM_level > GM_level) )
			continue;

		if (pl_GM_level > 0)
			sprintf(atcmd_output, "Name: %s (GM:%d) | Location: %s %d %d", pl_sd->status.name, pl_GM_level, mapindex_id2name(pl_sd->mapindex), pl_sd->bl.x, pl_sd->bl.y);
		else
			sprintf(atcmd_output, "Name: %s | Location: %s %d %d", pl_sd->status.name, mapindex_id2name(pl_sd->mapindex), pl_sd->bl.x, pl_sd->bl.y);
		clif_displaymessage(fd, atcmd_output);
		count++;
	}
	mapit_free(iter);

	if (count == 0)
		sprintf(atcmd_output, msg_txt(54), map[map_id].name); // No player found in map '%s'.
	else if (count == 1)
		sprintf(atcmd_output, msg_txt(55), map[map_id].name); // 1 player found in map '%s'.
	else {
		sprintf(atcmd_output, msg_txt(56), count, map[map_id].name); // %d players found in map '%s'.
	}
	clif_displaymessage(fd, atcmd_output);

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
int atcommand_whomap2(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	struct map_session_data *pl_sd;
	struct s_mapiterator* iter;
	int count;
	int pl_GM_level, GM_level;
	int map_id = 0;
	char map_name[MAP_NAME_LENGTH_EXT];

	nullpo_retr(-1, sd);

	memset(atcmd_output, '\0', sizeof(atcmd_output));
	memset(map_name, '\0', sizeof(map_name));

	if (!message || !*message)
		map_id = sd->bl.m;
	else {
		sscanf(message, "%15s", map_name);
		if ((map_id = map_mapname2mapid(map_name)) < 0)
			map_id = sd->bl.m;
	}

	count = 0;
	GM_level = pc_isGM(sd);

	iter = mapit_getallusers();
	for( pl_sd = (TBL_PC*)mapit_first(iter); mapit_exists(iter); pl_sd = (TBL_PC*)mapit_next(iter) )
	{
		pl_GM_level = pc_isGM(pl_sd);
		if( pl_sd->bl.m != map_id )
			continue;
		if( (battle_config.hide_GM_session || (pl_sd->sc.option & OPTION_INVISIBLE)) && (pl_GM_level > GM_level) )
			continue;

		if (pl_GM_level > 0)
			sprintf(atcmd_output, "Name: %s (GM:%d) | BLvl: %d | Job: %s (Lvl: %d)", pl_sd->status.name, pl_GM_level, pl_sd->status.base_level, job_name(pl_sd->status.class_), pl_sd->status.job_level);
		else
			sprintf(atcmd_output, "Name: %s | BLvl: %d | Job: %s (Lvl: %d)", pl_sd->status.name, pl_sd->status.base_level, job_name(pl_sd->status.class_), pl_sd->status.job_level);
		clif_displaymessage(fd, atcmd_output);
		count++;
	}
	mapit_free(iter);

	if (count == 0)
		sprintf(atcmd_output, msg_txt(54), map[map_id].name); // No player found in map '%s'.
	else if (count == 1)
		sprintf(atcmd_output, msg_txt(55), map[map_id].name); // 1 player found in map '%s'.
	else {
		sprintf(atcmd_output, msg_txt(56), count, map[map_id].name); // %d players found in map '%s'.
	}
	clif_displaymessage(fd, atcmd_output);

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
int atcommand_whomap(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	char temp0[100];
	char temp1[100];
	struct map_session_data *pl_sd;
	struct s_mapiterator* iter;
	int count;
	int pl_GM_level, GM_level;
	int map_id = 0;
	char map_name[MAP_NAME_LENGTH_EXT];
	struct guild *g;
	struct party_data *p;

	nullpo_retr(-1, sd);

	memset(temp0, '\0', sizeof(temp0));
	memset(temp1, '\0', sizeof(temp1));
	memset(atcmd_output, '\0', sizeof(atcmd_output));
	memset(map_name, '\0', sizeof(map_name));

	if (!message || !*message)
		map_id = sd->bl.m;
	else {
		sscanf(message, "%15s", map_name);
		if ((map_id = map_mapname2mapid(map_name)) < 0)
			map_id = sd->bl.m;
	}

	count = 0;
	GM_level = pc_isGM(sd);

	iter = mapit_getallusers();
	for( pl_sd = (TBL_PC*)mapit_first(iter); mapit_exists(iter); pl_sd = (TBL_PC*)mapit_next(iter) )
	{
		pl_GM_level = pc_isGM(pl_sd);
		if( pl_sd->bl.m != map_id )
			continue;
		if( (battle_config.hide_GM_session || (pl_sd->sc.option & OPTION_INVISIBLE)) && (pl_GM_level > GM_level) )
			continue;

		g = guild_search(pl_sd->status.guild_id);
		if (g == NULL)
			sprintf(temp1, "None");
		else
			sprintf(temp1, "%s", g->name);
		p = party_search(pl_sd->status.party_id);
		if (p == NULL)
			sprintf(temp0, "None");
		else
			sprintf(temp0, "%s", p->party.name);
		if (pl_GM_level > 0)
			sprintf(atcmd_output, "Name: %s (GM:%d) | Party: '%s' | Guild: '%s'", pl_sd->status.name, pl_GM_level, temp0, temp1);
		else
			sprintf(atcmd_output, "Name: %s | Party: '%s' | Guild: '%s'", pl_sd->status.name, temp0, temp1);
		clif_displaymessage(fd, atcmd_output);
		count++;
	}
	mapit_free(iter);

	if (count == 0)
		sprintf(atcmd_output, msg_txt(54), map[map_id].name); // No player found in map '%s'.
	else if (count == 1)
		sprintf(atcmd_output, msg_txt(55), map[map_id].name); // 1 player found in map '%s'.
	else {
		sprintf(atcmd_output, msg_txt(56), count, map[map_id].name); // %d players found in map '%s'.
	}
	clif_displaymessage(fd, atcmd_output);

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
int atcommand_whogm(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	struct map_session_data* pl_sd;
	struct s_mapiterator* iter;
	int j, count;
	int pl_GM_level, GM_level;
	char match_text[CHAT_SIZE_MAX];
	char player_name[NAME_LENGTH];
	struct guild *g;
	struct party_data *p;

	nullpo_retr(-1, sd);

	memset(atcmd_output, '\0', sizeof(atcmd_output));
	memset(match_text, '\0', sizeof(match_text));
	memset(player_name, '\0', sizeof(player_name));

	if (sscanf(message, "%199[^\n]", match_text) < 1)
		strcpy(match_text, "");
	for (j = 0; match_text[j]; j++)
		match_text[j] = TOLOWER(match_text[j]);

	count = 0;
	GM_level = pc_isGM(sd);

	iter = mapit_getallusers();
	for( pl_sd = (TBL_PC*)mapit_first(iter); mapit_exists(iter); pl_sd = (TBL_PC*)mapit_next(iter) )
	{
		pl_GM_level = pc_isGM(pl_sd);
		if (!pl_GM_level)
			continue;

		if (match_text[0])
		{
			memcpy(player_name, pl_sd->status.name, NAME_LENGTH);
			for (j = 0; player_name[j]; j++)
				player_name[j] = TOLOWER(player_name[j]);
		  	// search with no case sensitive
			if (strstr(player_name, match_text) == NULL)
				continue;
		}
		if (pl_GM_level > GM_level) {
			if (pl_sd->sc.option & OPTION_INVISIBLE)
				continue;
			sprintf(atcmd_output, "Name: %s (GM)", pl_sd->status.name);
			clif_displaymessage(fd, atcmd_output);
			count++;
			continue;
		}

		sprintf(atcmd_output, "Name: %s (GM:%d) | Location: %s %d %d",
			pl_sd->status.name, pl_GM_level,
			mapindex_id2name(pl_sd->mapindex), pl_sd->bl.x, pl_sd->bl.y);
		clif_displaymessage(fd, atcmd_output);

		sprintf(atcmd_output, "       BLvl: %d | Job: %s (Lvl: %d)",
			pl_sd->status.base_level,
			job_name(pl_sd->status.class_), pl_sd->status.job_level);
		clif_displaymessage(fd, atcmd_output);
		
		p = party_search(pl_sd->status.party_id);
		g = guild_search(pl_sd->status.guild_id);
	
		sprintf(atcmd_output,"       Party: '%s' | Guild: '%s'",
			p?p->party.name:"None", g?g->name:"None");

		clif_displaymessage(fd, atcmd_output);
		count++;
	}
	mapit_free(iter);

	if (count == 0)
		clif_displaymessage(fd, msg_txt(150)); // No GM found.
	else if (count == 1)
		clif_displaymessage(fd, msg_txt(151)); // 1 GM found.
	else {
		sprintf(atcmd_output, msg_txt(152), count); // %d GMs found.
		clif_displaymessage(fd, atcmd_output);
	}

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
int atcommand_save(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	nullpo_retr(-1, sd);

	pc_setsavepoint(sd, sd->mapindex, sd->bl.x, sd->bl.y);
	if (sd->status.pet_id > 0 && sd->pd)
		intif_save_petdata(sd->status.account_id, &sd->pd->pet);

	chrif_save(sd,0);
	
	clif_displaymessage(fd, msg_txt(6)); // Your save point has been changed.

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
int atcommand_load(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	int m;

	nullpo_retr(-1, sd);

	// Para desactivar el comando en mapas PVP
	if ((map[sd->bl.m].flag.pvp) && (40 >= pc_isGM(sd))) { // El Numero en rojo, quiere decir que solo los GM con el Nivel 10 o superior podr�n utilizar el comando en los mapas.
  	clif_displaymessage(sd->fd,"Lo siento, no puedes utilizar este comando en un mapa PVP.");
  	return -1;
	}

	// Para desactivar el comando en un Mapa GVG.
	if ((map[sd->bl.m].flag.gvg) && (40 >= pc_isGM(sd))) { // El Numero en rojo, quiere decir que solo los GM con el Nivel 10 o superior podr�n utilizar el comando en los mapas.
  	clif_displaymessage(sd->fd,"Lo Siento, no puedes utilizar este comando en un mapa de GVG.");
  	return -1;
	}

	if(sd->state.pvpmode) {
		clif_displaymessage(fd, "No puedes usar @load/@return en modo PK.");
		return 0;
	}

	m = map_mapindex2mapid(sd->status.save_point.map);
	if (m >= 0 && map[m].flag.nowarpto && battle_config.any_warp_GM_min_level > pc_isGM(sd)) {
		clif_displaymessage(fd, msg_txt(249));	// You are not authorized to warp to your save map.
		return -1;
	}
	if (sd->bl.m >= 0 && map[sd->bl.m].flag.nowarp && battle_config.any_warp_GM_min_level > pc_isGM(sd)) {
		clif_displaymessage(fd, msg_txt(248));	// You are not authorized to warp from your current map.
		return -1;
	}

	pc_setpos(sd, sd->status.save_point.map, sd->status.save_point.x, sd->status.save_point.y, 0);
	clif_displaymessage(fd, msg_txt(7)); // Warping to save point..

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
int atcommand_speed(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	int speed;

	nullpo_retr(-1, sd);

	memset(atcmd_output, '\0', sizeof(atcmd_output));

	if (!message || !*message || sscanf(message, "%d", &speed) < 1) {
		sprintf(atcmd_output, "Please, enter a speed value (usage: @speed <%d-%d>).", MIN_WALK_SPEED, MAX_WALK_SPEED);
		clif_displaymessage(fd, atcmd_output);
		return -1;
	}

	sd->base_status.speed = cap_value(speed, MIN_WALK_SPEED, MAX_WALK_SPEED);
	status_calc_bl(&sd->bl, SCB_SPEED);
	clif_displaymessage(fd, msg_txt(8)); // Speed changed.
	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
int atcommand_storage(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	nullpo_retr(-1, sd);

	if( sd->npc_id || sd->vender_id || sd->buyer_id || sd->state.trading || sd->state.storage_flag )
		return -1;

	if( premium_usage && map_flag_gvg(sd->bl.m) )
		return -1;

	if (storage_storageopen(sd) == 1)
	{	//Already open.
		clif_displaymessage(fd, msg_txt(250));
		return -1;
	}
	
	clif_displaymessage(fd, "Storage opened.");
	
	return 0;
}


/*==========================================
 *
 *------------------------------------------*/
int atcommand_guildstorage(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	nullpo_retr(-1, sd);

	if (!sd->status.guild_id) {
		clif_displaymessage(fd, msg_txt(252));
		return -1;
	}

	if (sd->npc_id || sd->vender_id || sd->buyer_id || sd->state.trading)
		return -1;

	if (sd->state.storage_flag == 1 || sd->state.storage_flag == 3) {
		clif_displaymessage(fd, msg_txt(250));
		return -1;
	}

	if (sd->state.storage_flag == 2) {
		clif_displaymessage(fd, msg_txt(251));
		return -1;
	}

	storage_guild_storageopen(sd);
	clif_displaymessage(fd, "Guild storage opened.");
	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
int atcommand_option(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	int param1 = 0, param2 = 0, param3 = 0;
	nullpo_retr(-1, sd);

	if (!message || !*message || sscanf(message, "%d %d %d", &param1, &param2, &param3) < 1 || param1 < 0 || param2 < 0 || param3 < 0) {
		clif_displaymessage(fd, "Please, enter at least a option (usage: @option <param1:0+> <param2:0+> <param3:0+>).");
		return -1;
	}

	sd->sc.opt1 = param1;
	sd->sc.opt2 = param2;
	pc_setoption(sd, param3);
	
	clif_displaymessage(fd, msg_txt(9)); // Options changed.

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
int atcommand_hide(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	nullpo_retr(-1, sd);
	if (sd->sc.option & OPTION_INVISIBLE) {
		sd->sc.option &= ~OPTION_INVISIBLE;
		if (sd->disguise)
			status_set_viewdata(&sd->bl, sd->disguise);
		else
			status_set_viewdata(&sd->bl, sd->status.class_);
		clif_displaymessage(fd, msg_txt(10)); // Invisible: Off
	} else {
		sd->sc.option |= OPTION_INVISIBLE;
		sd->vd.class_ = INVISIBLE_CLASS;
		clif_displaymessage(fd, msg_txt(11)); // Invisible: On
	}
	clif_changeoption(&sd->bl);

	return 0;
}

/*==========================================
 * Changes a character's class
 *------------------------------------------*/
int atcommand_jobchange(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	//FIXME: redundancy, potentially wrong code, should use job_name() or similar instead of hardcoding the table [ultramage]
	int job = 0, upper = 0;
	nullpo_retr(-1, sd);

	if (!message || !*message || sscanf(message, "%d %d", &job, &upper) < 1)
	{
		int i, found = 0;
		const struct { char name[16]; int id; } jobs[] = {
			{ "novice",		0 },
			{ "swordsman",	1 },
			{ "mage",		2 },
			{ "archer",		3 },
			{ "acolyte",	4 },
			{ "merchant",	5 },
			{ "thief",		6 },
			{ "knight",		7 },
			{ "priest",		8 },
			{ "priestess",	8 },
			{ "wizard",		9 },
			{ "blacksmith",	10 },
			{ "hunter",		11 },
			{ "assassin",	12 },
			{ "crusader",	14 },
			{ "monk",		15 },
			{ "sage",		16 },
			{ "rogue",		17 },
			{ "alchemist",	18 },
			{ "bard",		19 },
			{ "dancer",		20 },
			{ "super novice",	23 },
			{ "supernovice",	23 },
			{ "gunslinger",	24 },
			{ "gunner",	24 },
			{ "ninja",	25 },
			{ "high novice",	4001 },
			{ "swordsman high",	4002 },
			{ "mage high",		4003 },
			{ "archer high",	4004 },
			{ "acolyte high",	4005 },
			{ "merchant high",	4006 },
			{ "thief high",		4007 },
			{ "lord knight",	4008 },
			{ "high priest",	4009 },
			{ "high priestess",	4009 },
			{ "high wizard",	4010 },
			{ "whitesmith",		4011 },
			{ "sniper",		4012 },
			{ "assassin cross",	4013 },
			{ "paladin",	4015 },
			{ "champion",	4016 },
			{ "professor",	4017 },
			{ "stalker",	4018 },
			{ "creator",	4019 },
			{ "clown",		4020 },
			{ "gypsy",		4021 },
			{ "baby novice",	4023 },
			{ "baby swordsman",	4024 },
			{ "baby mage",		4025 },
			{ "baby archer",	4026 },
			{ "baby acolyte",	4027 },
			{ "baby merchant",	4028 },
			{ "baby thief",		4029 },
			{ "baby knight",	4030 },
			{ "baby priest",	4031 },
			{ "baby priestess",	4031 },
			{ "baby wizard",	4032 },
			{ "baby blacksmith",4033 },
			{ "baby hunter",	4034 },
			{ "baby assassin",	4035 },
			{ "baby crusader",	4037 },
			{ "baby monk",		4038 },
			{ "baby sage",		4039 },
			{ "baby rogue",		4040 },
			{ "baby alchemist",	4041 },
			{ "baby bard",		4042 },
			{ "baby dancer",	4043 },
			{ "super baby",		4045 },
			{ "taekwon",		4046 },
			{ "taekwon boy",	4046 },
			{ "taekwon girl",	4046 },
			{ "star gladiator",	4047 },
			{ "soul linker",	4049 },
			{ "gangsi",		4050 },
			{ "bongun",		4050 },
			{ "munak",		4050 },
			{ "death knight",	4051 },
			{ "dark collector",	4052 },
			{ "rune knight",	4054 },
			{ "warlock",		4055 },
			{ "ranger",		4056 },
			{ "arch bishop",	4057 },
			{ "mechanic",		4058 },
			{ "guillotine",		4059 },
			{ "rune knight2",	4060 },
			{ "warlock2",		4061 },
			{ "ranger2",		4062 },
			{ "arch bishop2",	4063 },
			{ "mechanic2",		4064 },
			{ "guillotine2",	4065 },
			{ "royal guard",	4066 },
			{ "sorcerer",		4067 },
			{ "minstrel",		4068 },
			{ "wanderer",		4069 },
			{ "sura",		4070 },
			{ "genetic",		4071 },
			{ "shadow chaser",	4072 },
			{ "royal guard2",	4073 },
			{ "sorcerer2",		4074 },
			{ "minstrel2",		4075 },
			{ "wanderer2",		4076 },
			{ "sura2",		4077 },
			{ "genetic2",		4078 },
			{ "shadow chaser2",	4079 },
			{ "baby rune",		4096 },
			{ "baby warlock",	4097 },
			{ "baby ranger",	4098 },
			{ "baby bishop",	4099 },
			{ "baby mechanic",	4100 },
			{ "baby cross",		4101 },
			{ "baby guard",		4102 },
			{ "baby sorcerer",	4103 },
			{ "baby minstrel",	4104 },
			{ "baby wanderer",	4105 },
			{ "baby sura",		4106 },
			{ "baby genetic",	4107 },
			{ "baby chaser",	4108 },
		};

		for (i=0; i < ARRAYLENGTH(jobs); i++) {
			if (strncmpi(message, jobs[i].name, 16) == 0) {
				job = jobs[i].id;
				upper = 0;
				found = 1;
				break;
			}
		}

		if (!found) {
			clif_displaymessage(fd, "Please, enter job ID (usage: @job/@jobchange <job name/ID>).");
			clif_displaymessage(fd, "----- Novice / 1st Class -----");
			clif_displaymessage(fd, "   0 Novice            1 Swordman          2 Mage              3 Archer");
			clif_displaymessage(fd, "   4 Acolyte           5 Merchant          6 Thief");
			clif_displaymessage(fd, "----- 2nd Class -----");
			clif_displaymessage(fd, "   7 Knight            8 Priest            9 Wizard           10 Blacksmith");
			clif_displaymessage(fd, "  11 Hunter           12 Assassin         14 Crusader         15 Monk");
			clif_displaymessage(fd, "  16 Sage             17 Rogue            18 Alchemist        19 Bard");
			clif_displaymessage(fd, "  20 Dancer");
			clif_displaymessage(fd, "----- High Novice / High 1st Class -----");
			clif_displaymessage(fd, "4001 Novice High    4002 Swordman High  4003 Mage High      4004 Archer High");
			clif_displaymessage(fd, "4005 Acolyte High   4006 Merchant High  4007 Thief High");
			clif_displaymessage(fd, "----- Transcendent 2nd Class -----");
			clif_displaymessage(fd, "4008 Lord Knight    4009 High Priest    4010 High Wizard    4011 Whitesmith");
			clif_displaymessage(fd, "4012 Sniper         4013 Assassin Cross 4015 Paladin        4016 Champion");
			clif_displaymessage(fd, "4017 Professor      4018 Stalker        4019 Creator        4020 Clown");
			clif_displaymessage(fd, "4021 Gypsy");
			clif_displaymessage(fd, "----- 3rd Class (Regular to 3rd) -----");
			clif_displaymessage(fd, "4054 Rune Knight    4055 Warlock        4056 Ranger         4057 Arch Bishop");
			clif_displaymessage(fd, "4058 Mechanic       4059 Guillotine Cross 4066 Royal Guard  4067 Sorcerer");
			clif_displaymessage(fd, "4068 Minstrel       4069 Wanderer       4070 Sura           4071 Genetic");
			clif_displaymessage(fd, "4072 Shadow Chaser");
			clif_displaymessage(fd, "----- 3rd Class (Transcendent to 3rd) -----");
			clif_displaymessage(fd, "4060 Rune Knight    4061 Warlock        4062 Ranger         4063 Arch Bishop");
			clif_displaymessage(fd, "4064 Mechanic       4065 Guillotine Cross 4073 Royal Guard  4074 Sorcerer");
			clif_displaymessage(fd, "4075 Minstrel       4076 Wanderer       4077 Sura           4078 Genetic");
			clif_displaymessage(fd, "4079 Shadow Chaser");
			clif_displaymessage(fd, "----- Expanded Class -----");
			clif_displaymessage(fd, "  23 Super Novice     24 Gunslinger       25 Ninja          4046 Taekwon");
			clif_displaymessage(fd, "4047 Star Gladiator 4049 Soul Linker    4050 Gangsi         4051 Death Knight");
			clif_displaymessage(fd, "4052 Dark Collector");
			clif_displaymessage(fd, "---- 1st And 2nd Baby Class ----");
			clif_displaymessage(fd, "4023 Baby Novice    4024 Baby Swordsman 4025 Baby Mage      4026 Baby Archer");
			clif_displaymessage(fd, "4027 Baby Acolyte   4028 Baby Merchant  4029 Baby Thief     4030 Baby Knight");
			clif_displaymessage(fd, "4031 Baby Priest    4032 Baby Wizard    4033 Baby Blacksmith 4034 Baby Hunter");
			clif_displaymessage(fd, "4035 Baby Assassin  4037 Baby Crusader  4038 Baby Monk      4039 Baby Sage");
			clif_displaymessage(fd, "4040 Baby Rogue     4041 Baby Alchemist 4042 Baby Bard      4043 Baby Dancer");
			clif_displaymessage(fd, "4045 Super Baby");
			clif_displaymessage(fd, "---- 3rd Baby Class ----");
			clif_displaymessage(fd, "4096 Baby Rune Knight    4097 Baby Warlock        4098 Baby Ranger");
			clif_displaymessage(fd, "4099 Baby Arch Bishop    4100 Baby Mechanic       4101 Baby Guillotine Cross");
			clif_displaymessage(fd, "4102 Baby Royal Guard    4103 Baby Sorcerer       4104 Baby Minstrel");
			clif_displaymessage(fd, "4105 Baby Wanderer       4106 Baby Sura           4107 Baby Genetic");
			clif_displaymessage(fd, "4108 Baby Shadow Chaser");
			clif_displaymessage(fd, "---- Mounts, Modes, And Others ----");
			clif_displaymessage(fd, "  13 Knight (Peco)    21 Crusader (Peco)  22 Wedding          26 Christmas");
			clif_displaymessage(fd, "  27 Summer 4014 Lord Knight (Peco) 4022 Paladin (Peco)  4036 Baby Knight (Peco)");
			clif_displaymessage(fd, "4044 Baby Crusader (Peco) 4048 Star Gladiator (Union) 4080 Rune Knight (Dragon)");
			clif_displaymessage(fd, "4081 Rune Knight Trans (Dragon) 4082 Royal Guard (Gryphon)");
			clif_displaymessage(fd, "4083 Royal Guard Trans (Gryphon) 4084 Ranger (Warg) 4085 Ranger Trans (Warg)");
			clif_displaymessage(fd, "4086 Mechanic (Mado) 4087 Mechanic Trans (Mado)");
			return -1;
		}
	}

	if (job == 37 ||job == 45)
		return 0;

	if (pcdb_checkid(job))
	{
		if (pc_jobchange(sd, job, upper) == 0)
			clif_displaymessage(fd, msg_txt(12)); // Your job has been changed.
		else {
			clif_displaymessage(fd, msg_txt(155)); // You are unable to change your job.
			return -1;
		}
	} else {
		clif_displaymessage(fd, "Please, enter a valid job ID (usage: @job/@jobchange <job name/ID>).");
		clif_displaymessage(fd, "   0 Novice            1 Swordman          2 Mage              3 Archer");
		clif_displaymessage(fd, "   4 Acolyte           5 Merchant          6 Thief");
		clif_displaymessage(fd, "----- 2nd Class -----");
		clif_displaymessage(fd, "   7 Knight            8 Priest            9 Wizard           10 Blacksmith");
		clif_displaymessage(fd, "  11 Hunter           12 Assassin         14 Crusader         15 Monk");
		clif_displaymessage(fd, "  16 Sage             17 Rogue            18 Alchemist        19 Bard");
		clif_displaymessage(fd, "  20 Dancer");
		clif_displaymessage(fd, "----- High Novice / High 1st Class -----");
		clif_displaymessage(fd, "4001 Novice High    4002 Swordman High  4003 Mage High      4004 Archer High");
		clif_displaymessage(fd, "4005 Acolyte High   4006 Merchant High  4007 Thief High");
		clif_displaymessage(fd, "----- Transcendent 2nd Class -----");
		clif_displaymessage(fd, "4008 Lord Knight    4009 High Priest    4010 High Wizard    4011 Whitesmith");
		clif_displaymessage(fd, "4012 Sniper         4013 Assassin Cross 4015 Paladin        4016 Champion");
		clif_displaymessage(fd, "4017 Professor      4018 Stalker        4019 Creator        4020 Clown");
		clif_displaymessage(fd, "4021 Gypsy");
		clif_displaymessage(fd, "----- 3rd Class (Regular to 3rd) -----");
		clif_displaymessage(fd, "4054 Rune Knight    4055 Warlock        4056 Ranger         4057 Arch Bishop");
		clif_displaymessage(fd, "4058 Mechanic       4059 Guillotine Cross 4066 Royal Guard  4067 Sorcerer");
		clif_displaymessage(fd, "4068 Minstrel       4069 Wanderer       4070 Sura           4071 Genetic");
		clif_displaymessage(fd, "4072 Shadow Chaser");
		clif_displaymessage(fd, "----- 3rd Class (Transcendent to 3rd) -----");
		clif_displaymessage(fd, "4060 Rune Knight    4061 Warlock        4062 Ranger         4063 Arch Bishop");
		clif_displaymessage(fd, "4064 Mechanic       4065 Guillotine Cross 4073 Royal Guard  4074 Sorcerer");
		clif_displaymessage(fd, "4075 Minstrel       4076 Wanderer       4077 Sura           4078 Genetic");
		clif_displaymessage(fd, "4079 Shadow Chaser");
		clif_displaymessage(fd, "----- Expanded Class -----");
		clif_displaymessage(fd, "  23 Super Novice     24 Gunslinger       25 Ninja          4046 Taekwon");
		clif_displaymessage(fd, "4047 Star Gladiator 4049 Soul Linker    4050 Gangsi         4051 Death Knight");
		clif_displaymessage(fd, "4052 Dark Collector");
		clif_displaymessage(fd, "---- 1st And 2nd Baby Class ----");
		clif_displaymessage(fd, "4023 Baby Novice    4024 Baby Swordsman 4025 Baby Mage      4026 Baby Archer");
		clif_displaymessage(fd, "4027 Baby Acolyte   4028 Baby Merchant  4029 Baby Thief     4030 Baby Knight");
		clif_displaymessage(fd, "4031 Baby Priest    4032 Baby Wizard    4033 Baby Blacksmith 4034 Baby Hunter");
		clif_displaymessage(fd, "4035 Baby Assassin  4037 Baby Crusader  4038 Baby Monk      4039 Baby Sage");
		clif_displaymessage(fd, "4040 Baby Rogue     4041 Baby Alchemist 4042 Baby Bard      4043 Baby Dancer");
		clif_displaymessage(fd, "4045 Super Baby");
		clif_displaymessage(fd, "---- 3rd Baby Class ----");
		clif_displaymessage(fd, "4096 Baby Rune Knight    4097 Baby Warlock        4098 Baby Ranger");
		clif_displaymessage(fd, "4099 Baby Arch Bishop    4100 Baby Mechanic       4101 Baby Guillotine Cross");
		clif_displaymessage(fd, "4102 Baby Royal Guard    4103 Baby Sorcerer       4104 Baby Minstrel");
		clif_displaymessage(fd, "4105 Baby Wanderer       4106 Baby Sura           4107 Baby Genetic");
		clif_displaymessage(fd, "4108 Baby Shadow Chaser");
		clif_displaymessage(fd, "---- Mounts, Modes, And Others ----");
		clif_displaymessage(fd, "  13 Knight (Peco)    21 Crusader (Peco)  22 Wedding          26 Christmas");
		clif_displaymessage(fd, "  27 Summer 4014 Lord Knight (Peco) 4022 Paladin (Peco)  4036 Baby Knight (Peco)");
		clif_displaymessage(fd, "4044 Baby Crusader (Peco) 4048 Star Gladiator (Union) 4080 Rune Knight (Dragon)");
		clif_displaymessage(fd, "4081 Rune Knight Trans (Dragon) 4082 Royal Guard (Gryphon)");
		clif_displaymessage(fd, "4083 Royal Guard Trans (Gryphon) 4084 Ranger (Warg) 4085 Ranger Trans (Warg)");
		clif_displaymessage(fd, "4086 Mechanic (Mado) 4087 Mechanic Trans (Mado)");
		return -1;
	}

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
int atcommand_die(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	nullpo_retr(-1, sd);
	clif_specialeffect(&sd->bl,450,SELF);
	status_kill(&sd->bl);
	clif_displaymessage(fd, msg_txt(13)); // A pity! You've died.

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
int atcommand_kill(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	struct map_session_data *pl_sd;
	nullpo_retr(-1, sd);

	memset(atcmd_player_name, '\0', sizeof(atcmd_player_name));

	if (!message || !*message) {
		clif_displaymessage(fd, "Please, enter a player name (usage: @kill <player name/id>).");
		return -1;
	}

	if((pl_sd=map_nick2sd((char *)message)) == NULL && (pl_sd=map_charid2sd(atoi(message))) == NULL)
	{
		clif_displaymessage(fd, msg_txt(3)); // Character not found.
		return -1;
	}
	
	if (pc_isGM(sd) < pc_isGM(pl_sd))
	{ // you can kill only lower or same level
		clif_displaymessage(fd, msg_txt(81)); // Your GM level don't authorise you to do this action on this player.
		return -1;
	}
	
	status_kill(&pl_sd->bl);
	clif_displaymessage(pl_sd->fd, msg_txt(13)); // A pity! You've died.
	if (fd != pl_sd->fd)
		clif_displaymessage(fd, msg_txt(14)); // Character killed.

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
int atcommand_alive(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	nullpo_retr(-1, sd);
	if (!status_revive(&sd->bl, 100, 100))
	{
		clif_displaymessage(fd, "You're not dead.");
		return -1;
	}
	clif_skill_nodamage(&sd->bl,&sd->bl,ALL_RESURRECTION,4,1);
	clif_displaymessage(fd, msg_txt(16)); // You've been revived! It's a miracle!
	return 0;
}

/*==========================================
 * +kamic [LuzZza]
 *------------------------------------------*/
int atcommand_kami(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	unsigned long color=0;
	nullpo_retr(-1, sd);

	memset(atcmd_output, '\0', sizeof(atcmd_output));

	if(*(command + 5) != 'c' && *(command + 5) != 'C') {

		if (!message || !*message) {
			clif_displaymessage(fd, "Please, enter a message (usage: @kami <message>).");
			return -1;
		}

		sscanf(message, "%199[^\n]", atcmd_output);
		intif_broadcast(atcmd_output, strlen(atcmd_output) + 1, (*(command + 5) == 'b' || *(command + 5) == 'B') ? 0x10 : 0);
	
	} else {
	
		if(!message || !*message || (sscanf(message, "%lx %199[^\n]", &color, atcmd_output) < 2)) {
			clif_displaymessage(fd, "Please, enter color and message (usage: @kamic <color> <message>).");
			return -1;
		}
	
		if(color > 0xFFFFFF) {
			clif_displaymessage(fd, "Invalid color.");
			return -1;
		}
	
		intif_broadcast2(atcmd_output, strlen(atcmd_output) + 1, color, 0x190, 12, 0, 0);
	}
	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
int atcommand_heal(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	int hp = 0, sp = 0; // [Valaris] thanks to fov
	nullpo_retr(-1, sd);

	sscanf(message, "%d %d", &hp, &sp);

	// some overflow checks
	if( hp == INT_MIN ) hp++;
	if( sp == INT_MIN ) sp++;

	if ( hp == 0 && sp == 0 ) {
		if (!status_percent_heal(&sd->bl, 100, 100))
			clif_displaymessage(fd, msg_txt(157)); // HP and SP have already been recovered.
		else
			clif_displaymessage(fd, msg_txt(17)); // HP, SP recovered.
		return 0;
	}

	if ( hp > 0 && sp >= 0 ) {
		if(!status_heal(&sd->bl, hp, sp, 0))
			clif_displaymessage(fd, msg_txt(157)); // HP and SP are already with the good value.
		else
			clif_displaymessage(fd, msg_txt(17)); // HP, SP recovered.
		return 0;
	}

	if ( hp < 0 && sp <= 0 ) {
		status_damage(NULL, &sd->bl, -hp, -sp, 0, 0, 0);
		clif_damage(&sd->bl,&sd->bl, gettick(), 0, 0, -hp, 0, 4, 0);
		clif_displaymessage(fd, msg_txt(156)); // HP or/and SP modified.
		return 0;
	}

	//Opposing signs.
	if ( hp ) {
		if (hp > 0)
			status_heal(&sd->bl, hp, 0, 0);
		else {
			status_damage(NULL, &sd->bl, -hp, 0, 0, 0, 0);
			clif_damage(&sd->bl,&sd->bl, gettick(), 0, 0, -hp, 0, 4, 0);
		}
	}

	if ( sp ) {
		if (sp > 0)
			status_heal(&sd->bl, 0, sp, 0);
		else
			status_damage(NULL, &sd->bl, 0, -sp, 0, 0, 0);
	}

	clif_displaymessage(fd, msg_txt(156)); // HP or/and SP modified.
	return 0;
}

/*==========================================
 * @item command (usage: @item <name/id_of_item> <quantity>) (modified by [Yor] for pet_egg)
 *------------------------------------------*/
int atcommand_item(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	char item_name[100];
	int number = 0, item_id, flag, bound = 0;
	struct item item_tmp;
	struct item_data *item_data;
	int get_count, i;
	nullpo_retr(-1, sd);

	memset(item_name, '\0', sizeof(item_name));

	if (!message || !*message || (
		sscanf(message, "\"%99[^\"]\" %d", item_name, &number) < 1 &&
		sscanf(message, "%99s %d", item_name, &number) < 1
	)) {
		clif_displaymessage(fd, "Please, enter an item name/id (usage: @item <item name or ID> [quantity]).");
		return -1;
	}

	if (number <= 0)
		number = 1;

	if ((item_data = itemdb_searchname(item_name)) == NULL &&
	    (item_data = itemdb_exists(atoi(item_name))) == NULL)
	{
		clif_displaymessage(fd, msg_txt(19)); // Invalid item ID or name.
		return -1;
	}

	if( !strcmpi(command+1,"bounditem") )
		bound = 1;

	item_id = item_data->nameid;
	get_count = number;
	//Check if it's stackable.
	if( !itemdb_isstackable2(item_data) )
	{
		if( bound && (item_data->type == IT_PETEGG || item_data->type == IT_PETARMOR) )
		{
			clif_displaymessage(fd, "Cannot create bounded pet eggs or pet armors.");
			return -1;
		}
		get_count = 1;
	}
	else if( bound )
	{
		clif_displaymessage(fd, "Cannot create bounded stackable items.");
		return -1;
	}

	for( i = 0; i < number; i += get_count )
	{
		// if not pet egg
		if( !pet_create_egg(sd, item_id) )
		{
			memset(&item_tmp, 0, sizeof(item_tmp));
			item_tmp.nameid = item_id;
			item_tmp.identify = 1;
			item_tmp.bound = bound;

			if( (flag = pc_additem(sd, &item_tmp, get_count)) )
				clif_additem(sd, 0, 0, flag);
		}

		//Logs (A)dmins items [Lupus]
		if( log_config.enable_logs&0x400 )
			log_pick_pc(sd, "A", item_id, get_count, NULL, item_tmp.serial);
		
		item_tmp.serial = 0;
	}

	clif_displaymessage(fd, msg_txt(18)); // Item created.
	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
int atcommand_item2(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	struct item item_tmp;
	struct item_data *item_data;
	char item_name[100];
	int item_id, number = 0;
	int identify = 0, refine = 0, attr = 0;
	int c1 = 0, c2 = 0, c3 = 0, c4 = 0;
	int flag, bound = 0;
	int loop, get_count, i;
	nullpo_retr(-1, sd);

	memset(item_name, '\0', sizeof(item_name));

	if (!message || !*message || (
		sscanf(message, "\"%99[^\"]\" %d %d %d %d %d %d %d %d", item_name, &number, &identify, &refine, &attr, &c1, &c2, &c3, &c4) < 9 &&
		sscanf(message, "%99s %d %d %d %d %d %d %d %d", item_name, &number, &identify, &refine, &attr, &c1, &c2, &c3, &c4) < 9
	)) {
		clif_displaymessage(fd, "Please, enter all informations (usage: @item2 <item name or ID> <quantity>");
		clif_displaymessage(fd, "  <Identify_flag> <refine> <attribut> <Card1> <Card2> <Card3> <Card4>).");
		return -1;
	}

	if (number <= 0)
		number = 1;

	item_id = 0;
	if ((item_data = itemdb_searchname(item_name)) != NULL ||
	    (item_data = itemdb_exists(atoi(item_name))) != NULL)
		item_id = item_data->nameid;

	if( item_id > 500 )
	{
		if( !strcmpi(command+1,"bounditem2") )
			bound = 1;

		if( !itemdb_isstackable2(item_data) )
		{
			if( bound && (item_data->type == IT_PETEGG || item_data->type == IT_PETARMOR) )
			{
				clif_displaymessage(fd, "Cannot create bounded pet eggs or pet armors.");
				return -1;
			}

			loop = number;
			get_count = 1;
			if( item_data->type == IT_PETEGG )
			{
				identify = 1;
				refine = 0;
			}
			if( item_data->type == IT_PETARMOR )
				refine = 0;
			if( refine > MAX_REFINE )
				refine = MAX_REFINE;
		}
		else
		{
			if( bound )
			{
				clif_displaymessage(fd, "Cannot create bounded stackable items.");
				return -1;
			}

			loop = 1;
			get_count = number;
			identify = 1;
			refine = attr = 0;
		}

		for( i = 0; i < loop; i++ )
		{
			memset(&item_tmp, 0, sizeof(item_tmp));
			item_tmp.nameid = item_id;
			item_tmp.identify = identify;
			item_tmp.refine = refine;
			item_tmp.attribute = attr;
			item_tmp.card[0] = c1;
			item_tmp.card[1] = c2;
			item_tmp.card[2] = c3;
			item_tmp.card[3] = c4;
			item_tmp.bound = bound;

			if( (flag = pc_additem(sd, &item_tmp, get_count)) )
				clif_additem(sd, 0, 0, flag);

			//Logs (A)dmins items [Lupus]
			if( log_config.enable_logs&0x400 )
				log_pick_pc(sd, "A", item_tmp.nameid, number, &item_tmp, item_tmp.serial);

			item_tmp.serial = 0;
		}

		clif_displaymessage(fd, msg_txt(18)); // Item created.
	}
	else
	{
		clif_displaymessage(fd, msg_txt(19)); // Invalid item ID or name.
		return -1;
	}

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
int atcommand_itemreset(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	int i;
	nullpo_retr(-1, sd);

	for (i = 0; i < MAX_INVENTORY; i++) {
		if (sd->status.inventory[i].amount && sd->status.inventory[i].equip == 0) {

			//Logs (A)dmins items [Lupus]
			if(log_config.enable_logs&0x400)
				log_pick_pc(sd, "A", sd->status.inventory[i].nameid, -sd->status.inventory[i].amount, &sd->status.inventory[i], sd->status.inventory[i].serial );

			pc_delitem(sd, i, sd->status.inventory[i].amount, 0, 0);
		}
	}
	clif_displaymessage(fd, msg_txt(20)); // All of your items have been removed.

	return 0;
}

/*==========================================
 * Atcommand @lvlup
 *------------------------------------------*/
int atcommand_baselevelup(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	int level=0, i=0, status_point=0;
	nullpo_retr(-1, sd);
	level = atoi(message);

	if (!message || !*message || !level) {
		clif_displaymessage(fd, "Please, enter a level adjustment (usage: @lvup/@blevel/@baselvlup <number of levels>).");
		return -1;
	}

	if (level > 0) {
		if (sd->status.base_level == pc_maxbaselv(sd)) { // check for max level by Valaris
			clif_displaymessage(fd, msg_txt(47)); // Base level can't go any higher.
			return -1;
		} // End Addition
		if ((unsigned int)level > pc_maxbaselv(sd) || (unsigned int)level > pc_maxbaselv(sd) - sd->status.base_level) // fix positiv overflow
			level = pc_maxbaselv(sd) - sd->status.base_level;
		status_point += stats_point_table[sd->status.base_level+level] - stats_point_table[sd->status.base_level];

		sd->status.status_point += status_point;
		sd->status.base_level += (unsigned int)level;
		status_percent_heal(&sd->bl, 100, 100);
		clif_misceffect(&sd->bl, 0);
		clif_displaymessage(fd, msg_txt(21)); // Base level raised.
	} else {
		if (sd->status.base_level == 1) {
			clif_displaymessage(fd, msg_txt(158)); // Base level can't go any lower.
			return -1;
		}
		level*=-1;
		if ((unsigned int)level >= sd->status.base_level)
			level = sd->status.base_level-1;
		status_point += stats_point_table[sd->status.base_level] - stats_point_table[sd->status.base_level-level];
		if (sd->status.status_point < status_point)
			pc_resetstate(sd);
		if (sd->status.status_point < status_point)
			sd->status.status_point = 0;
		else
			sd->status.status_point -= status_point;
		sd->status.base_level -= (unsigned int)level;
		clif_displaymessage(fd, msg_txt(22)); // Base level lowered.
	}
	clif_updatestatus(sd, SP_STATUSPOINT);
	clif_updatestatus(sd, SP_BASELEVEL);
	clif_updatestatus(sd, SP_NEXTBASEEXP);
	status_calc_pc(sd, 0);
	if(sd->status.party_id)
		party_send_levelup(sd);
	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
int atcommand_joblevelup(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	int level=0;
	nullpo_retr(-1, sd);
	
	level = atoi(message);

	if (!message || !*message || !level) {
		clif_displaymessage(fd, "Please, enter a level adjustment (usage: @joblvup/@jlevel/@joblvlup <number of levels>).");
		return -1;
	}
	if (level > 0) {
		if (sd->status.job_level == pc_maxjoblv(sd)) {
			clif_displaymessage(fd, msg_txt(23)); // Job level can't go any higher.
			return -1;
		}
		if ((unsigned int)level > pc_maxjoblv(sd) || (unsigned int)level > pc_maxjoblv(sd) - sd->status.job_level) // fix positiv overflow
			level = pc_maxjoblv(sd) - sd->status.job_level;
		sd->status.job_level += (unsigned int)level;
		clif_updatestatus(sd, SP_JOBLEVEL);
		clif_updatestatus(sd, SP_NEXTJOBEXP);
		sd->status.skill_point += level;
		clif_updatestatus(sd, SP_SKILLPOINT);
		status_calc_pc(sd, 0);
		clif_misceffect(&sd->bl, 1);
		clif_displaymessage(fd, msg_txt(24)); // Job level raised.
	} else {
		if (sd->status.job_level == 1) {
			clif_displaymessage(fd, msg_txt(159)); // Job level can't go any lower.
			return -1;
		}
		level *=-1;
		if ((unsigned int)level >= sd->status.job_level) // fix negativ overflow
			level = sd->status.job_level-1;
		sd->status.job_level -= (unsigned int)level;
		clif_updatestatus(sd, SP_JOBLEVEL);
		clif_updatestatus(sd, SP_NEXTJOBEXP);
		if (sd->status.skill_point < level)
			pc_resetskill(sd,0);	//Reset skills since we need to substract more points.
		if (sd->status.skill_point < level)
			sd->status.skill_point = 0;
		else
			sd->status.skill_point -= level;
		clif_updatestatus(sd, SP_SKILLPOINT);
		status_calc_pc(sd, 0);
		clif_displaymessage(fd, msg_txt(25)); // Job level lowered.
	}

	return 0;
}

/*==========================================
 * @help
 *------------------------------------------*/
int atcommand_help(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	char buf[2048], w1[2048], w2[2048];
	int i, gm_level;
	FILE* fp;
	nullpo_retr(-1, sd);

	memset(buf, '\0', sizeof(buf));

	if ((fp = fopen(help_txt, "r")) != NULL) {
		clif_displaymessage(fd, msg_txt(26)); // Help commands:
		gm_level = pc_isGM(sd);
		while(fgets(buf, sizeof(buf), fp) != NULL) {
			if (buf[0] == '/' && buf[1] == '/')
				continue;
			for (i = 0; buf[i] != '\0'; i++) {
				if (buf[i] == '\r' || buf[i] == '\n') {
					buf[i] = '\0';
					break;
				}
			}
			if (sscanf(buf, "%2047[^:]:%2047[^\n]", w1, w2) < 2)
				clif_displaymessage(fd, buf);
			else if (gm_level >= atoi(w1))
				clif_displaymessage(fd, w2);
		}
		fclose(fp);
	} else {
		clif_displaymessage(fd, msg_txt(27)); // File help.txt not found.
		return -1;
	}

	return 0;
}

/*==========================================
 * @help2 - Char commands [Kayla]
 *------------------------------------------*/
int atcommand_help2(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	char buf[2048], w1[2048], w2[2048];
	int i, gm_level;
	FILE* fp;
	nullpo_retr(-1, sd);

	memset(buf, '\0', sizeof(buf));

	if ((fp = fopen(help2_txt, "r")) != NULL) {
		clif_displaymessage(fd, msg_txt(26)); // Help commands:
		gm_level = pc_isGM(sd);
		while(fgets(buf, sizeof(buf), fp) != NULL) {
			if (buf[0] == '/' && buf[1] == '/')
				continue;
			for (i = 0; buf[i] != '\0'; i++) {
				if (buf[i] == '\r' || buf[i] == '\n') {
					buf[i] = '\0';
					break;
				}
			}
			if (sscanf(buf, "%2047[^:]:%2047[^\n]", w1, w2) < 2)
				clif_displaymessage(fd, buf);
			else if (gm_level >= atoi(w1))
				clif_displaymessage(fd, w2);
		}
		fclose(fp);
	} else {
		clif_displaymessage(fd, msg_txt(27)); // File help.txt not found.
		return -1;
	}

	return 0;
}


// helper function, used in foreach calls to stop auto-attack timers
// parameter: '0' - everyone, 'id' - only those attacking someone with that id
static int atcommand_stopattack(struct block_list *bl,va_list ap)
{
	struct unit_data *ud = unit_bl2ud(bl);
	int id = va_arg(ap, int);
	if (ud && ud->attacktimer != INVALID_TIMER && (!id || id == ud->target))
	{
		unit_stop_attack(bl);
		return 1;
	}
	return 0;
}
/*==========================================
 *
 *------------------------------------------*/
static int atcommand_pvpoff_sub(struct block_list *bl,va_list ap)
{
	TBL_PC* sd = (TBL_PC*)bl;
	clif_pvpset(sd, 0, 0, 2);
	if (sd->pvp_timer != -1) {
		delete_timer(sd->pvp_timer, pc_calc_pvprank_timer);
		sd->pvp_timer = INVALID_TIMER;
	}
	return 0;
}

int atcommand_pvpoff(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	nullpo_retr(-1, sd);

	if (!map[sd->bl.m].flag.pvp) {
		clif_displaymessage(fd, msg_txt(160)); // PvP is already Off.
		return -1;
	}

	map[sd->bl.m].flag.pvp = 0;

	if (!battle_config.pk_mode)
		clif_send0199(sd->bl.m, 0);
	map_foreachinmap(atcommand_pvpoff_sub,sd->bl.m, BL_PC);
	map_foreachinmap(atcommand_stopattack,sd->bl.m, BL_CHAR, 0);
	clif_displaymessage(fd, msg_txt(31)); // PvP: Off.
	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
static int atcommand_pvpon_sub(struct block_list *bl,va_list ap)
{
	TBL_PC* sd = (TBL_PC*)bl;
	if (sd->state.pvpmode)
		pc_pvpmodeoff(sd, 1, 1);
	if (sd->pvp_timer == -1) {
		sd->pvp_timer = add_timer(gettick() + 1, pc_calc_pvprank_timer, sd->bl.id, 0);
		sd->pvp_rank = 0;
		sd->pvp_lastusers = 0;
		sd->pvp_point = 5;
		sd->pvp_won = 0;
		sd->pvp_lost = 0;
	}
	return 0;
}

int atcommand_pvpon(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	nullpo_retr(-1, sd);

	if (map[sd->bl.m].flag.pvp) {
		clif_displaymessage(fd, msg_txt(161)); // PvP is already On.
		return -1;
	}

	map[sd->bl.m].flag.pvp = 1;

	if (!battle_config.pk_mode)
	{// display pvp circle and rank
		clif_send0199(sd->bl.m, 1);
		map_foreachinmap(atcommand_pvpon_sub,sd->bl.m, BL_PC);
	}

	clif_displaymessage(fd, msg_txt(32)); // PvP: On.

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
int atcommand_gvgoff(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	nullpo_retr(-1, sd);

	if (!map[sd->bl.m].flag.gvg) {
		clif_displaymessage(fd, msg_txt(162)); // GvG is already Off.
		return -1;
	}
		
	map[sd->bl.m].flag.gvg = 0;
	clif_send0199(sd->bl.m, 0);
	map_foreachinmap(atcommand_stopattack,sd->bl.m, BL_CHAR, 0);
	clif_displaymessage(fd, msg_txt(33)); // GvG: Off.

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
int atcommand_gvgon(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	nullpo_retr(-1, sd);

	if (map[sd->bl.m].flag.gvg) {
		clif_displaymessage(fd, msg_txt(163)); // GvG is already On.
		return -1;
	}
	
	map[sd->bl.m].flag.gvg = 1;
	clif_send0199(sd->bl.m, 3);
	clif_displaymessage(fd, msg_txt(34)); // GvG: On.

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
int atcommand_model(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	int hair_style = 0, hair_color = 0, cloth_color = 0;
	nullpo_retr(-1, sd);

	memset(atcmd_output, '\0', sizeof(atcmd_output));

	if (!message || !*message || sscanf(message, "%d %d %d", &hair_style, &hair_color, &cloth_color) < 1) {
		sprintf(atcmd_output, "Please, enter at least a value (usage: @model <hair ID: %d-%d> <hair color: %d-%d> <clothes color: %d-%d>).",
		        MIN_HAIR_STYLE, MAX_HAIR_STYLE, MIN_HAIR_COLOR, MAX_HAIR_COLOR, MIN_CLOTH_COLOR, MAX_CLOTH_COLOR);
		clif_displaymessage(fd, atcmd_output);
		return -1;
	}

	if (hair_style >= MIN_HAIR_STYLE && hair_style <= MAX_HAIR_STYLE &&
		hair_color >= MIN_HAIR_COLOR && hair_color <= MAX_HAIR_COLOR &&
		cloth_color >= MIN_CLOTH_COLOR && cloth_color <= MAX_CLOTH_COLOR) {
			pc_changelook(sd, LOOK_HAIR, hair_style);
			pc_changelook(sd, LOOK_HAIR_COLOR, hair_color);
			pc_changelook(sd, LOOK_CLOTHES_COLOR, cloth_color);
			clif_displaymessage(fd, msg_txt(36)); // Appearence changed.
	} else {
		clif_displaymessage(fd, msg_txt(37)); // An invalid number was specified.
		return -1;
	}

	return 0;
}

/*==========================================
 * @dye && @ccolor
 *------------------------------------------*/
int atcommand_dye(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	int cloth_color = 0;
	nullpo_retr(-1, sd);

	memset(atcmd_output, '\0', sizeof(atcmd_output));

	if (!message || !*message || sscanf(message, "%d", &cloth_color) < 1) {
		sprintf(atcmd_output, "Please, enter a clothes color (usage: @dye/@ccolor <clothes color: %d-%d>).", MIN_CLOTH_COLOR, MAX_CLOTH_COLOR);
		clif_displaymessage(fd, atcmd_output);
		return -1;
	}

	if (cloth_color >= MIN_CLOTH_COLOR && cloth_color <= MAX_CLOTH_COLOR) {
		pc_changelook(sd, LOOK_CLOTHES_COLOR, cloth_color);
		clif_displaymessage(fd, msg_txt(36)); // Appearence changed.
	} else {
		clif_displaymessage(fd, msg_txt(37)); // An invalid number was specified.
		return -1;
	}

	return 0;
}

/*==========================================
 * @hairstyle && @hstyle
 *------------------------------------------*/
int atcommand_hair_style(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	int hair_style = 0;
	nullpo_retr(-1, sd);

	memset(atcmd_output, '\0', sizeof(atcmd_output));

	if (!message || !*message || sscanf(message, "%d", &hair_style) < 1) {
		sprintf(atcmd_output, "Please, enter a hair style (usage: @hairstyle/@hstyle <hair ID: %d-%d>).", MIN_HAIR_STYLE, MAX_HAIR_STYLE);
		clif_displaymessage(fd, atcmd_output);
		return -1;
	}

	if (hair_style >= MIN_HAIR_STYLE && hair_style <= MAX_HAIR_STYLE) {
			pc_changelook(sd, LOOK_HAIR, hair_style);
			clif_displaymessage(fd, msg_txt(36)); // Appearence changed.
	} else {
		clif_displaymessage(fd, msg_txt(37)); // An invalid number was specified.
		return -1;
	}

	return 0;
}

/*==========================================
 * @haircolor && @hcolor
 *------------------------------------------*/
int atcommand_hair_color(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	int hair_color = 0;
	nullpo_retr(-1, sd);

	memset(atcmd_output, '\0', sizeof(atcmd_output));

	if (!message || !*message || sscanf(message, "%d", &hair_color) < 1) {
		sprintf(atcmd_output, "Please, enter a hair color (usage: @haircolor/@hcolor <hair color: %d-%d>).", MIN_HAIR_COLOR, MAX_HAIR_COLOR);
		clif_displaymessage(fd, atcmd_output);
		return -1;
	}

	if (hair_color >= MIN_HAIR_COLOR && hair_color <= MAX_HAIR_COLOR) {
			pc_changelook(sd, LOOK_HAIR_COLOR, hair_color);
			clif_displaymessage(fd, msg_txt(36)); // Appearence changed.
	} else {
		clif_displaymessage(fd, msg_txt(37)); // An invalid number was specified.
		return -1;
	}

	return 0;
}

/*==========================================
 * @go [city_number or city_name] - Updated by Harbin
 *------------------------------------------*/
int atcommand_go(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	int i;
	int town;
	char map_name[MAP_NAME_LENGTH];
	int m;
 
	const struct {
		char map[MAP_NAME_LENGTH];
		int x, y;
	} data[] = {
		{ MAP_PRONTERA,    156, 191 }, //  0=Prontera
		{ MAP_MORROC,      156,  93 }, //  1=Morroc
		{ MAP_GEFFEN,      119,  59 }, //  2=Geffen
		{ MAP_PAYON,       162, 233 }, //  3=Payon
		{ MAP_ALBERTA,     192, 147 }, //  4=Alberta
		{ MAP_IZLUDE,      128, 114 }, //  5=Izlude
		{ MAP_ALDEBARAN,   140, 131 }, //  6=Al de Baran
		{ MAP_LUTIE,       147, 134 }, //  7=Lutie
		{ MAP_COMODO,      189, 155 }, //  8=Comodo
		{ MAP_YUNO,        157,  51 }, //  9=Yuno
		{ MAP_AMATSU,      198,  84 }, // 10=Amatsu
		{ MAP_GONRYUN,     159, 117 }, // 11=Gonryun
		{ MAP_UMBALA,       94, 157 }, // 12=Umbala
		{ MAP_NIFLHEIM,     21, 153 }, // 13=Niflheim
		{ MAP_LOUYANG,     217,  99 }, // 14=Louyang
		{ MAP_NOVICE,       53, 111 }, // 15=Training Grounds
		{ MAP_JAIL,         23,  61 }, // 16=Prison
		{ MAP_JAWAII,      249, 127 }, // 17=Jawaii
		{ MAP_AYOTHAYA,    150, 165 }, // 18=Ayothaya
		{ MAP_EINBROCH,     64, 200 }, // 19=Einbroch
		{ MAP_LIGHTHALZEN, 158,  92 }, // 20=Lighthalzen
		{ MAP_EINBECH,      70,  95 }, // 21=Einbech
		{ MAP_HUGEL,        96, 145 }, // 22=Hugel
		{ MAP_RACHEL,      130, 110 }, // 23=Rachel
		{ MAP_VEINS,       216, 123 }, // 24=Veins
		{ MAP_MOSCOVIA,    223, 184 }, // 25=Moscovia
		{ MAP_MIDCAMP,     219, 236 }, // 26=Mid Camp
		{ MAP_MANUK,       279, 231 }, // 27=Manuk
		{ MAP_SPLENDIDE,   199, 148 }, // 28=Splendide
		{ MAP_BRASILIS,    195, 222 }, // 29=Brasilis
		{ MAP_ERISCASTLE,  197, 189 }, // 30=El Dicastes
		{ MAP_MORA,        121, 166 }, // 31=Mora
		{ MAP_DEWATA,      200, 198 }, // 32=Dewata
		{ MAP_MALANGDO,    167, 139 }, // 33=Malangdo
		{ MAP_MERCADO,       3,  14 }, // 34=Mercado
		{ MAP_MERCHANT,     59,  36 }, // 35=Merchant
	};
 
	nullpo_retr(-1, sd);

	if( sd->npc_id || sd->vender_id || sd->buyer_id || sd->state.trading || sd->state.storage_flag )
	return -1;

	// Para desactivar el comando en mapas PVP
	if ((map[sd->bl.m].flag.pvp) && (40 >= pc_isGM(sd))) { // El Numero en rojo, quiere decir que solo los GM con el Nivel 10 o superior podr�n utilizar el comando en los mapas.
  	clif_displaymessage(sd->fd,"Lo siento, no puedes utilizar este comando en un mapa PVP.");
  	return -1;
	}

	// Para desactivar el comando en un Mapa GVG.
	if ((map[sd->bl.m].flag.gvg) && (40 >= pc_isGM(sd))) { // El Numero en rojo, quiere decir que solo los GM con el Nivel 10 o superior podr�n utilizar el comando en los mapas.
  	clif_displaymessage(sd->fd,"Lo Siento, no puedes utilizar este comando en un mapa de GVG.");
  	return -1;
	}

	if(sd->state.pvpmode) {
		clif_displaymessage(fd, "No puedes usar @go en modo PK.");
		return 0;
	}
 
	if( map[sd->bl.m].flag.nogo && battle_config.any_warp_GM_min_level > pc_isGM(sd) ) {
		clif_displaymessage(sd->fd,"You can not use @go on this map.");
		return 0;
	}
 
	memset(map_name, '\0', sizeof(map_name));
	memset(atcmd_output, '\0', sizeof(atcmd_output));
 
	// get the number
	town = atoi(message);
 
	// if no value, display all value
	if (!message || !*message || sscanf(message, "%11s", map_name) < 1 || town < 0 || town >= ARRAYLENGTH(data)) {
		clif_displaymessage(fd, msg_txt(38)); // Invalid location number, or name.
		clif_displaymessage(fd, msg_txt(82)); // Please provide a name or number from the list provided:
		clif_displaymessage(fd, " 0=Prontera         1=Morroc       2=Geffen");
		clif_displaymessage(fd, " 3=Payon            4=Alberta      5=Izlude");
		clif_displaymessage(fd, " 6=Al De Baran      7=Lutie        8=Comodo");
		clif_displaymessage(fd, " 9=Yuno             10=Amatsu      11=Gonryun");
		clif_displaymessage(fd, " 12=Umbala          13=Niflheim    14=Louyang");
		clif_displaymessage(fd, " 15=Novice Grounds  16=Prison      17=Jawaii");
		clif_displaymessage(fd, " 18=Ayothaya        19=Einbroch    20=Lighthalzen");
		clif_displaymessage(fd, " 21=Einbech         22=Hugel       23=Rachel");
		clif_displaymessage(fd, " 24=Veins           25=Moscovia    26=Mid Camp");
		clif_displaymessage(fd, " 27=Manuk           28=Splendide   29=Brasilis");
		clif_displaymessage(fd, " 30=El Dicastes     31=Mora        32=Dewata");
		clif_displaymessage(fd, " 33=Malangdo        34=Mercado     35=Merchant");
		return -1;
	}

	// get possible name of the city
	map_name[MAP_NAME_LENGTH-1] = '\0';
	for (i = 0; map_name[i]; i++)
		map_name[i] = TOLOWER(map_name[i]);
	// try to identify the map name
	if (strncmp(map_name, "prontera", 3) == 0) {
		town = 0;
	} else if (strncmp(map_name, "morocc", 3) == 0) {
		town = 1;
	} else if (strncmp(map_name, "geffen", 3) == 0) {
		town = 2;
	} else if (strncmp(map_name, "payon", 3) == 0 ||
	           strncmp(map_name, "paion", 3) == 0) {
		town = 3;
	} else if (strncmp(map_name, "alberta", 3) == 0) {
		town = 4;
	} else if (strncmp(map_name, "izlude", 3) == 0 ||
	           strncmp(map_name, "islude", 3) == 0) {
		town = 5;
	} else if (strncmp(map_name, "aldebaran", 3) == 0 ||
	           strcmp(map_name,  "al") == 0) {
		town = 6;
	} else if (strncmp(map_name, "lutie", 3) == 0 ||
	           strcmp(map_name,  "christmas") == 0 ||
	           strncmp(map_name, "xmas", 3) == 0 ||
	           strncmp(map_name, "x-mas", 3) == 0) {
		town = 7;
	} else if (strncmp(map_name, "comodo", 3) == 0) {
		town = 8;
	} else if (strncmp(map_name, "yuno", 3) == 0) {
		town = 9;
	} else if (strncmp(map_name, "amatsu", 3) == 0) {
		town = 10;
	} else if (strncmp(map_name, "gonryun", 3) == 0) {
		town = 11;
	} else if (strncmp(map_name, "umbala", 3) == 0) {
		town = 12;
	} else if (strncmp(map_name, "niflheim", 3) == 0) {
		town = 13;
	} else if (strncmp(map_name, "louyang", 3) == 0) {
		town = 14;
	} else if (strncmp(map_name, "new_1-1", 3) == 0 ||
	           strncmp(map_name, "startpoint", 3) == 0 ||
	           strncmp(map_name, "begining", 3) == 0) {
		town = 15;
	} else if (strncmp(map_name, "sec_pri", 3) == 0 ||
	           strncmp(map_name, "prison", 3) == 0 ||
	           strncmp(map_name, "jails", 3) == 0) {
		town = 16;
	} else if (strncmp(map_name, "jawaii", 3) == 0 ||
	           strncmp(map_name, "jawai", 3) == 0) {
		town = 17;
	} else if (strncmp(map_name, "ayothaya", 3) == 0 ||
	           strncmp(map_name, "ayotaya", 3) == 0) {
		town = 18;
	} else if (strncmp(map_name, "einbroch", 5) == 0 ||
	           strncmp(map_name, "ainbroch", 5) == 0) {
		town = 19;
	} else if (strncmp(map_name, "lighthalzen", 3) == 0) {
		town = 20;
	} else if (strncmp(map_name, "einbech", 3) == 0) {
		town = 21;
	} else if (strncmp(map_name, "hugel", 3) == 0) {
		town = 22;
	} else if (strncmp(map_name, "rachel", 3) == 0) {
		town = 23;
	} else if (strncmp(map_name, "veins", 3) == 0) {
		town = 24;
	} else if (strncmp(map_name, "moscovia", 3) == 0) {
		town = 25;
	} else if (strncmp(map_name, "mid_camp", 3) == 0 ||
	           strncmp(map_name, "midcamp", 3) == 0) {
		town = 26;
	} else if (strncmp(map_name, "manuk", 3) == 0) {
		town = 27;
	} else if (strncmp(map_name, "splendide", 3) == 0) {
		town = 28;
	} else if (strncmp(map_name, "brasilis", 3) == 0) {
		town = 29;
	} else if (strncmp(map_name, "dicastes01", 3) == 0 ||
	           strncmp(map_name, "dicastes", 3) == 0 ||
	           strncmp(map_name, "eldicastes", 3) == 0) {
		town = 30;
	} else if (strncmp(map_name, "mora", 3) == 0) {
		town = 31;
	} else if (strncmp(map_name, "dewata", 3) == 0) {
		town = 32;
	} else if (strncmp(map_name, "malangdo", 3) == 0) {
		town = 33;
	} else if (strncmp(map_name, "mercado", 3) == 0) {
		town = 34;
	} else if (strncmp(map_name, "mercado", 3) == 0) {
		town = 35;
	}

	if (town >= 0 && town < ARRAYLENGTH(data))
	{
		m = map_mapname2mapid(data[town].map);
		if (m >= 0 && map[m].flag.nowarpto && battle_config.any_warp_GM_min_level > pc_isGM(sd)) {
			clif_displaymessage(fd, msg_txt(247));
			return -1;
		}
		if (sd->bl.m >= 0 && map[sd->bl.m].flag.nowarp && battle_config.any_warp_GM_min_level > pc_isGM(sd)) {
			clif_displaymessage(fd, msg_txt(248));
			return -1;
		}
		if (pc_setpos(sd, mapindex_name2id(data[town].map), data[town].x, data[town].y, 3) == 0) {
			clif_displaymessage(fd, msg_txt(0)); // Warped.
		} else {
			clif_displaymessage(fd, msg_txt(1)); // Map not found.
			return -1;
		}
	} else { // if you arrive here, you have an error in town variable when reading of names
		clif_displaymessage(fd, msg_txt(38)); // Invalid location number or name.
		return -1;
	}
 
	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
int atcommand_monster(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	char name[NAME_LENGTH];
	char monster[NAME_LENGTH];
	int mob_id;
	int number = 0;
	int count;
	int i, k, range;
	short mx, my;
	nullpo_retr(-1, sd);

	memset(name, '\0', sizeof(name));
	memset(monster, '\0', sizeof(monster));
	memset(atcmd_output, '\0', sizeof(atcmd_output));

	if (!message || !*message) {
			clif_displaymessage(fd, msg_txt(80)); // Give the display name or monster name/id please.
			return -1;
	}
	if (sscanf(message, "\"%23[^\"]\" %23s %d", name, monster, &number) > 1 ||
		sscanf(message, "%23s \"%23[^\"]\" %d", monster, name, &number) > 1) {
		//All data can be left as it is.
	} else if ((count=sscanf(message, "%23s %d %23s", monster, &number, name)) > 1) {
		//Here, it is possible name was not given and we are using monster for it.
		if (count < 3) //Blank mob's name.
			name[0] = '\0';
	} else if (sscanf(message, "%23s %23s %d", name, monster, &number) > 1) {
		//All data can be left as it is.
	} else if (sscanf(message, "%23s", monster) > 0) {
		//As before, name may be already filled.
		name[0] = '\0';
	} else {
		clif_displaymessage(fd, msg_txt(80)); // Give a display name and monster name/id please.
		return -1;
	}

	if ((mob_id = mobdb_searchname(monster)) == 0) // check name first (to avoid possible name begining by a number)
		mob_id = mobdb_checkid(atoi(monster));

	if (mob_id == 0) {
		clif_displaymessage(fd, msg_txt(40)); // Invalid monster ID or name.
		return -1;
	}

	if (mob_id == MOBID_EMPERIUM) {
		clif_displaymessage(fd, msg_txt(83)); // Monster 'Emperium' cannot be spawned.
		return -1;
	}

	if (number <= 0)
		number = 1;

	if (strlen(name) < 1)
		strcpy(name, "--ja--");

	// If value of atcommand_spawn_quantity_limit directive is greater than or equal to 1 and quantity of monsters is greater than value of the directive
	if (battle_config.atc_spawn_quantity_limit && number > battle_config.atc_spawn_quantity_limit)
		number = battle_config.atc_spawn_quantity_limit;

	if (battle_config.etc_log)
		ShowInfo("%s monster='%s' name='%s' id=%d count=%d (%d,%d)\n", command, monster, name, mob_id, number, sd->bl.x, sd->bl.y);

	count = 0;
	range = (int)sqrt((float)number) +2; // calculation of an odd number (+ 4 area around)
	for (i = 0; i < number; i++) {
		map_search_freecell(&sd->bl, 0, &mx,  &my, range, range, 0);
		k = mob_once_spawn(sd, sd->bl.m, mx, my, name, mob_id, 1, "");
		count += (k != 0) ? 1 : 0;
	}

	if (count != 0)
		if (number == count)
			clif_displaymessage(fd, msg_txt(39)); // All monster summoned!
		else {
			sprintf(atcmd_output, msg_txt(240), count); // %d monster(s) summoned!
			clif_displaymessage(fd, atcmd_output);
		}
	else {
		clif_displaymessage(fd, msg_txt(40)); // Invalid monster ID or name.
		return -1;
	}

	return 0;
}

// small monster spawning [Valaris]
int atcommand_monstersmall(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	char name[NAME_LENGTH] = "";
	char monster[NAME_LENGTH] = "";
	int mob_id = 0;
	int number = 0;
	int x = 0;
	int y = 0;
	int count;
	int i;

	nullpo_retr(-1, sd);

	if (!message || !*message) {
		clif_displaymessage(fd, "Give a monster name/id please.");
		return -1;
	}

	if (sscanf(message, "\"%23[^\"]\" %23s %d %d %d", name, monster, &number, &x, &y) < 2 &&
	    sscanf(message, "%23s \"%23[^\"]\" %d %d %d", monster, name, &number, &x, &y) < 2 &&
	    sscanf(message, "%23s %d %23s %d %d", monster, &number, name, &x, &y) < 1) {
		clif_displaymessage(fd, "Give a monster name/id please.");
		return -1;
	}

	// If monster identifier/name argument is a name
	if ((mob_id = mobdb_searchname(monster)) == 0) // check name first (to avoid possible name begining by a number)
		mob_id = atoi(monster);

	if (mob_id == 0) {
		clif_displaymessage(fd, msg_txt(40));
		return -1;
	}

	if (mob_id == MOBID_EMPERIUM) {
		clif_displaymessage(fd, msg_txt(83));	// Cannot spawn emperium
		return -1;
	}

	if (mobdb_checkid(mob_id) == 0) {
		clif_displaymessage(fd, "Invalid monster ID"); // Invalid Monster ID.
		return -1;
	}

	if (number <= 0)
		number = 1;

	if (strlen(name) < 1)
		strcpy(name, "--ja--");

	// If value of atcommand_spawn_quantity_limit directive is greater than or equal to 1 and quantity of monsters is greater than value of the directive
	if (battle_config.atc_spawn_quantity_limit >= 1 && number > battle_config.atc_spawn_quantity_limit)
		number = battle_config.atc_spawn_quantity_limit;

	count = 0;
	for (i = 0; i < number; i++) {
		int mx, my;
		if (x <= 0)
			mx = sd->bl.x + (rand() % 11 - 5);
		else
			mx = x;
		if (y <= 0)
			my = sd->bl.y + (rand() % 11 - 5);
		else
			my = y;
		count += (mob_once_spawn(sd, sd->bl.m, mx, my, name, mob_id, 1, "2") != 0) ? 1 : 0;
	}

	if (count != 0)
		clif_displaymessage(fd, msg_txt(39)); // Monster Summoned!!
	else
		clif_displaymessage(fd, msg_txt(40)); // Invalid Monster ID.

	return 0;
}
// big monster spawning [Valaris]
int atcommand_monsterbig(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	char name[NAME_LENGTH] = "";
	char monster[NAME_LENGTH] = "";
	int mob_id = 0;
	int number = 0;
	int x = 0;
	int y = 0;
	int count;
	int i;

	nullpo_retr(-1, sd);

	if (!message || !*message) {
		clif_displaymessage(fd, "Give a monster name/id please.");
		return -1;
	}

	if (sscanf(message, "\"%23[^\"]\" %23s %d %d %d", name, monster, &number, &x, &y) < 2 &&
	    sscanf(message, "%23s \"%23[^\"]\" %d %d %d", monster, name, &number, &x, &y) < 2 &&
	    sscanf(message, "%23s %d %23s %d %d", monster, &number, name, &x, &y) < 1) {
		clif_displaymessage(fd, "Give a monster name/id please.");
		return -1;
	}

	// If monster identifier/name argument is a name
	if ((mob_id = mobdb_searchname(monster)) == 0) // check name first (to avoid possible name begining by a number)
		mob_id = atoi(monster);

	if (mob_id == 0) {
		clif_displaymessage(fd, msg_txt(40));
		return -1;
	}

	if (mob_id == MOBID_EMPERIUM) {
		clif_displaymessage(fd, msg_txt(83));	// Cannot spawn emperium
		return -1;
	}

	if (mobdb_checkid(mob_id) == 0) {
		clif_displaymessage(fd, "Invalid monster ID"); // Invalid Monster ID.
		return -1;
	}

	if (number <= 0)
		number = 1;

	if (strlen(name) < 1)
		strcpy(name, "--ja--");

	// If value of atcommand_spawn_quantity_limit directive is greater than or equal to 1 and quantity of monsters is greater than value of the directive
	if (battle_config.atc_spawn_quantity_limit >= 1 && number > battle_config.atc_spawn_quantity_limit)
		number = battle_config.atc_spawn_quantity_limit;

	count = 0;
	for (i = 0; i < number; i++) {
		int mx, my;
		if (x <= 0)
			mx = sd->bl.x + (rand() % 11 - 5);
		else
			mx = x;
		if (y <= 0)
			my = sd->bl.y + (rand() % 11 - 5);
		else
			my = y;
		count += (mob_once_spawn(sd, sd->bl.m, mx, my, name, mob_id, 1, "4") != 0) ? 1 : 0;
	}

	if (count != 0)
		clif_displaymessage(fd, msg_txt(39)); // Monster Summoned!!
	else
		clif_displaymessage(fd, msg_txt(40)); // Invalid Monster ID.

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
static int atkillmonster_sub(struct block_list *bl, va_list ap)
{
	struct mob_data *md;
	int flag;
	
	nullpo_retr(0, md=(struct mob_data *)bl);
	flag = va_arg(ap, int);

	if (md->guardian_data)
		return 0; //Do not touch WoE mobs!
	
	if (flag)
		status_zap(bl,md->status.hp, 0);
	else
		status_kill(bl);
	return 1;
}

void atcommand_killmonster_sub(const int fd, struct map_session_data* sd, const char* message, const int drop)
{
	int map_id;
	char map_name[MAP_NAME_LENGTH_EXT];

	if (!sd) return;

	memset(map_name, '\0', sizeof(map_name));

	if (!message || !*message || sscanf(message, "%15s", map_name) < 1)
		map_id = sd->bl.m;
	else {
		if ((map_id = map_mapname2mapid(map_name)) < 0)
			map_id = sd->bl.m;
	}

	map_foreachinmap(atkillmonster_sub, map_id, BL_MOB, drop);

	clif_displaymessage(fd, msg_txt(165)); // All monsters killed!

	return;
}

int atcommand_killmonster(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	atcommand_killmonster_sub(fd, sd, message, 1);
	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
int atcommand_killmonster2(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	atcommand_killmonster_sub(fd, sd, message, 0);
	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
int atcommand_refine(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	int i,j, position = 0, refine = 0, current_position, final_refine;
	int count;
	nullpo_retr(-1, sd);

	memset(atcmd_output, '\0', sizeof(atcmd_output));

	if (!message || !*message || sscanf(message, "%d %d", &position, &refine) < 2) {
		clif_displaymessage(fd, "Please, enter a position and an amount (usage: @refine <equip position> <+/- amount>).");
		sprintf(atcmd_output, "%d: Lower Headgear", EQP_HEAD_LOW);
		clif_displaymessage(fd, atcmd_output);
		sprintf(atcmd_output, "%d: Right Hand", EQP_HAND_R);
		clif_displaymessage(fd, atcmd_output);
		sprintf(atcmd_output, "%d: Garment", EQP_GARMENT);
		clif_displaymessage(fd, atcmd_output);
		sprintf(atcmd_output, "%d: Left Accessory", EQP_ACC_L);
		clif_displaymessage(fd, atcmd_output);
		sprintf(atcmd_output, "%d: Body Armor", EQP_ARMOR);
		clif_displaymessage(fd, atcmd_output);
		sprintf(atcmd_output, "%d: Left Hand", EQP_HAND_L);
		clif_displaymessage(fd, atcmd_output);
		sprintf(atcmd_output, "%d: Shoes", EQP_SHOES);
		clif_displaymessage(fd, atcmd_output);
		sprintf(atcmd_output, "%d: Right Accessory", EQP_ACC_R);
		clif_displaymessage(fd, atcmd_output);
		sprintf(atcmd_output, "%d: Top Headgear", EQP_HEAD_TOP);
		clif_displaymessage(fd, atcmd_output);
		sprintf(atcmd_output, "%d: Mid Headgear", EQP_HEAD_MID);
		clif_displaymessage(fd, atcmd_output);
		return -1;
	}

	refine = cap_value(refine, -MAX_REFINE, MAX_REFINE);

	count = 0;
	for (j = 0; j < EQI_MAX-1; j++) {
		if ((i = sd->equip_index[j]) < 0)
			continue;
		if(j == EQI_HAND_R && sd->equip_index[EQI_HAND_L] == i)
			continue;
		if(j == EQI_HEAD_MID && sd->equip_index[EQI_HEAD_LOW] == i)
			continue;
		if(j == EQI_HEAD_TOP && (sd->equip_index[EQI_HEAD_MID] == i || sd->equip_index[EQI_HEAD_LOW] == i))
			continue;

		if(position && !(sd->status.inventory[i].equip & position))
			continue;

		final_refine = cap_value(sd->status.inventory[i].refine + refine, 0, MAX_REFINE);
		if (sd->status.inventory[i].refine != final_refine) {
			sd->status.inventory[i].refine = final_refine;
			current_position = sd->status.inventory[i].equip;
			pc_unequipitem(sd, i, 3);
			clif_refine(fd, 0, i, sd->status.inventory[i].refine);
			clif_delitem(sd, i, 1, 3);
			clif_additem(sd, i, 1, 0);
			pc_equipitem(sd, i, current_position);
			clif_misceffect(&sd->bl, 3);
			count++;
		}
	}

	if (count == 0)
		clif_displaymessage(fd, msg_txt(166)); // No item has been refined.
	else if (count == 1)
		clif_displaymessage(fd, msg_txt(167)); // 1 item has been refined.
	else {
		sprintf(atcmd_output, msg_txt(168), count); // %d items have been refined.
		clif_displaymessage(fd, atcmd_output);
	}

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
int atcommand_produce(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	char item_name[100];
	int item_id, attribute = 0, star = 0;
	int flag = 0;
	struct item_data *item_data;
	struct item tmp_item;
	nullpo_retr(-1, sd);

	memset(atcmd_output, '\0', sizeof(atcmd_output));
	memset(item_name, '\0', sizeof(item_name));

	if (!message || !*message || (
		sscanf(message, "\"%99[^\"]\" %d %d", item_name, &attribute, &star) < 1 &&
		sscanf(message, "%99s %d %d", item_name, &attribute, &star) < 1
	)) {
		clif_displaymessage(fd, "Please, enter at least an item name/id (usage: @produce <equip name or equip ID> <element> <# of very's>).");
		return -1;
	}

	item_id = 0;
	if ((item_data = itemdb_searchname(item_name)) == NULL &&
	    (item_data = itemdb_exists(atoi(item_name))) == NULL)
	{
		clif_displaymessage(fd, msg_txt(170)); //This item is not an equipment.
		return -1;
	}
	item_id = item_data->nameid;
	if (itemdb_isequip2(item_data)) {
		if (attribute < MIN_ATTRIBUTE || attribute > MAX_ATTRIBUTE)
			attribute = ATTRIBUTE_NORMAL;
		if (star < MIN_STAR || star > MAX_STAR)
			star = 0;
		memset(&tmp_item, 0, sizeof tmp_item);
		tmp_item.nameid = item_id;
		tmp_item.amount = 1;
		tmp_item.identify = 1;
		tmp_item.card[0] = CARD0_FORGE;
		tmp_item.card[1] = item_data->type==IT_WEAPON?
			((star*5) << 8) + attribute:0;
		tmp_item.card[2] = GetWord(sd->status.char_id, 0);
		tmp_item.card[3] = GetWord(sd->status.char_id, 1);
		clif_produceeffect(sd, 0, item_id);
		clif_misceffect(&sd->bl, 3);
		if ((flag = pc_additem(sd, &tmp_item, 1)))
			clif_additem(sd, 0, 0, flag);

		//Logs (A)dmins items [Lupus]
		if(log_config.enable_logs&0x400)
			log_pick_pc(sd, "A", tmp_item.nameid, 1, &tmp_item, tmp_item.serial);
	} else {
		sprintf(atcmd_output, msg_txt(169), item_id, item_data->name); // The item (%d: '%s') is not equipable.
		clif_displaymessage(fd, atcmd_output);
		return -1;
	}

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
int atcommand_memo(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	int position = 0;
	nullpo_retr(-1, sd);

	memset(atcmd_output, '\0', sizeof(atcmd_output));

	if( !message || !*message || sscanf(message, "%d", &position) < 1 )
	{
		int i;
		clif_displaymessage(sd->fd,  "Your actual memo positions are:");
		for( i = 0; i < MAX_MEMOPOINTS; i++ )
		{
			if( sd->status.memo_point[i].map )
				sprintf(atcmd_output, "%d - %s (%d,%d)", i, mapindex_id2name(sd->status.memo_point[i].map), sd->status.memo_point[i].x, sd->status.memo_point[i].y);
			else
				sprintf(atcmd_output, msg_txt(171), i); // %d - void
			clif_displaymessage(sd->fd, atcmd_output);
 		}
		return 0;
 	}
 
	if( position < 0 || position >= MAX_MEMOPOINTS )
	{
		sprintf(atcmd_output, "Please, enter a valid position (usage: @memo <memo_position:%d-%d>).", 0, MAX_MEMOPOINTS-1);
		clif_displaymessage(fd, atcmd_output);
		return -1;
	}

	pc_memo(sd, position);
	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
int atcommand_gat(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	int y;
	nullpo_retr(-1, sd);

	memset(atcmd_output, '\0', sizeof(atcmd_output));

	for (y = 2; y >= -2; y--) {
		sprintf(atcmd_output, "%s (x= %d, y= %d) %02X %02X %02X %02X %02X",
			map[sd->bl.m].name,   sd->bl.x - 2, sd->bl.y + y,
 			map_getcell(sd->bl.m, sd->bl.x - 2, sd->bl.y + y, CELL_GETTYPE),
 			map_getcell(sd->bl.m, sd->bl.x - 1, sd->bl.y + y, CELL_GETTYPE),
 			map_getcell(sd->bl.m, sd->bl.x,     sd->bl.y + y, CELL_GETTYPE),
 			map_getcell(sd->bl.m, sd->bl.x + 1, sd->bl.y + y, CELL_GETTYPE),
 			map_getcell(sd->bl.m, sd->bl.x + 2, sd->bl.y + y, CELL_GETTYPE));

		clif_displaymessage(fd, atcmd_output);
	}

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
int atcommand_displaystatus(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	int i, type, flag, tick, val1, val2, val3;
	nullpo_retr(-1, sd);
	
	if (!message || !*message || (i = sscanf(message, "%d %d %d %d %d %d", &type, &flag, &tick, &val1, &val2, &val3)) < 1) {
		clif_displaymessage(fd, "Please, enter a status type/flag (usage: @displaystatus <status type> <flag> <tick> <val1> <val2> <val3>).");
		return -1;
	}
	if (i < 2) flag = 1;
	if (i < 3) tick = 0;

	clif_status_change(&sd->bl, type, flag, tick, val1, val2, val3);

	return 0;
}

/*==========================================
 * @stpoint (Rewritten by [Yor])
 *------------------------------------------*/
int atcommand_statuspoint(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	int point, new_status_point;

	if (!message || !*message || (point = atoi(message)) == 0) {
		clif_displaymessage(fd, "Please, enter a number (usage: @stpoint <number of points>).");
		return -1;
	}

	if (point < 0 && sd->status.status_point < -point)
		new_status_point = 0;
	else
		new_status_point = sd->status.status_point + point;

	if (new_status_point != (int)sd->status.status_point) {
		sd->status.status_point = new_status_point;
		clif_updatestatus(sd, SP_STATUSPOINT);
		clif_displaymessage(fd, msg_txt(174)); // Number of status points changed.
	} else {
		if (point < 0)
			clif_displaymessage(fd, msg_txt(41)); // Unable to decrease the number/value.
		else
			clif_displaymessage(fd, msg_txt(149)); // Unable to increase the number/value.
		return -1;
	}

	return 0;
}

/*==========================================
 * @skpoint (Rewritten by [Yor])
 *------------------------------------------*/
int atcommand_skillpoint(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	int point, new_skill_point;
	nullpo_retr(-1, sd);

	if (!message || !*message || (point = atoi(message)) == 0) {
		clif_displaymessage(fd, "Please, enter a number (usage: @skpoint <number of points>).");
		return -1;
	}

	if (point < 0 && sd->status.skill_point < -point)
		new_skill_point = 0;
	else
		new_skill_point = sd->status.skill_point + point;
	
	if (new_skill_point != (int)sd->status.skill_point) {
		sd->status.skill_point = new_skill_point;
		clif_updatestatus(sd, SP_SKILLPOINT);
		clif_displaymessage(fd, msg_txt(175)); // Number of skill points changed.
	} else {
		if (point < 0)
			clif_displaymessage(fd, msg_txt(41)); // Unable to decrease the number/value.
		else
			clif_displaymessage(fd, msg_txt(149)); // Unable to increase the number/value.
		return -1;
	}

	return 0;
}

/*==========================================
 * @zeny (Rewritten by [Yor])
 *------------------------------------------*/
int atcommand_zeny(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	int zeny, new_zeny;
	nullpo_retr(-1, sd);

	if (!message || !*message || (zeny = atoi(message)) == 0) {
		clif_displaymessage(fd, "Please, enter an amount (usage: @zeny <amount>).");
		return -1;
	}

	new_zeny = sd->status.zeny + zeny;
	if (zeny > 0 && (zeny > MAX_ZENY || new_zeny > MAX_ZENY)) // fix positiv overflow
		new_zeny = MAX_ZENY;
	else if (zeny < 0 && (zeny < -MAX_ZENY || new_zeny < 0)) // fix negativ overflow
		new_zeny = 0;

	if (new_zeny != sd->status.zeny) {
		sd->status.zeny = new_zeny;
		clif_updatestatus(sd, SP_ZENY);
		clif_displaymessage(fd, msg_txt(176)); // Current amount of zeny changed.
	} else {
		if (zeny < 0)
			clif_displaymessage(fd, msg_txt(41)); // Unable to decrease the number/value.
		else
			clif_displaymessage(fd, msg_txt(149)); // Unable to increase the number/value.
		return -1;
	}

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
int atcommand_param(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	int i, value = 0, new_value, max;
	const char* param[] = { "str", "agi", "vit", "int", "dex", "luk" };
	short* status[6];
 	//we don't use direct initialization because it isn't part of the c standard.
	nullpo_retr(-1, sd);
	
	memset(atcmd_output, '\0', sizeof(atcmd_output));

	if (!message || !*message || sscanf(message, "%d", &value) < 1 || value == 0) {
		sprintf(atcmd_output, "Please, enter a valid value (usage: @str,@agi,@vit,@int,@dex,@luk <+/-adjustment>).");
		clif_displaymessage(fd, atcmd_output);
		return -1;
	}

	ARR_FIND( 0, ARRAYLENGTH(param), i, strcmpi(command+1, param[i]) == 0 );

	if( i == ARRAYLENGTH(param) || i > MAX_STATUS_TYPE) { // normally impossible...
		sprintf(atcmd_output, "Please, enter a valid value (usage: @str,@agi,@vit,@int,@dex,@luk <+/-adjustment>).");
		clif_displaymessage(fd, atcmd_output);
		return -1;
	}

	status[0] = &sd->status.str;
	status[1] = &sd->status.agi;
	status[2] = &sd->status.vit;
	status[3] = &sd->status.int_;
	status[4] = &sd->status.dex;
	status[5] = &sd->status.luk;

	max = SHRT_MAX;
	new_value = cap_value(*status[i] + value, 1, max);
	
	if (new_value != (int)*status[i]) {
		*status[i] = new_value;
		clif_updatestatus(sd, SP_STR + i);
		clif_updatestatus(sd, SP_USTR + i);
		status_calc_pc(sd, 0);
		clif_displaymessage(fd, msg_txt(42)); // Stat changed.
	} else {
		if (value < 0)
			clif_displaymessage(fd, msg_txt(41)); // Unable to decrease the number/value.
		else
			clif_displaymessage(fd, msg_txt(149)); // Unable to increase the number/value.
		return -1;
	}

	return 0;
}

/*==========================================
 * Stat all by fritz (rewritten by [Yor])
 *------------------------------------------*/
int atcommand_stat_all(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	int index, count, value, max, new_value;
	short* status[6];
 	//we don't use direct initialization because it isn't part of the c standard.
	nullpo_retr(-1, sd);
	
	status[0] = &sd->status.str;
	status[1] = &sd->status.agi;
	status[2] = &sd->status.vit;
	status[3] = &sd->status.int_;
	status[4] = &sd->status.dex;
	status[5] = &sd->status.luk;

	if (!message || !*message || sscanf(message, "%d", &value) < 1 || value == 0) {
		value = pc_maxparameter(sd);
		max = pc_maxparameter(sd);
	} else {
		max = SHRT_MAX;
	}

	count = 0;
	for (index = 0; index < ARRAYLENGTH(status); index++) {

		if (value > 0 && *status[index] > max - value)
			new_value = max;
		else if (value < 0 && *status[index] <= -value)
			new_value = 1;
		else
			new_value = *status[index] +value;
		
		if (new_value != (int)*status[index]) {
			*status[index] = new_value;
			clif_updatestatus(sd, SP_STR + index);
			clif_updatestatus(sd, SP_USTR + index);
			count++;
		}
	}

	if (count > 0) { // if at least 1 stat modified
		status_calc_pc(sd, 0);
		clif_displaymessage(fd, msg_txt(84)); // All stats changed!
	} else {
		if (value < 0)
			clif_displaymessage(fd, msg_txt(177)); // You cannot decrease that stat anymore.
		else
			clif_displaymessage(fd, msg_txt(178)); // You cannot increase that stat anymore.
		return -1;
	}

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
int atcommand_guildlevelup(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	int level = 0;
	short added_level;
	struct guild *guild_info;
	nullpo_retr(-1, sd);

	if (!message || !*message || sscanf(message, "%d", &level) < 1 || level == 0) {
		clif_displaymessage(fd, "Please, enter a valid level (usage: @guildlvup/@guildlvlup <# of levels>).");
		return -1;
	}

	if (sd->status.guild_id <= 0 || (guild_info = guild_search(sd->status.guild_id)) == NULL) {
		clif_displaymessage(fd, msg_txt(43)); // You're not in a guild.
		return -1;
	}
	//if (strcmp(sd->status.name, guild_info->master) != 0) {
	//	clif_displaymessage(fd, msg_txt(44)); // You're not the master of your guild.
	//	return -1;
	//}

	added_level = (short)level;
	if (level > 0 && (level > MAX_GUILDLEVEL || added_level > ((short)MAX_GUILDLEVEL - guild_info->guild_lv))) // fix positiv overflow
		added_level = (short)MAX_GUILDLEVEL - guild_info->guild_lv;
	else if (level < 0 && (level < -MAX_GUILDLEVEL || added_level < (1 - guild_info->guild_lv))) // fix negativ overflow
		added_level = 1 - guild_info->guild_lv;

	if (added_level != 0) {
		intif_guild_change_basicinfo(guild_info->guild_id, GBI_GUILDLV, &added_level, 2);
		clif_displaymessage(fd, msg_txt(179)); // Guild level changed.
	} else {
		clif_displaymessage(fd, msg_txt(45)); // Guild level change failed.
		return -1;
	}

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
int atcommand_makeegg(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	struct item_data *item_data;
	int id, pet_id;
	nullpo_retr(-1, sd);

	if (!message || !*message) {
		clif_displaymessage(fd, "Please, enter a monster/egg name/id (usage: @makeegg <pet>).");
		return -1;
	}

	if ((item_data = itemdb_searchname(message)) != NULL) // for egg name
		id = item_data->nameid;
	else
	if ((id = mobdb_searchname(message)) != 0) // for monster name
		;
	else
		id = atoi(message);

	pet_id = search_petDB_index(id, PET_CLASS);
	if (pet_id < 0)
		pet_id = search_petDB_index(id, PET_EGG);
	if (pet_id >= 0) {
		sd->catch_target_class = pet_db[pet_id].class_;
		intif_create_pet(
			sd->status.account_id, sd->status.char_id,
			(short)pet_db[pet_id].class_, (short)mob_db(pet_db[pet_id].class_)->lv,
			(short)pet_db[pet_id].EggID, 0, (short)pet_db[pet_id].intimate,
			100, 0, 1, pet_db[pet_id].jname);
	} else {
		clif_displaymessage(fd, msg_txt(180)); // The monster/egg name/id doesn't exist.
		return -1;
	}

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
int atcommand_hatch(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	nullpo_retr(-1, sd);
	if (sd->status.pet_id <= 0)
		clif_sendegg(sd);
	else {
		clif_displaymessage(fd, msg_txt(181)); // You already have a pet.
		return -1;
	}

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
int atcommand_petfriendly(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	int friendly;
	struct pet_data *pd;
	nullpo_retr(-1, sd);

	if (!message || !*message || (friendly = atoi(message)) < 0) {
		clif_displaymessage(fd, "Please, enter a valid value (usage: @petfriendly <0-1000>).");
		return -1;
	}

	pd = sd->pd;
	if (!pd) {
		clif_displaymessage(fd, msg_txt(184)); // Sorry, but you have no pet.
		return -1;
	}
	
	if (friendly < 0 || friendly > 1000)
	{
		clif_displaymessage(fd, msg_txt(37)); // An invalid number was specified.
		return -1;
	}
	
	if (friendly == pd->pet.intimate) {
		clif_displaymessage(fd, msg_txt(183)); // Pet intimacy is already at maximum.
		return -1;
	}

	pet_set_intimate(pd, friendly);
	clif_send_petstatus(sd);
	clif_displaymessage(fd, msg_txt(182)); // Pet intimacy changed.
	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
int atcommand_pethungry(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	int hungry;
	struct pet_data *pd;
	nullpo_retr(-1, sd);

	if (!message || !*message || (hungry = atoi(message)) < 0) {
		clif_displaymessage(fd, "Please, enter a valid number (usage: @pethungry <0-100>).");
		return -1;
	}

	pd = sd->pd;
	if (!sd->status.pet_id || !pd) {
		clif_displaymessage(fd, msg_txt(184)); // Sorry, but you have no pet.
		return -1;
	}
	if (hungry < 0 || hungry > 100) {
		clif_displaymessage(fd, msg_txt(37)); // An invalid number was specified.
		return -1;
	}
	if (hungry == pd->pet.hungry) {
		clif_displaymessage(fd, msg_txt(186)); // Pet hunger is already at maximum.
		return -1;
	}

	pd->pet.hungry = hungry;
	clif_send_petstatus(sd);
	clif_displaymessage(fd, msg_txt(185)); // Pet hunger changed.

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
int atcommand_petrename(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	struct pet_data *pd;
	nullpo_retr(-1, sd);
	if (!sd->status.pet_id || !sd->pd) {
		clif_displaymessage(fd, msg_txt(184)); // Sorry, but you have no pet.
		return -1;
	}
	pd = sd->pd;
	if (!pd->pet.rename_flag) {
		clif_displaymessage(fd, msg_txt(188)); // You can already rename your pet.
		return -1;
	}

	pd->pet.rename_flag = 0;
	intif_save_petdata(sd->status.account_id, &pd->pet);
	clif_send_petstatus(sd);
	clif_displaymessage(fd, msg_txt(187)); // You can now rename your pet.

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
int atcommand_recall(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	struct map_session_data *pl_sd = NULL;

	nullpo_retr(-1, sd);

	if (!message || !*message) {
		clif_displaymessage(fd, "Please, enter a player name (usage: @recall <player name/id>).");
		return -1;
	}

	if((pl_sd=map_nick2sd((char *)message)) == NULL && (pl_sd=map_charid2sd(atoi(message))) == NULL)
	{
		clif_displaymessage(fd, msg_txt(3)); // Character not found.
		return -1;
	}

	if( pl_sd->vender_id || pl_sd->buyer_id ) { //check if player is vending
		clif_displaymessage(fd,"The player is in trade or @autotrade."); // You should be vending to use @Autotrade.
		return -1;
	}
		
	if (pl_sd == sd)
	{
		clif_displaymessage(fd, "You are already where you are...");
		return -1;
	}

	if ( pc_isGM(sd) < pc_isGM(pl_sd) )
	{
		clif_displaymessage(fd, msg_txt(81)); // Your GM level doesn't authorize you to preform this action on the specified player.
		return -1;
	}
	
	if (sd->bl.m >= 0 && map[sd->bl.m].flag.nowarpto && battle_config.any_warp_GM_min_level > pc_isGM(sd)) {
		clif_displaymessage(fd, "You are not authorised to warp somenone to your actual map.");
		return -1;
	}
	if (pl_sd->bl.m >= 0 && map[pl_sd->bl.m].flag.nowarp && battle_config.any_warp_GM_min_level > pc_isGM(sd)) {
		clif_displaymessage(fd, "You are not authorized to warp this player from its actual map.");
		return -1;
	}
	pc_setpos(pl_sd, sd->mapindex, sd->bl.x, sd->bl.y, 2);
	sprintf(atcmd_output, msg_txt(46), pl_sd->status.name); // %s recalled!
	clif_displaymessage(fd, atcmd_output);

	return 0;
}

/*==========================================
 * charblock command (usage: charblock <player_name>)
 * This command do a definitiv ban on a player
 *------------------------------------------*/
int atcommand_char_block(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	nullpo_retr(-1, sd);

	memset(atcmd_player_name, '\0', sizeof(atcmd_player_name));

	if (!message || !*message || sscanf(message, "%23[^\n]", atcmd_player_name) < 1) {
		clif_displaymessage(fd, "Please, enter a player name (usage: @charblock/@block <name>).");
		return -1;
	}

	chrif_char_ask_name(sd->status.account_id, atcmd_player_name, 1, 0, 0, 0, 0, 0, 0); // type: 1 - block
	clif_displaymessage(fd, msg_txt(88)); // Character name sent to char-server to ask it.

	return 0;
}

/*==========================================
 * charban command (usage: charban <time> <player_name>)
 * This command do a limited ban on a player
 * Time is done as follows:
 *   Adjustment value (-1, 1, +1, etc...)
 *   Modified element:
 *     a or y: year
 *     m:  month
 *     j or d: day
 *     h:  hour
 *     mn: minute
 *     s:  second
 * <example> @ban +1m-2mn1s-6y test_player
 *           this example adds 1 month and 1 second, and substracts 2 minutes and 6 years at the same time.
 *------------------------------------------*/
int atcommand_char_ban(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	char * modif_p;
	int year, month, day, hour, minute, second, value;
	nullpo_retr(-1, sd);

	memset(atcmd_output, '\0', sizeof(atcmd_output));
	memset(atcmd_player_name, '\0', sizeof(atcmd_player_name));

	if (!message || !*message || sscanf(message, "%s %23[^\n]", atcmd_output, atcmd_player_name) < 2) {
		clif_displaymessage(fd, "Please, enter ban time and a player name (usage: @charban/@ban/@banish/@charbanish <time> <name>).");
		return -1;
	}

	atcmd_output[sizeof(atcmd_output)-1] = '\0';

	modif_p = atcmd_output;
	year = month = day = hour = minute = second = 0;
	while (modif_p[0] != '\0') {
		value = atoi(modif_p);
		if (value == 0)
			modif_p++;
		else {
			if (modif_p[0] == '-' || modif_p[0] == '+')
				modif_p++;
			while (modif_p[0] >= '0' && modif_p[0] <= '9')
				modif_p++;
			if (modif_p[0] == 's') {
				second = value;
				modif_p++;
			} else if (modif_p[0] == 'n') {
				minute = value;
				modif_p++;
			} else if (modif_p[0] == 'm' && modif_p[1] == 'n') {
				minute = value;
				modif_p = modif_p + 2;
			} else if (modif_p[0] == 'h') {
				hour = value;
				modif_p++;
			} else if (modif_p[0] == 'd' || modif_p[0] == 'j') {
				day = value;
				modif_p++;
			} else if (modif_p[0] == 'm') {
				month = value;
				modif_p++;
			} else if (modif_p[0] == 'y' || modif_p[0] == 'a') {
				year = value;
				modif_p++;
			} else if (modif_p[0] != '\0') {
				modif_p++;
			}
		}
	}
	if (year == 0 && month == 0 && day == 0 && hour == 0 && minute == 0 && second == 0) {
		clif_displaymessage(fd, msg_txt(85)); // Invalid time for ban command.
		return -1;
	}

	chrif_char_ask_name(sd->status.account_id, atcmd_player_name, 2, year, month, day, hour, minute, second); // type: 2 - ban
	clif_displaymessage(fd, msg_txt(88)); // Character name sent to char-server to ask it.

	return 0;
}

/*==========================================
 * charunblock command (usage: charunblock <player_name>)
 *------------------------------------------*/
int atcommand_char_unblock(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	nullpo_retr(-1, sd);

	memset(atcmd_player_name, '\0', sizeof(atcmd_player_name));

	if (!message || !*message || sscanf(message, "%23[^\n]", atcmd_player_name) < 1) {
		clif_displaymessage(fd, "Please, enter a player name (usage: @charunblock <player_name>).");
		return -1;
	}

	// send answer to login server via char-server
	chrif_char_ask_name(sd->status.account_id, atcmd_player_name, 3, 0, 0, 0, 0, 0, 0); // type: 3 - unblock
	clif_displaymessage(fd, msg_txt(88)); // Character name sent to char-server to ask it.

	return 0;
}

/*==========================================
 * charunban command (usage: charunban <player_name>)
 *------------------------------------------*/
int atcommand_char_unban(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	nullpo_retr(-1, sd);

	memset(atcmd_player_name, '\0', sizeof(atcmd_player_name));

	if (!message || !*message || sscanf(message, "%23[^\n]", atcmd_player_name) < 1) {
		clif_displaymessage(fd, "Please, enter a player name (usage: @charunban <player_name>).");
		return -1;
	}

	// send answer to login server via char-server
	chrif_char_ask_name(sd->status.account_id, atcmd_player_name, 4, 0, 0, 0, 0, 0, 0); // type: 4 - unban
	clif_displaymessage(fd, msg_txt(88)); // Character name sent to char-server to ask it.

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
int atcommand_night(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	nullpo_retr(-1, sd);

	if (night_flag != 1) {
		map_night_timer(night_timer_tid, 0, 0, 1);
	} else {
		clif_displaymessage(fd, msg_txt(89)); // Night mode is already enabled.
		return -1;
	}

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
int atcommand_day(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	nullpo_retr(-1, sd);

	if (night_flag != 0) {
		map_day_timer(day_timer_tid, 0, 0, 1);
	} else {
		clif_displaymessage(fd, msg_txt(90)); // Day mode is already enabled.
		return -1;
	}

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
int atcommand_doom(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	struct map_session_data* pl_sd;
	struct s_mapiterator* iter;

	nullpo_retr(-1, sd);
	clif_specialeffect(&sd->bl,450,ALL_SAMEMAP);

	iter = mapit_getallusers();
	for( pl_sd = (TBL_PC*)mapit_first(iter); mapit_exists(iter); pl_sd = (TBL_PC*)mapit_next(iter) )
	{
		if (pl_sd->fd != fd && pc_isGM(sd) >= pc_isGM(pl_sd))
		{
			status_kill(&pl_sd->bl);
			clif_displaymessage(pl_sd->fd, msg_txt(61)); // The holy messenger has given judgement.
		}
	}
	mapit_free(iter);

	clif_displaymessage(fd, msg_txt(62)); // Judgement was made.

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
int atcommand_doommap(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	struct map_session_data* pl_sd;
	struct s_mapiterator* iter;

	nullpo_retr(-1, sd);
	clif_specialeffect(&sd->bl,450,ALL_CLIENT);

	iter = mapit_getallusers();
	for( pl_sd = (TBL_PC*)mapit_first(iter); mapit_exists(iter); pl_sd = (TBL_PC*)mapit_next(iter) )
	{
		if (pl_sd->fd != fd && sd->bl.m == pl_sd->bl.m && pc_isGM(sd) >= pc_isGM(pl_sd))
		{
			status_kill(&pl_sd->bl);
			clif_displaymessage(pl_sd->fd, msg_txt(61)); // The holy messenger has given judgement.
		}
	}
	mapit_free(iter);

	clif_displaymessage(fd, msg_txt(62)); // Judgement was made.

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
static void atcommand_raise_sub(struct map_session_data* sd)
{
	if (!status_isdead(&sd->bl))
		return;

	if(!status_revive(&sd->bl, 100, 100))
		return;
	clif_skill_nodamage(&sd->bl,&sd->bl,ALL_RESURRECTION,4,1);
	clif_displaymessage(sd->fd, msg_txt(63)); // Mercy has been shown.
}

/*==========================================
 *
 *------------------------------------------*/
int atcommand_raise(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	struct map_session_data* pl_sd;
	struct s_mapiterator* iter;
		
	nullpo_retr(-1, sd);

	iter = mapit_getallusers();
	for( pl_sd = (TBL_PC*)mapit_first(iter); mapit_exists(iter); pl_sd = (TBL_PC*)mapit_next(iter) )
		atcommand_raise_sub(pl_sd);
	mapit_free(iter);

	clif_displaymessage(fd, msg_txt(64)); // Mercy has been granted.

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
int atcommand_raisemap(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	struct map_session_data* pl_sd;
	struct s_mapiterator* iter;

	nullpo_retr(-1, sd);

	iter = mapit_getallusers();
	for( pl_sd = (TBL_PC*)mapit_first(iter); mapit_exists(iter); pl_sd = (TBL_PC*)mapit_next(iter) )
		if (sd->bl.m == pl_sd->bl.m)
			atcommand_raise_sub(pl_sd);
	mapit_free(iter);

	clif_displaymessage(fd, msg_txt(64)); // Mercy has been granted.

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
int atcommand_kick(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	struct map_session_data *pl_sd;
	nullpo_retr(-1, sd);

	memset(atcmd_player_name, '\0', sizeof(atcmd_player_name));

	if (!message || !*message) {
		clif_displaymessage(fd, "Please, enter a player name (usage: @kick <player name/id>).");
		return -1;
	}

	if((pl_sd=map_nick2sd((char *)message)) == NULL && (pl_sd=map_charid2sd(atoi(message))) == NULL)
	{
		clif_displaymessage(fd, msg_txt(3)); // Character not found.
		return -1;
	}

	if ( pc_isGM(sd) < pc_isGM(pl_sd) )
	{
		clif_displaymessage(fd, msg_txt(81)); // Your GM level don't authorise you to do this action on this player.
		return -1;
	}
	
	clif_GM_kick(sd, pl_sd);
	
	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
int atcommand_kickall(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	struct map_session_data* pl_sd;
	struct s_mapiterator* iter;
	nullpo_retr(-1, sd);

	iter = mapit_getallusers();
	for( pl_sd = (TBL_PC*)mapit_first(iter); mapit_exists(iter); pl_sd = (TBL_PC*)mapit_next(iter) )
	{
		if (pc_isGM(sd) >= pc_isGM(pl_sd)) { // you can kick only lower or same gm level
			if (sd->status.account_id != pl_sd->status.account_id)
				clif_GM_kick(NULL, pl_sd);
		}
	}
	mapit_free(iter);

	clif_displaymessage(fd, msg_txt(195)); // All players have been kicked!

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
int atcommand_allskill(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	nullpo_retr(-1, sd);
	pc_allskillup(sd); // all skills
	sd->status.skill_point = 0; // 0 skill points
	clif_updatestatus(sd, SP_SKILLPOINT); // update
	clif_displaymessage(fd, msg_txt(76)); // All skills have been added to your skill tree.

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
int atcommand_questskill(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	int skill_id;
	nullpo_retr(-1, sd);

	if (!message || !*message || (skill_id = atoi(message)) < 0) {
		clif_displaymessage(fd, "Please, enter a quest skill number (usage: @questskill <#:0+>).");
		return -1;
	}
	if (skill_id < 0 && skill_id >= MAX_SKILL_DB) {
		clif_displaymessage(fd, msg_txt(198)); // This skill number doesn't exist.
		return -1;
	}
	if (!(skill_get_inf2(skill_id) & INF2_QUEST_SKILL)) {
		clif_displaymessage(fd, msg_txt(197)); // This skill number doesn't exist or isn't a quest skill.
		return -1;
	}
	if (pc_checkskill(sd, skill_id) > 0) {
		clif_displaymessage(fd, msg_txt(196)); // You already have this quest skill.
		return -1;
	}

	pc_skill(sd, skill_id, 1, 0);
	clif_displaymessage(fd, msg_txt(70)); // You have learned the skill.

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
int atcommand_lostskill(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	int skill_id;
	nullpo_retr(-1, sd);

	if (!message || !*message || (skill_id = atoi(message)) < 0) {
		clif_displaymessage(fd, "Please, enter a quest skill number (usage: @lostskill <#:0+>).");
		return -1;
	}
	if (skill_id < 0 && skill_id >= MAX_SKILL) {
		clif_displaymessage(fd, msg_txt(198)); // This skill number doesn't exist.
		return -1;
	}
	if (!(skill_get_inf2(skill_id) & INF2_QUEST_SKILL)) {
		clif_displaymessage(fd, msg_txt(197)); // This skill number doesn't exist or isn't a quest skill.
		return -1;
	}
	if (pc_checkskill(sd, skill_id) == 0) {
		clif_displaymessage(fd, msg_txt(201)); // You don't have this quest skill.
		return -1;
	}

	sd->status.skill[skill_id].lv = 0;
	sd->status.skill[skill_id].flag = 0;
	clif_deleteskill(sd,skill_id);
	clif_displaymessage(fd, msg_txt(71)); // You have forgotten the skill.

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
int atcommand_spiritball(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	int max_spiritballs = min(ARRAYLENGTH(sd->spirit_timer), 0x7FFF);
	int number;
	nullpo_retr(-1, sd);

	if( !message || !*message || (number = atoi(message)) < 0 || number > max_spiritballs )
	{
		char msg[CHAT_SIZE_MAX];
		safesnprintf(msg, sizeof(msg), "Usage: @spiritball <number: 0-%d>", max_spiritballs);
		clif_displaymessage(fd, msg);
		return -1;
	}

	if( sd->spiritball > 0 )
		pc_delspiritball(sd, sd->spiritball, 1);
	sd->spiritball = number;
	clif_spiritball(sd);
	// no message, player can look the difference

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
int atcommand_party(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	char party[NAME_LENGTH];
	nullpo_retr(-1, sd);

	memset(party, '\0', sizeof(party));

	if (!message || !*message || sscanf(message, "%23[^\n]", party) < 1) {
		clif_displaymessage(fd, "Please, enter a party name (usage: @party <party_name>).");
		return -1;
	}

	party_create(sd, party, 0, 0);

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
int atcommand_guild(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	char guild[NAME_LENGTH];
	int prev;
	nullpo_retr(-1, sd);

	memset(guild, '\0', sizeof(guild));

	if (!message || !*message || sscanf(message, "%23[^\n]", guild) < 1) {
		clif_displaymessage(fd, "Please, enter a guild name (usage: @guild <guild_name>).");
		return -1;
	}

	prev = battle_config.guild_emperium_check;
	battle_config.guild_emperium_check = 0;
	guild_create(sd, guild);
	battle_config.guild_emperium_check = prev;

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
int atcommand_agitstart(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	int woe_set;

	nullpo_retr(-1, sd);
	if( agit_flag != 0 )
	{
		clif_displaymessage(fd, msg_txt(73)); // War of Emperium is currently in progress.
		return -1;
	}

	if( (woe_set = atoi(message)) <= 0 )
		return -1;

	agit_flag = woe_set;
	guild_agit_start();

	clif_displaymessage(fd, msg_txt(72)); // War of Emperium has been initiated.
	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
int atcommand_agitend(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	nullpo_retr(-1, sd);
	if (agit_flag == 0) {
		clif_displaymessage(fd, msg_txt(75)); // War of Emperium is currently not in progress.
		return -1;
	}

	guild_agit_end();
	agit_flag = 0;
	clif_displaymessage(fd, msg_txt(74)); // War of Emperium has been ended.

	return 0;
}

/*==========================================
 * @mapexit - shuts down the map server
 *------------------------------------------*/
int atcommand_mapexit(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	struct map_session_data* pl_sd;
	struct s_mapiterator* iter;

	nullpo_retr(-1, sd);

	iter = mapit_getallusers();
	for( pl_sd = (TBL_PC*)mapit_first(iter); mapit_exists(iter); pl_sd = (TBL_PC*)mapit_next(iter) )
		if (sd->status.account_id != pl_sd->status.account_id)
			clif_GM_kick(NULL, pl_sd);
	mapit_free(iter);

	clif_GM_kick(NULL, sd);
	
	flush_fifos();

	runflag = 0;

	return 0;
}

/*==========================================
 * idsearch <part_of_name>: revrited by [Yor]
 *------------------------------------------*/
int atcommand_idsearch(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	char item_name[100];
	unsigned int i, match;
	struct item_data *item_array[MAX_SEARCH];
	nullpo_retr(-1, sd);

	memset(item_name, '\0', sizeof(item_name));
	memset(atcmd_output, '\0', sizeof(atcmd_output));

	if (!message || !*message || sscanf(message, "%99s", item_name) < 0) {
		clif_displaymessage(fd, "Please, enter a part of item name (usage: @idsearch <part_of_item_name>).");
		return -1;
	}

	sprintf(atcmd_output, msg_txt(77), item_name); // The reference result of '%s' (name: id):
	clif_displaymessage(fd, atcmd_output);
	match = itemdb_searchname_array(item_array, MAX_SEARCH, item_name);
	if (match > MAX_SEARCH) {
		sprintf(atcmd_output, msg_txt(269), MAX_SEARCH, match);
		clif_displaymessage(fd, atcmd_output);
		match = MAX_SEARCH;
	}
	for(i = 0; i < match; i++) {
		sprintf(atcmd_output, msg_txt(78), item_array[i]->jname, item_array[i]->nameid); // %s: %d
		clif_displaymessage(fd, atcmd_output);
	}
	sprintf(atcmd_output, msg_txt(79), match); // It is %d affair above.
	clif_displaymessage(fd, atcmd_output);

	return 0;
}

/*==========================================
 * Recall All Characters Online To Your Location
 *------------------------------------------*/
int atcommand_recallall(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	struct map_session_data* pl_sd;
	struct s_mapiterator* iter;
	int count;
	nullpo_retr(-1, sd);

	memset(atcmd_output, '\0', sizeof(atcmd_output));

	if (sd->bl.m >= 0 && map[sd->bl.m].flag.nowarpto && battle_config.any_warp_GM_min_level > pc_isGM(sd)) {
		clif_displaymessage(fd, "You are not authorised to warp somenone to your actual map.");
		return -1;
	}

	count = 0;
	iter = mapit_getallusers();
	for( pl_sd = (TBL_PC*)mapit_first(iter); mapit_exists(iter); pl_sd = (TBL_PC*)mapit_next(iter) )
	{
		if (sd->status.account_id != pl_sd->status.account_id && pc_isGM(sd) >= pc_isGM(pl_sd))
		{
			if (pl_sd->bl.m >= 0 && map[pl_sd->bl.m].flag.nowarp && battle_config.any_warp_GM_min_level > pc_isGM(sd))
				count++;
			else {
				if (pc_isdead(pl_sd)) { //Wake them up
					pc_setstand(pl_sd);
					pc_setrestartvalue(pl_sd,1);
				}
				pc_setpos(pl_sd, sd->mapindex, sd->bl.x, sd->bl.y, 2);
			}
		}
	}
	mapit_free(iter);

	clif_displaymessage(fd, msg_txt(92)); // All characters recalled!
	if (count) {
		sprintf(atcmd_output, "Because you are not authorised to warp from some maps, %d player(s) have not been recalled.", count);
		clif_displaymessage(fd, atcmd_output);
	}

	return 0;
}

/*==========================================
 * Recall online characters of a guild to your location
 *------------------------------------------*/
int atcommand_guildrecall(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	struct map_session_data* pl_sd;
	struct s_mapiterator* iter;
	int count;
	char guild_name[NAME_LENGTH];
	struct guild *g;
	nullpo_retr(-1, sd);

	memset(guild_name, '\0', sizeof(guild_name));
	memset(atcmd_output, '\0', sizeof(atcmd_output));

	if (!message || !*message || sscanf(message, "%23[^\n]", guild_name) < 1) {
		clif_displaymessage(fd, "Please, enter a guild name/id (usage: @guildrecall <guild_name/id>).");
		return -1;
	}

	if (sd->bl.m >= 0 && map[sd->bl.m].flag.nowarpto && battle_config.any_warp_GM_min_level > pc_isGM(sd)) {
		clif_displaymessage(fd, "You are not authorised to warp somenone to your actual map.");
		return -1;
	}

	if ((g = guild_searchname(guild_name)) == NULL && // name first to avoid error when name begin with a number
	    (g = guild_search(atoi(message))) == NULL)
	{
		clif_displaymessage(fd, msg_txt(94)); // Incorrect name/ID, or no one from the guild is online.
		return -1;
	}

	count = 0;

	iter = mapit_getallusers();
	for( pl_sd = (TBL_PC*)mapit_first(iter); mapit_exists(iter); pl_sd = (TBL_PC*)mapit_next(iter) )
	{
		if (sd->status.account_id != pl_sd->status.account_id && pl_sd->status.guild_id == g->guild_id)
		{
			if (pc_isGM(pl_sd) > pc_isGM(sd))
				continue; //Skip GMs greater than you.
			if (pl_sd->bl.m >= 0 && map[pl_sd->bl.m].flag.nowarp && battle_config.any_warp_GM_min_level > pc_isGM(sd))
				count++;
			else
				pc_setpos(pl_sd, sd->mapindex, sd->bl.x, sd->bl.y, 2);
		}
	}
	mapit_free(iter);

	sprintf(atcmd_output, msg_txt(93), g->name); // All online characters of the %s guild have been recalled to your position.
	clif_displaymessage(fd, atcmd_output);
	if (count) {
		sprintf(atcmd_output, "Because you are not authorised to warp from some maps, %d player(s) have not been recalled.", count);
		clif_displaymessage(fd, atcmd_output);
	}

	return 0;
}

/*==========================================
 * Recall online characters of a party to your location
 *------------------------------------------*/
int atcommand_partyrecall(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	struct map_session_data* pl_sd;
	struct s_mapiterator* iter;
	char party_name[NAME_LENGTH];
	struct party_data *p;
	int count;
	nullpo_retr(-1, sd);

	memset(party_name, '\0', sizeof(party_name));
	memset(atcmd_output, '\0', sizeof(atcmd_output));

	if (!message || !*message || sscanf(message, "%23[^\n]", party_name) < 1) {
		clif_displaymessage(fd, "Please, enter a party name/id (usage: @partyrecall <party_name/id>).");
		return -1;
	}

	if (sd->bl.m >= 0 && map[sd->bl.m].flag.nowarpto && battle_config.any_warp_GM_min_level > pc_isGM(sd)) {
		clif_displaymessage(fd, "You are not authorised to warp somenone to your actual map.");
		return -1;
	}

	if ((p = party_searchname(party_name)) == NULL && // name first to avoid error when name begin with a number
	    (p = party_search(atoi(message))) == NULL)
	{
		clif_displaymessage(fd, msg_txt(96)); // Incorrect name or ID, or no one from the party is online.
		return -1;
	}

	count = 0;

	iter = mapit_getallusers();
	for( pl_sd = (TBL_PC*)mapit_first(iter); mapit_exists(iter); pl_sd = (TBL_PC*)mapit_next(iter) )
	{
		if (sd->status.account_id != pl_sd->status.account_id && pl_sd->status.party_id == p->party.party_id)
		{
			if (pc_isGM(pl_sd) > pc_isGM(sd))
				continue; //Skip GMs greater than you.
			if (pl_sd->bl.m >= 0 && map[pl_sd->bl.m].flag.nowarp && battle_config.any_warp_GM_min_level > pc_isGM(sd))
				count++;
			else
				pc_setpos(pl_sd, sd->mapindex, sd->bl.x, sd->bl.y, 2);
		}
	}
	mapit_free(iter);

	sprintf(atcmd_output, msg_txt(95), p->party.name); // All online characters of the %s party have been recalled to your position.
	clif_displaymessage(fd, atcmd_output);
	if (count) {
		sprintf(atcmd_output, "Because you are not authorised to warp from some maps, %d player(s) have not been recalled.", count);
		clif_displaymessage(fd, atcmd_output);
	}

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
int atcommand_reloadhashdb(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	nullpo_retr(-1, sd);
	hashblock_reload();
	clif_displaymessage(fd, "GRF hash database reloaded.");
	
	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
int atcommand_reloaditemdb(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	nullpo_retr(-1, sd);
	itemdb_reload();
	clif_displaymessage(fd, msg_txt(97)); // Item database has been reloaded.

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
int atcommand_reloadmobdb(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	nullpo_retr(-1, sd);
	mob_reload();
	read_petdb();
	merc_reload();
	reload_elementaldb();
	clif_displaymessage(fd, msg_txt(98)); // Monster database has been reloaded.

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
int atcommand_reloadskilldb(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	nullpo_retr(-1, sd);
	skill_reload();
	merc_skill_reload();
	reload_elemental_skilldb();
	clif_displaymessage(fd, msg_txt(99)); // Skill database has been reloaded.

	return 0;
}

/*==========================================
 * @reloadatcommand - reloads atcommand_athena.conf
 *------------------------------------------*/
int atcommand_reloadatcommand(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	atcommand_config_read(ATCOMMAND_CONF_FILENAME);
	clif_displaymessage(fd, msg_txt(254));
	return 0;
}
/*==========================================
 * @reloadbattleconf - reloads battle_athena.conf
 *------------------------------------------*/
int atcommand_reloadbattleconf(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	struct Battle_Config prev_config;
	memcpy(&prev_config, &battle_config, sizeof(prev_config));

	battle_config_read(BATTLE_CONF_FILENAME);
	// It can cause serius problems if we change this setting while server is UP.
	battle_config.renewal_system_enable = prev_config.renewal_system_enable;
	battle_config.anti_mayapurple_hack = prev_config.anti_mayapurple_hack;

	if( prev_config.item_rate_mvp          != battle_config.item_rate_mvp
	||  prev_config.item_rate_common       != battle_config.item_rate_common
	||  prev_config.item_rate_common_boss  != battle_config.item_rate_common_boss
	||  prev_config.item_rate_card         != battle_config.item_rate_card
	||  prev_config.item_rate_card_boss    != battle_config.item_rate_card_boss
	||  prev_config.item_rate_equip        != battle_config.item_rate_equip
	||  prev_config.item_rate_equip_boss   != battle_config.item_rate_equip_boss
	||  prev_config.item_rate_heal         != battle_config.item_rate_heal
	||  prev_config.item_rate_heal_boss    != battle_config.item_rate_heal_boss
	||  prev_config.item_rate_use          != battle_config.item_rate_use
	||  prev_config.item_rate_use_boss     != battle_config.item_rate_use_boss
	||  prev_config.item_rate_treasure     != battle_config.item_rate_treasure
	||  prev_config.item_rate_adddrop      != battle_config.item_rate_adddrop
	||  prev_config.logarithmic_drops      != battle_config.logarithmic_drops
	||  prev_config.item_drop_common_min   != battle_config.item_drop_common_min
	||  prev_config.item_drop_common_max   != battle_config.item_drop_common_max
	||  prev_config.item_drop_card_min     != battle_config.item_drop_card_min
	||  prev_config.item_drop_card_max     != battle_config.item_drop_card_max
	||  prev_config.item_drop_equip_min    != battle_config.item_drop_equip_min
	||  prev_config.item_drop_equip_max    != battle_config.item_drop_equip_max
	||  prev_config.item_drop_mvp_min      != battle_config.item_drop_mvp_min
	||  prev_config.item_drop_mvp_max      != battle_config.item_drop_mvp_max
	||  prev_config.item_drop_heal_min     != battle_config.item_drop_heal_min
	||  prev_config.item_drop_heal_max     != battle_config.item_drop_heal_max
	||  prev_config.item_drop_use_min      != battle_config.item_drop_use_min
	||  prev_config.item_drop_use_max      != battle_config.item_drop_use_max
	||  prev_config.item_drop_treasure_min != battle_config.item_drop_treasure_min
	||  prev_config.item_drop_treasure_max != battle_config.item_drop_treasure_max
	||  prev_config.base_exp_rate          != battle_config.base_exp_rate
	||  prev_config.job_exp_rate           != battle_config.job_exp_rate
	)
  	{	// Exp or Drop rates changed.
		mob_reload(); //Needed as well so rate changes take effect.
#ifndef TXT_ONLY
		chrif_ragsrvinfo(battle_config.base_exp_rate, battle_config.job_exp_rate, battle_config.item_rate_common);
#endif
	}
	clif_displaymessage(fd, msg_txt(255));
	return 0;
}
/*==========================================
 * @reloadstatusdb - reloads job_db1.txt job_db2.txt job_db2-2.txt refine_db.txt size_fix.txt
 *------------------------------------------*/
int atcommand_reloadstatusdb(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	status_readdb();
	clif_displaymessage(fd, msg_txt(256));
	return 0;
}
/*==========================================
 * @reloadpcdb - reloads exp.txt skill_tree.txt attr_fix.txt statpoint.txt
 *------------------------------------------*/
int atcommand_reloadpcdb(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	pc_readdb();
	clif_displaymessage(fd, msg_txt(257));
	return 0;
}

/*==========================================
 * @reloadmotd - reloads motd.txt
 *------------------------------------------*/
int atcommand_reloadmotd(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	pc_read_motd();
	clif_displaymessage(fd, msg_txt(268));
	return 0;
}

/*==========================================
 * @reloadscript - reloads all scripts (npcs, warps, mob spawns, ...)
 *------------------------------------------*/
int atcommand_reloadscript(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	nullpo_retr(-1, sd);
	//atcommand_broadcast( fd, sd, "@broadcast", "eAthena Server is Rehashing..." );
	//atcommand_broadcast( fd, sd, "@broadcast", "You will feel a bit of lag at this point !" );
	//atcommand_broadcast( fd, sd, "@broadcast", "Reloading NPCs..." );

	flush_fifos();
	script_reload();
	npc_reload();

	clif_displaymessage(fd, msg_txt(100)); // Scripts have been reloaded.

	return 0;
}

/*==========================================
 * @mapinfo [0-3] <map name> by MC_Cameri
 * => Shows information about the map [map name]
 * 0 = no additional information
 * 1 = Show users in that map and their location
 * 2 = Shows NPCs in that map
 * 3 = Shows the shops/chats in that map (not implemented)
 *------------------------------------------*/
int atcommand_mapinfo(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	struct map_session_data* pl_sd;
	struct s_mapiterator* iter;
	struct npc_data *nd = NULL;
	struct chat_data *cd = NULL;
	char direction[12];
	int i, m_id, chat_num, list = 0;
	unsigned short m_index;
	char mapname[24];

	nullpo_retr(-1, sd);

	memset(atcmd_output, '\0', sizeof(atcmd_output));
	memset(mapname, '\0', sizeof(mapname));
	memset(direction, '\0', sizeof(direction));

	sscanf(message, "%d %23[^\n]", &list, mapname);

	if (list < 0 || list > 3) {
		clif_displaymessage(fd, "Please, enter at least a valid list number (usage: @mapinfo <0-3> [map]).");
		return -1;
	}

	if (mapname[0] == '\0') {
		safestrncpy(mapname, mapindex_id2name(sd->mapindex), MAP_NAME_LENGTH);
		m_id =  map_mapindex2mapid(sd->mapindex);
	} else {
		m_id = map_mapname2mapid(mapname);
	}

	if (m_id < 0) {
		clif_displaymessage(fd, msg_txt(1)); // Map not found.
		return -1;
	}
	m_index = mapindex_name2id(mapname); //This one shouldn't fail since the previous seek did not.
	
	clif_displaymessage(fd, "------ Map Info ------");

	// count chats (for initial message)
	chat_num = 0;
	iter = mapit_getallusers();
	for( pl_sd = (TBL_PC*)mapit_first(iter); mapit_exists(iter); pl_sd = (TBL_PC*)mapit_next(iter) )
		if( (cd = (struct chat_data*)map_id2bl(pl_sd->chatID)) != NULL && pl_sd->mapindex == m_index && cd->usersd[0] == pl_sd )
			chat_num++;
	mapit_free(iter);

	sprintf(atcmd_output, "Map Name: %s | Players In Map: %d | NPCs In Map: %d | Chats In Map: %d", mapname, map[m_id].users, map[m_id].npc_num, chat_num);
	clif_displaymessage(fd, atcmd_output);
	clif_displaymessage(fd, "------ Map Flags ------");
	if (map[m_id].flag.town)
		clif_displaymessage(fd, "Town Map");

	if (battle_config.autotrade_mapflag == map[m_id].flag.autotrade)
		clif_displaymessage(fd, "Autotrade Enabled");
	else
		clif_displaymessage(fd, "Autotrade Disabled");
	
	if (map[m_id].flag.battleground)
		clif_displaymessage(fd, "Battlegrounds ON");
		
	strcpy(atcmd_output,"PvP Flags: ");
	if (map[m_id].flag.pvp)
		strcat(atcmd_output, "Pvp ON | ");
	if (map[m_id].flag.pvp_noguild)
		strcat(atcmd_output, "NoGuild | ");
	if (map[m_id].flag.pvp_noparty)
		strcat(atcmd_output, "NoParty | ");
	if (map[m_id].flag.pvp_nightmaredrop)
		strcat(atcmd_output, "NightmareDrop | ");
	if (map[m_id].flag.pvp_nocalcrank)
		strcat(atcmd_output, "NoCalcRank | ");
	clif_displaymessage(fd, atcmd_output);

	if (map[m_id].flag.pvp_event)
	{
		sprintf(atcmd_output,"PVP Event Flag: x1 %d | x2 %d | y1 %d | y2 %d", map[m_id].pvpe_x1, map[m_id].pvpe_x2, map[m_id].pvpe_y1, map[m_id].pvpe_y2);
		clif_displaymessage(fd, atcmd_output);
	}

	strcpy(atcmd_output,"GvG Flags: ");
	if (map[m_id].flag.gvg)
		strcat(atcmd_output, "GvG ON | ");
	if (map[m_id].flag.gvg_dungeon)
		strcat(atcmd_output, "GvG Dungeon | ");
	if (map[m_id].flag.gvg_castle)
		strcat(atcmd_output, "GvG Castle | ");
	if (map[m_id].flag.gvg_noparty)
		strcat(atcmd_output, "NoParty | ");
	clif_displaymessage(fd, atcmd_output);

	strcpy(atcmd_output,"Teleport Flags: ");
	if (map[m_id].flag.noteleport)
		strcat(atcmd_output, "NoTeleport | ");
	if (map[m_id].flag.monster_noteleport)
		strcat(atcmd_output, "Monster NoTeleport | ");
	if (map[m_id].flag.nowarp)
		strcat(atcmd_output, "NoWarp | ");
	if (map[m_id].flag.nowarpto)
		strcat(atcmd_output, "NoWarpTo | ");
	if (map[m_id].flag.noreturn)
		strcat(atcmd_output, "NoReturn | ");
	if (map[m_id].flag.nogo)
		strcat(atcmd_output, "NoGo | ");
	if (map[m_id].flag.nomemo)
		strcat(atcmd_output, "NoMemo | ");
	if (map[m_id].flag.blocked)
		strcat(atcmd_output, "Blocked | ");
	clif_displaymessage(fd, atcmd_output);

	sprintf(atcmd_output, "No Exp Penalty: %s | No Zeny Penalty: %s", (map[m_id].flag.noexppenalty) ? "On" : "Off", (map[m_id].flag.nozenypenalty) ? "On" : "Off");
	clif_displaymessage(fd, atcmd_output);

	if (map[m_id].flag.nosave) {
		if (!map[m_id].save.map)
			sprintf(atcmd_output, "No Save (Return to last Save Point)");
		else if (map[m_id].save.x == -1 || map[m_id].save.y == -1 )
			sprintf(atcmd_output, "No Save, Save Point: %s,Random",mapindex_id2name(map[m_id].save.map));
		else
			sprintf(atcmd_output, "No Save, Save Point: %s,%d,%d",
				mapindex_id2name(map[m_id].save.map),map[m_id].save.x,map[m_id].save.y);
		clif_displaymessage(fd, atcmd_output);
	}

	strcpy(atcmd_output,"Weather Flags: ");
	if (map[m_id].flag.snow)
		strcat(atcmd_output, "Snow | ");
	if (map[m_id].flag.fog)
		strcat(atcmd_output, "Fog | ");
	if (map[m_id].flag.sakura)
		strcat(atcmd_output, "Sakura | ");
	if (map[m_id].flag.clouds)
		strcat(atcmd_output, "Clouds | ");
	if (map[m_id].flag.clouds2)
		strcat(atcmd_output, "Clouds2 | ");
	if (map[m_id].flag.fireworks)
		strcat(atcmd_output, "Fireworks | ");
	if (map[m_id].flag.leaves)
		strcat(atcmd_output, "Leaves | ");
	if (map[m_id].flag.rain)
		strcat(atcmd_output, "Rain | ");
	if (map[m_id].flag.indoors)
		strcat(atcmd_output, "Indoors | ");
	if (map[m_id].flag.nightenabled)
		strcat(atcmd_output, "Displays Night | ");
	clif_displaymessage(fd, atcmd_output);

	strcpy(atcmd_output,"Other Flags: ");
	if (map[m_id].flag.nobranch)
		strcat(atcmd_output, "NoBranch | ");
	if (map[m_id].flag.notrade)
		strcat(atcmd_output, "NoTrade | ");
	if (map[m_id].flag.novending)
		strcat(atcmd_output, "NoVending | ");
	if (map[m_id].flag.nodrop)
		strcat(atcmd_output, "NoDrop | ");
	if (map[m_id].flag.noskill)
		strcat(atcmd_output, "NoSkill | ");
	if (map[m_id].flag.noicewall)
		strcat(atcmd_output, "NoIcewall | ");
	if (map[m_id].flag.allowks)
		strcat(atcmd_output, "AllowKS | ");
	clif_displaymessage(fd, atcmd_output);

	strcpy(atcmd_output,"Other Flags: ");
	if (map[m_id].nocommand)
		strcat(atcmd_output, "NoCommand | ");
	if (map[m_id].flag.nobaseexp)
		strcat(atcmd_output, "NoBaseEXP | ");
	if (map[m_id].flag.nojobexp)
		strcat(atcmd_output, "NoJobEXP | ");
	if (map[m_id].flag.nomobloot)
		strcat(atcmd_output, "NoMobLoot | ");
	if (map[m_id].flag.nomvploot)
		strcat(atcmd_output, "NoMVPLoot | ");
	if (map[m_id].flag.partylock)
		strcat(atcmd_output, "PartyLock | ");
	if (map[m_id].flag.guildlock)
		strcat(atcmd_output, "GuildLock | ");
	clif_displaymessage(fd, atcmd_output);

	switch (list) {
	case 0:
		// Do nothing. It's list 0, no additional display.
		break;
	case 1:
		clif_displaymessage(fd, "----- Players in Map -----");
		iter = mapit_getallusers();
		for( pl_sd = (TBL_PC*)mapit_first(iter); mapit_exists(iter); pl_sd = (TBL_PC*)mapit_next(iter) )
		{
			if (pl_sd->mapindex == m_index) {
				sprintf(atcmd_output, "Player '%s' (session #%d) | Location: %d,%d",
				        pl_sd->status.name, pl_sd->fd, pl_sd->bl.x, pl_sd->bl.y);
				clif_displaymessage(fd, atcmd_output);
			}
		}
		mapit_free(iter);
		break;
	case 2:
		clif_displaymessage(fd, "----- NPCs in Map -----");
		for (i = 0; i < map[m_id].npc_num;)
		{
			nd = map[m_id].npc[i];
			switch(nd->ud.dir) {
			case 0:  strcpy(direction, "North"); break;
			case 1:  strcpy(direction, "North West"); break;
			case 2:  strcpy(direction, "West"); break;
			case 3:  strcpy(direction, "South West"); break;
			case 4:  strcpy(direction, "South"); break;
			case 5:  strcpy(direction, "South East"); break;
			case 6:  strcpy(direction, "East"); break;
			case 7:  strcpy(direction, "North East"); break;
			case 9:  strcpy(direction, "North"); break;
			default: strcpy(direction, "Unknown"); break;
			}
			sprintf(atcmd_output, "NPC %d: %s | Direction: %s | Sprite: %d | Location: %d %d",
			        ++i, nd->name, direction, nd->class_, nd->bl.x, nd->bl.y);
			clif_displaymessage(fd, atcmd_output);
		}
		break;
	case 3:
		clif_displaymessage(fd, "----- Chats in Map -----");
		iter = mapit_getallusers();
		for( pl_sd = (TBL_PC*)mapit_first(iter); mapit_exists(iter); pl_sd = (TBL_PC*)mapit_next(iter) )
		{
			if ((cd = (struct chat_data*)map_id2bl(pl_sd->chatID)) != NULL &&
			    pl_sd->mapindex == m_index &&
			    cd->usersd[0] == pl_sd)
			{
				sprintf(atcmd_output, "Chat: %s | Player: %s | Location: %d %d",
				        cd->title, pl_sd->status.name, cd->bl.x, cd->bl.y);
				clif_displaymessage(fd, atcmd_output);
				sprintf(atcmd_output, "   Users: %d/%d | Password: %s | Public: %s",
				        cd->users, cd->limit, cd->pass, (cd->pub) ? "Yes" : "No");
				clif_displaymessage(fd, atcmd_output);
			}
		}
		mapit_free(iter);
		break;
	default: // normally impossible to arrive here
		clif_displaymessage(fd, "Please, enter at least a valid list number (usage: @mapinfo <0-3> [map]).");
		return -1;
		break;
	}

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
int atcommand_mount(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	int msg[4] = { 0, 0, 0, 0 }, option = 0, skillnum = 0, val, riding_flag = 0;
	nullpo_retr(-1, sd);

	if( !message || !*message || sscanf(message, "%d", &val) < 1 || val < 1 || val > 5 )
		val = 0; // Default color to riding dragon

	if( (sd->class_&MAPID_UPPERMASK) == MAPID_KNIGHT || (sd->class_&MAPID_UPPERMASK) == MAPID_CRUSADER )
	{
		if( sd->class_&JOBL_THIRD )
		{
			if( (sd->class_&MAPID_UPPERMASK) == MAPID_KNIGHT )
			{ // Rune Knight
				if( pc_isriding(sd,OPTION_RIDING_DRAGON) )
					riding_flag = 1;
				msg[0] = 700; msg[1] = 702; msg[2] = 701; msg[3] = 703;
				option = pc_isriding(sd,OPTION_RIDING_DRAGON) ? OPTION_RIDING_DRAGON :
					(val == 2) ? OPTION_BLACK_DRAGON :
					(val == 3) ? OPTION_WHITE_DRAGON :
					(val == 4) ? OPTION_BLUE_DRAGON :
					(val == 5) ? OPTION_RED_DRAGON :
					OPTION_GREEN_DRAGON;
				skillnum = RK_DRAGONTRAINING;
			}
			else
			{ // Royal Guard
				if( pc_isriding(sd,OPTION_RIDING) )
					riding_flag = 1;
				msg[0] = 714; msg[1] = 716; msg[2] = 715; msg[3] = 717;
				option = OPTION_RIDING;
				skillnum = KN_RIDING;
			}
		}
		else
		{ // Lord Knight - Knight - Paladin - Crusader
			if( pc_isriding(sd,OPTION_RIDING) )
				riding_flag = 1;
			msg[0] = 102; msg[1] = 214; msg[2] = 213; msg[3] = 212;
			option = OPTION_RIDING;
			skillnum = KN_RIDING;
		}
	}
	else if( sd->class_&JOBL_THIRD )
	{
		if( (sd->class_&MAPID_UPPERMASK) == MAPID_HUNTER )
		{ // Ranger
			if( pc_iswarg(sd) )
				pc_setoption(sd,sd->sc.option&~OPTION_WUG);
			if( pc_isriding(sd,OPTION_RIDING_WUG) )
				riding_flag = 1;
			msg[0] = 704; msg[1] = 706; msg[2] = 705; msg[3] = 707;
			option = OPTION_RIDING_WUG;
			skillnum = RA_WUGRIDER;
		}
		else if( (sd->class_&MAPID_UPPERMASK) == MAPID_BLACKSMITH )
		{
			if( pc_isriding(sd, OPTION_MADO) )
				riding_flag = 1;
			msg[0] = 710; msg[1] = 712; msg[2] = 711; msg[3] = 713;
			option = OPTION_MADO;
		}
	}

	if( !option )
	{
		clif_displaymessage(fd, "You can not mount with your current job.");
		return -1;
	}

	if( skillnum && !pc_checkskill(sd,skillnum) )
	{ // You haven't required skill to mount
		clif_displaymessage(fd, msg_txt(msg[2])); // You can not mount with your current job.
		return -1;
	}
	if( sd->disguise )
	{ // Disguised
		clif_displaymessage(fd, msg_txt(msg[3])); // Cannot mount while in disguise.
		return -1;
	}
	if( riding_flag )
	{ //Dismount
		pc_setoption(sd, sd->sc.option & ~option);
		if( option == OPTION_RIDING_WUG )
			pc_setoption(sd, sd->sc.option&OPTION_WUG);
		clif_displaymessage(fd, msg_txt(msg[1])); // You have released your mount.
	}
	else
	{ // Mount
		pc_setoption(sd, sd->sc.option | option);
		clif_displaymessage(fd, msg_txt(msg[0])); // You have mounted.
	}

	return 0;
}

/*==========================================
 *Spy Commands by Syrus22
 *------------------------------------------*/
int atcommand_guildspy(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	char guild_name[NAME_LENGTH];
	struct guild *g;
	nullpo_retr(-1, sd);

	memset(guild_name, '\0', sizeof(guild_name));
	memset(atcmd_output, '\0', sizeof(atcmd_output));

	if (!enable_spy)
	{
		clif_displaymessage(fd, "The mapserver has spy command support disabled.");
		return -1;
	}
	if (!message || !*message || sscanf(message, "%23[^\n]", guild_name) < 1) {
		clif_displaymessage(fd, "Please, enter a guild name/id (usage: @guildspy <guild_name/id>).");
		return -1;
	}

	if ((g = guild_searchname(guild_name)) != NULL || // name first to avoid error when name begin with a number
	    (g = guild_search(atoi(message))) != NULL) {
		if (sd->guildspy == g->guild_id) {
			sd->guildspy = 0;
			sprintf(atcmd_output, msg_txt(103), g->name); // No longer spying on the %s guild.
			clif_displaymessage(fd, atcmd_output);
		} else {
			sd->guildspy = g->guild_id;
			sprintf(atcmd_output, msg_txt(104), g->name); // Spying on the %s guild.
			clif_displaymessage(fd, atcmd_output);
		}
	} else {
		clif_displaymessage(fd, msg_txt(94)); // Incorrect name/ID, or no one from the specified guild is online.
		return -1;
	}

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
int atcommand_partyspy(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	char party_name[NAME_LENGTH];
	struct party_data *p;
	nullpo_retr(-1, sd);

	memset(party_name, '\0', sizeof(party_name));
	memset(atcmd_output, '\0', sizeof(atcmd_output));

	if (!enable_spy)
	{
		clif_displaymessage(fd, "The mapserver has spy command support disabled.");
		return -1;
	}

	if (!message || !*message || sscanf(message, "%23[^\n]", party_name) < 1) {
		clif_displaymessage(fd, "Please, enter a party name/id (usage: @partyspy <party_name/id>).");
		return -1;
	}

	if ((p = party_searchname(party_name)) != NULL || // name first to avoid error when name begin with a number
	    (p = party_search(atoi(message))) != NULL) {
		if (sd->partyspy == p->party.party_id) {
			sd->partyspy = 0;
			sprintf(atcmd_output, msg_txt(105), p->party.name); // No longer spying on the %s party.
			clif_displaymessage(fd, atcmd_output);
		} else {
			sd->partyspy = p->party.party_id;
			sprintf(atcmd_output, msg_txt(106), p->party.name); // Spying on the %s party.
			clif_displaymessage(fd, atcmd_output);
		}
	} else {
		clif_displaymessage(fd, msg_txt(96)); // Incorrect name/ID, or no one from the specified party is online.
		return -1;
	}

	return 0;
}

/*==========================================
 * @repairall [Valaris]
 *------------------------------------------*/
int atcommand_repairall(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	int count, i;
	nullpo_retr(-1, sd);

	count = 0;
	for (i = 0; i < MAX_INVENTORY; i++) {
		if (sd->status.inventory[i].nameid && sd->status.inventory[i].attribute == 1) {
			sd->status.inventory[i].attribute = 0;
			clif_produceeffect(sd, 0, sd->status.inventory[i].nameid);
			count++;
		}
	}

	if (count > 0) {
		clif_misceffect(&sd->bl, 3);
		clif_equiplist(sd);
		clif_displaymessage(fd, msg_txt(107)); // All items have been repaired.
	} else {
		clif_displaymessage(fd, msg_txt(108)); // No item need to be repaired.
		return -1;
	}

	return 0;
}

/*==========================================
 * @nuke [Valaris]
 *------------------------------------------*/
int atcommand_nuke(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	struct map_session_data *pl_sd;
	nullpo_retr(-1, sd);

	memset(atcmd_player_name, '\0', sizeof(atcmd_player_name));

	if (!message || !*message || sscanf(message, "%23[^\n]", atcmd_player_name) < 1) {
		clif_displaymessage(fd, "Please, enter a player name (usage: @nuke <char name>).");
		return -1;
	}

	if ((pl_sd = map_nick2sd(atcmd_player_name)) != NULL) {
		if (pc_isGM(sd) >= pc_isGM(pl_sd)) { // you can kill only lower or same GM level
			skill_castend_nodamage_id(&pl_sd->bl, &pl_sd->bl, NPC_SELFDESTRUCTION, 99, gettick(), 0);
			clif_displaymessage(fd, msg_txt(109)); // Player has been nuked!
		} else {
			clif_displaymessage(fd, msg_txt(81)); // Your GM level don't authorise you to do this action on this player.
			return -1;
		}
	} else {
		clif_displaymessage(fd, msg_txt(3)); // Character not found.
		return -1;
	}

	return 0;
}

/*==========================================
 * @tonpc
 *------------------------------------------*/
int atcommand_tonpc(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	char npcname[NAME_LENGTH+1];
	struct npc_data *nd;

	nullpo_retr(-1, sd);

	memset(npcname, 0, sizeof(npcname));

	if (!message || !*message || sscanf(message, "%23[^\n]", npcname) < 1) {
		clif_displaymessage(fd, "Please, enter a NPC name (usage: @tonpc <NPC_name>).");
		return -1;
	}

	if ((nd = npc_name2id(npcname)) != NULL) {
		if (pc_setpos(sd, map_id2index(nd->bl.m), nd->bl.x, nd->bl.y, 3) == 0)
			clif_displaymessage(fd, msg_txt(0)); // Warped.
		else
			return -1;
	} else {
		clif_displaymessage(fd, msg_txt(111)); // This NPC doesn't exist.
		return -1;
	}

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
int atcommand_shownpc(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	char NPCname[NAME_LENGTH+1];
	nullpo_retr(-1, sd);

	memset(NPCname, '\0', sizeof(NPCname));

	if (!message || !*message || sscanf(message, "%23[^\n]", NPCname) < 1) {
		clif_displaymessage(fd, "Please, enter a NPC name (usage: @enablenpc <NPC_name>).");
		return -1;
	}

	if (npc_name2id(NPCname) != NULL) {
		npc_enable(NPCname, 1);
		clif_displaymessage(fd, msg_txt(110)); // Npc Enabled.
	} else {
		clif_displaymessage(fd, msg_txt(111)); // This NPC doesn't exist.
		return -1;
	}

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
int atcommand_hidenpc(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	char NPCname[NAME_LENGTH+1];
	nullpo_retr(-1, sd);

	memset(NPCname, '\0', sizeof(NPCname));

	if (!message || !*message || sscanf(message, "%23[^\n]", NPCname) < 1) {
		clif_displaymessage(fd, "Please, enter a NPC name (usage: @hidenpc <NPC_name>).");
		return -1;
	}

	if (npc_name2id(NPCname) == NULL) {
		clif_displaymessage(fd, msg_txt(111)); // This NPC doesn't exist.
		return -1;
	}

	npc_enable(NPCname, 0);
	clif_displaymessage(fd, msg_txt(112)); // Npc Disabled.
	return 0;
}

int atcommand_loadnpc(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	FILE *fp;

	if (!message || !*message) {
		clif_displaymessage(fd, "Please, enter a script file name (usage: @loadnpc <file name>).");
		return -1;
	}

	// check if script file exists
	if ((fp = fopen(message, "r")) == NULL) {
		clif_displaymessage(fd, msg_txt(261));
		return -1;
	}
	fclose(fp);

	// add to list of script sources and run it
	npc_addsrcfile(message);
	npc_parsesrcfile(message);
	npc_read_event_script();

	clif_displaymessage(fd, msg_txt(262));

	return 0;
}

int atcommand_unloadnpc(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	struct npc_data *nd;
	char NPCname[NAME_LENGTH+1];
	nullpo_retr(-1, sd);

	memset(NPCname, '\0', sizeof(NPCname));

	if (!message || !*message || sscanf(message, "%24[^\n]", NPCname) < 1) {
		clif_displaymessage(fd, "Please, enter a NPC name (usage: @npcoff <NPC_name>).");
		return -1;
	}

	if ((nd = npc_name2id(NPCname)) == NULL) {
		clif_displaymessage(fd, msg_txt(111)); // This NPC doesn't exist.
		return -1;
	}

	npc_unload_duplicates(nd);
	npc_unload(nd);
	npc_read_event_script();
	clif_displaymessage(fd, msg_txt(112)); // Npc Disabled.
	return 0;
}

/*==========================================
 * time in txt for time command (by [Yor])
 *------------------------------------------*/
char* txt_time(unsigned int duration)
{
	int days, hours, minutes, seconds;
	char temp[CHAT_SIZE_MAX];
	static char temp1[CHAT_SIZE_MAX];

	memset(temp, '\0', sizeof(temp));
	memset(temp1, '\0', sizeof(temp1));

	days = duration / (60 * 60 * 24);
	duration = duration - (60 * 60 * 24 * days);
	hours = duration / (60 * 60);
	duration = duration - (60 * 60 * hours);
	minutes = duration / 60;
	seconds = duration - (60 * minutes);

	if (days < 2)
		sprintf(temp, msg_txt(219), days); // %d day
	else
		sprintf(temp, msg_txt(220), days); // %d days
	if (hours < 2)
		sprintf(temp1, msg_txt(221), temp, hours); // %s %d hour
	else
		sprintf(temp1, msg_txt(222), temp, hours); // %s %d hours
	if (minutes < 2)
		sprintf(temp, msg_txt(223), temp1, minutes); // %s %d minute
	else
		sprintf(temp, msg_txt(224), temp1, minutes); // %s %d minutes
	if (seconds < 2)
		sprintf(temp1, msg_txt(225), temp, seconds); // %s and %d second
	else
		sprintf(temp1, msg_txt(226), temp, seconds); // %s and %d seconds

	return temp1;
}

/*==========================================
 * @time/@date/@serverdate/@servertime: Display the date/time of the server (by [Yor]
 * Calculation management of GM modification (@day/@night GM commands) is done
 *------------------------------------------*/
int atcommand_servertime(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	const struct TimerData * timer_data;
	const struct TimerData * timer_data2;
	time_t time_server;  // variable for number of seconds (used with time() function)
	struct tm *datetime; // variable for time in structure ->tm_mday, ->tm_sec, ...
	char temp[CHAT_SIZE_MAX];
	nullpo_retr(-1, sd);

	memset(temp, '\0', sizeof(temp));

	time(&time_server);  // get time in seconds since 1/1/1970
	datetime = localtime(&time_server); // convert seconds in structure
	// like sprintf, but only for date/time (Sunday, November 02 2003 15:12:52)
	strftime(temp, sizeof(temp)-1, msg_txt(230), datetime); // Server time (normal time): %A, %B %d %Y %X.
	clif_displaymessage(fd, temp);

	if (battle_config.night_duration == 0 && battle_config.day_duration == 0) {
		if (night_flag == 0)
			clif_displaymessage(fd, msg_txt(231)); // Game time: The game is in permanent daylight.
		else
			clif_displaymessage(fd, msg_txt(232)); // Game time: The game is in permanent night.
	} else if (battle_config.night_duration == 0)
		if (night_flag == 1) { // we start with night
			timer_data = get_timer(day_timer_tid);
			sprintf(temp, msg_txt(233), txt_time(DIFF_TICK(timer_data->tick,gettick())/1000)); // Game time: The game is actualy in night for %s.
			clif_displaymessage(fd, temp);
			clif_displaymessage(fd, msg_txt(234)); // Game time: After, the game will be in permanent daylight.
		} else
			clif_displaymessage(fd, msg_txt(231)); // Game time: The game is in permanent daylight.
	else if (battle_config.day_duration == 0)
		if (night_flag == 0) { // we start with day
			timer_data = get_timer(night_timer_tid);
			sprintf(temp, msg_txt(235), txt_time(DIFF_TICK(timer_data->tick,gettick())/1000)); // Game time: The game is actualy in daylight for %s.
			clif_displaymessage(fd, temp);
			clif_displaymessage(fd, msg_txt(236)); // Game time: After, the game will be in permanent night.
		} else
			clif_displaymessage(fd, msg_txt(232)); // Game time: The game is in permanent night.
	else {
		if (night_flag == 0) {
			timer_data = get_timer(night_timer_tid);
			timer_data2 = get_timer(day_timer_tid);
			sprintf(temp, msg_txt(235), txt_time(DIFF_TICK(timer_data->tick,gettick())/1000)); // Game time: The game is actualy in daylight for %s.
			clif_displaymessage(fd, temp);
			if (DIFF_TICK(timer_data->tick, timer_data2->tick) > 0)
				sprintf(temp, msg_txt(237), txt_time(DIFF_TICK(timer_data->interval,DIFF_TICK(timer_data->tick,timer_data2->tick)) / 1000)); // Game time: After, the game will be in night for %s.
			else
				sprintf(temp, msg_txt(237), txt_time(DIFF_TICK(timer_data2->tick,timer_data->tick)/1000)); // Game time: After, the game will be in night for %s.
			clif_displaymessage(fd, temp);
			sprintf(temp, msg_txt(238), txt_time(timer_data->interval / 1000)); // Game time: A day cycle has a normal duration of %s.
			clif_displaymessage(fd, temp);
		} else {
			timer_data = get_timer(day_timer_tid);
			timer_data2 = get_timer(night_timer_tid);
			sprintf(temp, msg_txt(233), txt_time(DIFF_TICK(timer_data->tick,gettick()) / 1000)); // Game time: The game is actualy in night for %s.
			clif_displaymessage(fd, temp);
			if (DIFF_TICK(timer_data->tick,timer_data2->tick) > 0)
				sprintf(temp, msg_txt(239), txt_time((timer_data->interval - DIFF_TICK(timer_data->tick, timer_data2->tick)) / 1000)); // Game time: After, the game will be in daylight for %s.
			else
				sprintf(temp, msg_txt(239), txt_time(DIFF_TICK(timer_data2->tick, timer_data->tick) / 1000)); // Game time: After, the game will be in daylight for %s.
			clif_displaymessage(fd, temp);
			sprintf(temp, msg_txt(238), txt_time(timer_data->interval / 1000)); // Game time: A day cycle has a normal duration of %s.
			clif_displaymessage(fd, temp);
		}
	}

	return 0;
}

//Added by Coltaro
//We're using this function here instead of using time_t so that it only counts player's jail time when he/she's online (and since the idea is to reduce the amount of minutes one by one in status_change_timer...).
//Well, using time_t could still work but for some reason that looks like more coding x_x
static void get_jail_time(int jailtime, int* year, int* month, int* day, int* hour, int* minute)
{
	const int factor_year = 518400; //12*30*24*60 = 518400
	const int factor_month = 43200; //30*24*60 = 43200
	const int factor_day = 1440; //24*60 = 1440
	const int factor_hour = 60;

	*year = jailtime/factor_year;
	jailtime -= *year*factor_year;
	*month = jailtime/factor_month;
	jailtime -= *month*factor_month;
	*day = jailtime/factor_day;
	jailtime -= *day*factor_day;
	*hour = jailtime/factor_hour;
	jailtime -= *hour*factor_hour;
	*minute = jailtime;

	*year = *year > 0? *year : 0;
	*month = *month > 0? *month : 0;
	*day = *day > 0? *day : 0;
	*hour = *hour > 0? *hour : 0;
	*minute = *minute > 0? *minute : 0;
	return;
}

/*==========================================
 * @jail <char_name> by [Yor]
 * Special warp! No check with nowarp and nowarpto flag
 *------------------------------------------*/
int atcommand_jail(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	struct map_session_data *pl_sd;
	int x, y;
	unsigned short m_index;
	nullpo_retr(-1, sd);

	memset(atcmd_player_name, '\0', sizeof(atcmd_player_name));

	if (!message || !*message || sscanf(message, "%23[^\n]", atcmd_player_name) < 1) {
		clif_displaymessage(fd, "Please, enter a player name (usage: @jail <char_name>).");
		return -1;
	}

	if ((pl_sd = map_nick2sd(atcmd_player_name)) == NULL) {
		clif_displaymessage(fd, msg_txt(3)); // Character not found.
		return -1;
	}

	if (pc_isGM(sd) < pc_isGM(pl_sd))
  	{ // you can jail only lower or same GM
		clif_displaymessage(fd, msg_txt(81)); // Your GM level don't authorise you to do this action on this player.
		return -1;
	}

	if (pl_sd->sc.data[SC_JAILED])
	{
		clif_displaymessage(fd, msg_txt(118)); // Player warped in jails.
		return -1;
	}

	switch(rand() % 2) { //Jail Locations
	case 0:
		m_index = mapindex_name2id(MAP_JAIL);
		x = 24;
		y = 75;
		break;
	default:
		m_index = mapindex_name2id(MAP_JAIL);
		x = 49;
		y = 75;
		break;
	}

	//Duration of INT_MAX to specify infinity.
	sc_start4(&pl_sd->bl,SC_JAILED,100,INT_MAX,m_index,x,y,1000);
	clif_displaymessage(pl_sd->fd, msg_txt(117)); // GM has send you in jails.
	clif_displaymessage(fd, msg_txt(118)); // Player warped in jails.
	return 0;
}

/*==========================================
 * @unjail/@discharge <char_name> by [Yor]
 * Special warp! No check with nowarp and nowarpto flag
 *------------------------------------------*/
int atcommand_unjail(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	struct map_session_data *pl_sd;

	memset(atcmd_player_name, '\0', sizeof(atcmd_player_name));

	if (!message || !*message || sscanf(message, "%23[^\n]", atcmd_player_name) < 1) {
		clif_displaymessage(fd, "Please, enter a player name (usage: @unjail/@discharge <char_name>).");
		return -1;
	}

	if ((pl_sd = map_nick2sd(atcmd_player_name)) == NULL) {
		clif_displaymessage(fd, msg_txt(3)); // Character not found.
		return -1;
	}

	if (pc_isGM(sd) < pc_isGM(pl_sd)) { // you can jail only lower or same GM

		clif_displaymessage(fd, msg_txt(81)); // Your GM level don't authorise you to do this action on this player.
		return -1;
	}

	if (!pl_sd->sc.data[SC_JAILED])
	{
		clif_displaymessage(fd, msg_txt(119)); // This player is not in jails.
		return -1;
	}

	//Reset jail time to 1 sec.
	sc_start(&pl_sd->bl,SC_JAILED,100,1,1000);
	clif_displaymessage(pl_sd->fd, msg_txt(120)); // A GM has discharged you from jail.
	clif_displaymessage(fd, msg_txt(121)); // Player unjailed.
	return 0;
}

int atcommand_jailfor(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	struct map_session_data *pl_sd = NULL;
	int year, month, day, hour, minute, value;
	char * modif_p;
	int jailtime = 0,x,y;
	short m_index = 0;
	nullpo_retr(-1, sd);

	if (!message || !*message || sscanf(message, "%s %23[^\n]",atcmd_output,atcmd_player_name) < 2) {
		clif_displaymessage(fd, msg_txt(400));	//Usage: @jailfor <time> <character name>
		return -1;
	}

	atcmd_output[sizeof(atcmd_output)-1] = '\0';

	modif_p = atcmd_output;
	year = month = day = hour = minute = 0;
	while (modif_p[0] != '\0') {
		value = atoi(modif_p);
		if (value == 0)
			modif_p++;
		else {
			if (modif_p[0] == '-' || modif_p[0] == '+')
				modif_p++;
			while (modif_p[0] >= '0' && modif_p[0] <= '9')
				modif_p++;
			if (modif_p[0] == 'n') {
				minute = value;
				modif_p++;
			} else if (modif_p[0] == 'm' && modif_p[1] == 'n') {
				minute = value;
				modif_p = modif_p + 2;
			} else if (modif_p[0] == 'h') {
				hour = value;
				modif_p++;
			} else if (modif_p[0] == 'd' || modif_p[0] == 'j') {
				day = value;
				modif_p++;
			} else if (modif_p[0] == 'm') {
				month = value;
				modif_p++;
			} else if (modif_p[0] == 'y' || modif_p[0] == 'a') {
				year = value;
				modif_p++;
			} else if (modif_p[0] != '\0') {
				modif_p++;
			}
		}
	}

	if (year == 0 && month == 0 && day == 0 && hour == 0 && minute == 0) {
		clif_displaymessage(fd, "Invalid time for jail command.");
		return -1;
	}

	if ((pl_sd = map_nick2sd(atcmd_player_name)) == NULL) {
		clif_displaymessage(fd, msg_txt(3)); // Character not found.
		return -1;
	}

	if (pc_isGM(pl_sd) > pc_isGM(sd)) {
		clif_displaymessage(fd, msg_txt(81)); // Your GM level don't authorise you to do this action on this player.
		return -1;
	}

	jailtime = year*12*30*24*60 + month*30*24*60 + day*24*60 + hour*60 + minute;	//In minutes

	if(jailtime==0) {
		clif_displaymessage(fd, "Invalid time for jail command.");
		return -1;
	}

	//Added by Coltaro
	if(pl_sd->sc.data[SC_JAILED] && 
		pl_sd->sc.data[SC_JAILED]->val1 != INT_MAX)
  	{	//Update the player's jail time
		jailtime += pl_sd->sc.data[SC_JAILED]->val1;
		if (jailtime <= 0) {
			jailtime = 0;
			clif_displaymessage(pl_sd->fd, msg_txt(120)); // GM has discharge you.
			clif_displaymessage(fd, msg_txt(121)); // Player unjailed
		} else {
			get_jail_time(jailtime,&year,&month,&day,&hour,&minute);
			sprintf(atcmd_output,msg_txt(402),"You are now",year,month,day,hour,minute); //%s in jail for %d years, %d months, %d days, %d hours and %d minutes
	 		clif_displaymessage(pl_sd->fd, atcmd_output);
			sprintf(atcmd_output,msg_txt(402),"This player is now",year,month,day,hour,minute); //This player is now in jail for %d years, %d months, %d days, %d hours and %d minutes
	 		clif_displaymessage(fd, atcmd_output);
		}
	} else if (jailtime < 0) {
		clif_displaymessage(fd, "Invalid time for jail command.");
		return -1;
	}

	//Jail locations, add more as you wish.
	switch(rand()%2)
	{
		case 1: //Jail #1
			m_index = mapindex_name2id(MAP_JAIL);
			x = 49; y = 75;
			break;
		default: //Default Jail
			m_index = mapindex_name2id(MAP_JAIL);
			x = 24; y = 75;
			break;
	}

	sc_start4(&pl_sd->bl,SC_JAILED,100,jailtime,m_index,x,y,jailtime?60000:1000); //jailtime = 0: Time was reset to 0. Wait 1 second to warp player out (since it's done in status_change_timer).
	return 0;
}


//By Coltaro
int atcommand_jailtime(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	int year, month, day, hour, minute;

	nullpo_retr(-1, sd);
	
	if (!sd->sc.data[SC_JAILED]) {
		clif_displaymessage(fd, "You are not in jail."); // You are not in jail.
		return -1;
	}

	if (sd->sc.data[SC_JAILED]->val1 == INT_MAX) {
		clif_displaymessage(fd, "You have been jailed indefinitely.");
		return 0;
	}

	if (sd->sc.data[SC_JAILED]->val1 <= 0) { // Was not jailed with @jailfor (maybe @jail? or warped there? or got recalled?)
		clif_displaymessage(fd, "You have been jailed for an unknown amount of time.");
		return -1;
	}

	//Get remaining jail time
	get_jail_time(sd->sc.data[SC_JAILED]->val1,&year,&month,&day,&hour,&minute);
	sprintf(atcmd_output,msg_txt(402),"You will remain",year,month,day,hour,minute); // You will remain in jail for %d years, %d months, %d days, %d hours and %d minutes

	clif_displaymessage(fd, atcmd_output);

	return 0;
}

/*==========================================
 * @disguise <mob_id> by [Valaris] (simplified by [Yor])
 *------------------------------------------*/
int atcommand_disguise(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	int id = 0;
	nullpo_retr(-1, sd);

	if (!message || !*message) {
		clif_displaymessage(fd, "Please, enter a Monster/NPC name/id (usage: @disguise <monster_name_or_monster_ID>).");
		return -1;
	}

	if ((id = atoi(message)) > 0)
	{	//Acquired an ID
		if (!mobdb_checkid(id) && !npcdb_checkid(id))
			id = 0; //Invalid id for either mobs or npcs.
	}	else	{ //Acquired a Name
		if ((id = mobdb_searchname(message)) == 0)
		{
			struct npc_data* nd = npc_name2id(message);
			if (nd != NULL)
				id = nd->class_;
		}
	}

	if (id == 0)
	{
		clif_displaymessage(fd, msg_txt(123));	// Invalid Monster/NPC name/ID specified.
		return -1;
	}

	if( pc_isriding(sd, OPTION_RIDING|OPTION_RIDING_DRAGON|OPTION_RIDING_WUG|OPTION_MADO) )
	{
		//FIXME: wrong message [ultramage]
		//clif_displaymessage(fd, msg_txt(227)); // Character cannot wear disguise while riding a PecoPeco.
		return -1;
	}

	pc_disguise(sd, id);
	clif_displaymessage(fd, msg_txt(122)); // Disguise applied.

	return 0;
}

/*==========================================
 * DisguiseAll
 *------------------------------------------*/
int atcommand_disguiseall(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	int mob_id=0;
	struct map_session_data *pl_sd;
	struct s_mapiterator* iter;
	nullpo_retr(-1, sd);

	if (!message || !*message) {
		clif_displaymessage(fd, "Please, enter a Monster/NPC name/id (usage: @disguiseall <monster name or monster ID>).");
		return -1;
	}

	if ((mob_id = mobdb_searchname(message)) == 0) // check name first (to avoid possible name begining by a number)
		mob_id = atoi(message);

	if (!mobdb_checkid(mob_id) && !npcdb_checkid(mob_id)) { //if mob or npc...
		clif_displaymessage(fd, msg_txt(123)); // Monster/NPC name/id not found.
		return -1;
	}

	iter = mapit_getallusers();
	for( pl_sd = (TBL_PC*)mapit_first(iter); mapit_exists(iter); pl_sd = (TBL_PC*)mapit_next(iter) )
	{
		if( !pc_isriding(pl_sd, OPTION_RIDING|OPTION_RIDING_DRAGON|OPTION_RIDING_WUG|OPTION_MADO) )
			pc_disguise(pl_sd, mob_id);
	}
	mapit_free(iter);

	clif_displaymessage(fd, msg_txt(122)); // Disguise applied.
	return 0;
}

/*==========================================
 * DisguiseGuild
 *------------------------------------------*/
int atcommand_disguiseguild(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	struct map_session_data *pl_sd;
	int id = 0, i;
	char guild_name[NAME_LENGTH], name[NAME_LENGTH];
	struct guild *g;

	memset(guild_name, '\0', sizeof(guild_name));
	memset(name, '\0', sizeof(name));

	if( !message || !*message || sscanf(message, "%23[^,], %23[^\r\n]", name, guild_name) < 2 )
	{
		clif_displaymessage(fd, "Please, enter a guild name/id (usage: @disguiseguild <mob_name/id>,<guild_name/id>).");
		return -1;
	}

	if( (id = atoi(name)) > 0 )
	{
		if( !mobdb_checkid(id) && !npcdb_checkid(id) )
			id = 0;
	}
	else
	{
		if( (id = mobdb_searchname(name)) == 0 )
		{
			struct npc_data* nd = npc_name2id(name);
			if( nd != NULL )
				id = nd->class_;
		}
	}

	if( id == 0 )
	{
		clif_displaymessage(fd, msg_txt(123));	// Monster/NPC name/id hasn't been found.
		return -1;
	}

	if( (g = guild_searchname(guild_name)) == NULL && (g = guild_search(atoi(guild_name))) == NULL )
	{
		clif_displaymessage(fd, msg_txt(94)); // Incorrect name/ID, or no one from the guild is online.
		return -1;
	}

	for( i = 0; i < g->max_member; i++ )
	{
		if( (pl_sd = g->member[i].sd ) == NULL )
			continue;

		if( pc_isriding(pl_sd, OPTION_RIDING|OPTION_RIDING_DRAGON|OPTION_RIDING_WUG|OPTION_MADO) )
			continue;

		pc_disguise(pl_sd, id);
	}

	return 0;
}

/*==========================================
 * @undisguise by [Yor]
 *------------------------------------------*/
int atcommand_undisguise(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	nullpo_retr(-1, sd);
	if (sd->disguise) {
		pc_disguise(sd, 0);
		clif_displaymessage(fd, msg_txt(124)); // Undisguise applied.
	} else {
		clif_displaymessage(fd, msg_txt(125)); // You're not disguised.
		return -1;
	}

	return 0;
}

/*==========================================
 * UndisguiseAll
 *------------------------------------------*/
int atcommand_undisguiseall(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	struct map_session_data *pl_sd;
	struct s_mapiterator* iter;
	nullpo_retr(-1, sd);

	iter = mapit_getallusers();
	for( pl_sd = (TBL_PC*)mapit_first(iter); mapit_exists(iter); pl_sd = (TBL_PC*)mapit_next(iter) )
		if( pl_sd->disguise )
			pc_disguise(pl_sd, 0);
	mapit_free(iter);

	clif_displaymessage(fd, msg_txt(124)); // Undisguise applied.

	return 0;
}

/*==========================================
 * @exp by [Skotlex]
 *------------------------------------------*/
int atcommand_exp(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	char output[CHAT_SIZE_MAX];
	double nextb, nextj;
	nullpo_retr(-1, sd);
	memset(output, '\0', sizeof(output));
	
	nextb = pc_nextbaseexp(sd);
	if (nextb)
		nextb = sd->status.base_exp*100.0/nextb;
	
	nextj = pc_nextjobexp(sd);
	if (nextj)
		nextj = sd->status.job_exp*100.0/nextj;
	
	sprintf(output, "Base Level: %d (%.3f%%) | Job Level: %d (%.3f%%)", sd->status.base_level, nextb, sd->status.job_level, nextj);
	clif_displaymessage(fd, output);
	return 0;
}


/*==========================================
 * @broadcast by [Valaris]
 *------------------------------------------*/
int atcommand_broadcast(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	nullpo_retr(-1, sd);

	memset(atcmd_output, '\0', sizeof(atcmd_output));

	if (!message || !*message) {
		clif_displaymessage(fd, "Please, enter a message (usage: @broadcast <message>).");
		return -1;
	}

	sprintf(atcmd_output, "%s: %s", sd->status.name, message);
	intif_broadcast(atcmd_output, strlen(atcmd_output) + 1, 0);

	return 0;
}

/*==========================================
 * @localbroadcast by [Valaris]
 *------------------------------------------*/
int atcommand_localbroadcast(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	nullpo_retr(-1, sd);

	memset(atcmd_output, '\0', sizeof(atcmd_output));

	if (!message || !*message) {
		clif_displaymessage(fd, "Please, enter a message (usage: @localbroadcast <message>).");
		return -1;
	}

	sprintf(atcmd_output, "%s: %s", sd->status.name, message);

	clif_broadcast(&sd->bl, atcmd_output, strlen(atcmd_output) + 1, 0, ALL_SAMEMAP);

	return 0;
}

/*==========================================
 * @email <actual@email> <new@email> by [Yor]
 *------------------------------------------*/
int atcommand_email(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	char actual_email[100];
	char new_email[100];
	nullpo_retr(-1, sd);

	memset(actual_email, '\0', sizeof(actual_email));
	memset(new_email, '\0', sizeof(new_email));

	if (!message || !*message || sscanf(message, "%99s %99s", actual_email, new_email) < 2) {
		clif_displaymessage(fd, "Please enter 2 emails (usage: @email <actual@email> <new@email>).");
		return -1;
	}

	if (e_mail_check(actual_email) == 0) {
		clif_displaymessage(fd, msg_txt(144)); // Invalid actual email. If you have default e-mail, give a@a.com.
		return -1;
	} else if (e_mail_check(new_email) == 0) {
		clif_displaymessage(fd, msg_txt(145)); // Invalid new email. Please enter a real e-mail.
		return -1;
	} else if (strcmpi(new_email, "a@a.com") == 0) {
		clif_displaymessage(fd, msg_txt(146)); // New email must be a real e-mail.
		return -1;
	} else if (strcmpi(actual_email, new_email) == 0) {
		clif_displaymessage(fd, msg_txt(147)); // New email must be different of the actual e-mail.
		return -1;
	}

	chrif_changeemail(sd->status.account_id, actual_email, new_email);
	clif_displaymessage(fd, msg_txt(148)); // Information sended to login-server via char-server.
	return 0;
}

/*==========================================
 *@effect
 *------------------------------------------*/
int atcommand_effect(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	int type = 0, flag = 0;
	nullpo_retr(-1, sd);

	if (!message || !*message || sscanf(message, "%d", &type) < 1) {
		clif_displaymessage(fd, "Please, enter an effect number (usage: @effect <effect number>).");
		return -1;
	}

	clif_specialeffect(&sd->bl, type, (send_target)flag);
	clif_displaymessage(fd, msg_txt(229)); // Your effect has changed.
	return 0;
}

/*==========================================
 * @killer by MouseJstr
 * enable killing players even when not in pvp
 *------------------------------------------*/
int atcommand_killer(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	nullpo_retr(-1, sd);
	sd->state.killer = !sd->state.killer;

	if(sd->state.killer)
		clif_displaymessage(fd, msg_txt(241));
	else {
		clif_displaymessage(fd, msg_txt(287));
		pc_stop_attack(sd);
	}
	return 0;
}

/*==========================================
 * @killable by MouseJstr
 * enable other people killing you
 *------------------------------------------*/
int atcommand_killable(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	nullpo_retr(-1, sd);
	sd->state.killable = !sd->state.killable;

	if(sd->state.killable)
		clif_displaymessage(fd, msg_txt(242));
	else {
		clif_displaymessage(fd, msg_txt(288));
		map_foreachinrange(atcommand_stopattack,&sd->bl, AREA_SIZE, BL_CHAR, sd->bl.id);
	}
	return 0;
}

/*==========================================
 * @skillon by MouseJstr
 * turn skills on for the map
 *------------------------------------------*/
int atcommand_skillon(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	nullpo_retr(-1, sd);
	map[sd->bl.m].flag.noskill = 0;
	clif_displaymessage(fd, msg_txt(244));
	return 0;
}

/*==========================================
 * @skilloff by MouseJstr
 * Turn skills off on the map
 *------------------------------------------*/
int atcommand_skilloff(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	nullpo_retr(-1, sd);
	map[sd->bl.m].flag.noskill = 1;
	clif_displaymessage(fd, msg_txt(243));
	return 0;
}

/*==========================================
 * @npcmove by MouseJstr
 * move a npc
 *------------------------------------------*/
int atcommand_npcmove(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	int x = 0, y = 0, m;
	struct npc_data *nd = 0;
	nullpo_retr(-1, sd);
	memset(atcmd_player_name, '\0', sizeof atcmd_player_name);

	if (!message || !*message || sscanf(message, "%d %d %23[^\n]", &x, &y, atcmd_player_name) < 3) {
		clif_displaymessage(fd, "Usage: @npcmove <X> <Y> <npc_name>");
		return -1;
	}

	if ((nd = npc_name2id(atcmd_player_name)) == NULL)
	{
		clif_displaymessage(fd, msg_txt(111)); // This NPC doesn't exist.
		return -1;
	}

	if ((m=nd->bl.m) < 0 || nd->bl.prev == NULL)
	{
		clif_displaymessage(fd, "NPC is not on this map.");
		return -1;	//Not on a map.
	}
	
	x = cap_value(x, 0, map[m].xs-1);
	y = cap_value(y, 0, map[m].ys-1);
	map_foreachinrange(clif_outsight, &nd->bl, AREA_SIZE, BL_PC, &nd->bl);
	map_moveblock(&nd->bl, x, y, gettick());
	map_foreachinrange(clif_insight, &nd->bl, AREA_SIZE, BL_PC, &nd->bl);
	clif_displaymessage(fd, "NPC moved.");

	return 0;
}

/*==========================================
 * @addwarp by MouseJstr
 * Create a new static warp point.
 *------------------------------------------*/
int atcommand_addwarp(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	char mapname[32];
	int x,y;
	unsigned short m;
	struct npc_data* nd;

	nullpo_retr(-1, sd);

	if (!message || !*message || sscanf(message, "%31s %d %d", mapname, &x, &y) < 3) {
		clif_displaymessage(fd, "usage: @addwarp <mapname> <X> <Y>.");
		return -1;
	}

	m = mapindex_name2id(mapname);
	if( m == 0 )
	{
		sprintf(atcmd_output, "Unknown map '%s'.", mapname);
		clif_displaymessage(fd, atcmd_output);
		return -1;
	}

	nd = npc_add_warp(sd->bl.m, sd->bl.x, sd->bl.y, 2, 2, m, x, y);
	if( nd == NULL )
		return -1;

	sprintf(atcmd_output, "New warp NPC '%s' created.", nd->exname);
	clif_displaymessage(fd, atcmd_output);
	return 0;
}

/*==========================================
 * @follow by [MouseJstr]
 * Follow a player .. staying no more then 5 spaces away
 *------------------------------------------*/
int atcommand_follow(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	struct map_session_data *pl_sd = NULL;
	nullpo_retr(-1, sd);

	if (!message || !*message) {
		if (sd->followtarget == -1)
			return -1;

		pc_stop_following (sd);
		clif_displaymessage(fd, "Follow mode OFF.");
		return 0;
	}
	
	if ( (pl_sd = map_nick2sd((char *)message)) == NULL )
	{
		clif_displaymessage(fd, msg_txt(3)); // Character not found.
		return -1;
	}

	if (sd->followtarget == pl_sd->bl.id) {
		pc_stop_following (sd);
		clif_displaymessage(fd, "Follow mode OFF.");
	} else {
		pc_follow(sd, pl_sd->bl.id);
		clif_displaymessage(fd, "Follow mode ON.");
	}
	
	return 0;
}


/*==========================================
 * @dropall by [MouseJstr]
 * Drop all your possession on the ground
 *------------------------------------------*/
int atcommand_dropall(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	int i;
	nullpo_retr(-1, sd);
	for (i = 0; i < MAX_INVENTORY; i++) {
	if (sd->status.inventory[i].amount) {
		if(sd->status.inventory[i].equip != 0)
			pc_unequipitem(sd, i, 3);
			pc_dropitem(sd,  i, sd->status.inventory[i].amount);
		}
	}
	return 0;
}

/*==========================================
 * @storeall by [MouseJstr]
 * Put everything into storage
 *------------------------------------------*/
int atcommand_storeall(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	int i;
	nullpo_retr(-1, sd);

	if (sd->state.storage_flag != 1)
  	{	//Open storage.
		if( storage_storageopen(sd) == 1 ) {
			clif_displaymessage(fd, "You can't open the storage currently.");
			return -1;
		}
	}

	for (i = 0; i < MAX_INVENTORY; i++) {
		if (sd->status.inventory[i].amount) {
			if(sd->status.inventory[i].equip != 0)
				pc_unequipitem(sd, i, 3);
			storage_storageadd(sd,  i, sd->status.inventory[i].amount);
		}
	}
	storage_storageclose(sd);

	clif_displaymessage(fd, "It is done");
	return 0;
}

/*==========================================
 * @skillid by [MouseJstr]
 * lookup a skill by name
 *------------------------------------------*/
int atcommand_skillid(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	int skillen, idx;
	nullpo_retr(-1, sd);

	if (!message || !*message)
	{
		clif_displaymessage(fd, "Please enter a skill name to look up (usage: @skillid <skill name>).");
		return -1;
	}

	skillen = strlen(message);

	for (idx = 0; idx < MAX_SKILL_DB; idx++) {
		if (strnicmp(skill_db[idx].name, message, skillen) == 0 || strnicmp(skill_db[idx].desc, message, skillen) == 0)
		{
			sprintf(atcmd_output, "skill %d: %s", idx, skill_db[idx].desc);
			clif_displaymessage(fd, atcmd_output);
		}
	}

	return 0;
}
/*==========================================
 * @blockskill
 * Block/UnBlock Skill Usage
 *------------------------------------------*/
int atcommand_blockskill(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	int idx = atoi(message);
	if( (idx = skill_get_index(idx)) > 0 )
	{
		skill_db[idx].blocked = !skill_db[idx].blocked;
		if( skill_db[idx].blocked )
			sprintf(atcmd_output, "Blocked usage of Skill %d: %s", idx, skill_db[idx].desc);
		else
			sprintf(atcmd_output, "Allowed usage of Skill %d: %s", idx, skill_db[idx].desc);

		clif_displaymessage(fd, atcmd_output);
		return 0;
	}
	else
	{
		clif_displaymessage(fd, "Skill not found...");
		return -1;
	}
}

/*==========================================
 * @useskill by [MouseJstr]
 * A way of using skills without having to find them in the skills menu
 *------------------------------------------*/
int atcommand_useskill(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	struct map_session_data *pl_sd = NULL;
	struct block_list *bl;
	int skillnum;
	int skilllv;
	char target[100];
	nullpo_retr(-1, sd);

	if(!message || !*message || sscanf(message, "%d %d %23[^\n]", &skillnum, &skilllv, target) != 3) {
		clif_displaymessage(fd, "Usage: @useskill <skillnum> <skillv> <target>");
		return -1;
	}

	if ( (pl_sd = map_nick2sd(target)) == NULL )
	{
		clif_displaymessage(fd, msg_txt(3)); // Character not found.
		return -1;
	}

	if ( pc_isGM(sd) < pc_isGM(pl_sd) )
	{
		clif_displaymessage(fd, msg_txt(81)); // Your GM level don't authorise you to do this action on this player.
		return -1;
	}

	if (skillnum >= HM_SKILLBASE && skillnum < HM_SKILLBASE+MAX_HOMUNSKILL
		&& sd->hd && merc_is_hom_active(sd->hd)) // (If used with @useskill, put the homunc as dest)
		bl = &sd->hd->bl;
	else
		bl = &sd->bl;
	
	if (skill_get_inf(skillnum)&INF_GROUND_SKILL)
		unit_skilluse_pos(bl, pl_sd->bl.x, pl_sd->bl.y, skillnum, skilllv);
	else
		unit_skilluse_id(bl, pl_sd->bl.id, skillnum, skilllv);

	return 0;
}

/*==========================================
 * @displayskill by [Skotlex]
 *  Debug command to locate new skill IDs. It sends the
 *  three possible skill-effect packets to the area.
 *------------------------------------------*/
int atcommand_displayskill(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	struct status_data * status;
	unsigned int tick;
	int skillnum;
	int skilllv = 1;
	nullpo_retr(-1, sd);

	if (!message || !*message || sscanf(message, "%d %d", &skillnum, &skilllv) < 1)
	{
		clif_displaymessage(fd, "Usage: @displayskill <skillnum> {<skillv>}>");
		return -1;
	}
	status = status_get_status_data(&sd->bl);
	tick = gettick();
	clif_skill_damage(&sd->bl,&sd->bl, tick, status->amotion, status->dmotion, 1, 1, skillnum, skilllv, 5);
	clif_skill_nodamage(&sd->bl, &sd->bl, skillnum, skilllv, 1);
	clif_skill_poseffect(&sd->bl, skillnum, skilllv, sd->bl.x, sd->bl.y, tick);
	return 0;
}

/*==========================================
 * @skilltree by [MouseJstr]
 * prints the skill tree for a player required to get to a skill
 *------------------------------------------*/
int atcommand_skilltree(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	struct map_session_data *pl_sd = NULL;
	int skillnum;
	int meets, j, c=0;
	char target[NAME_LENGTH], *tbl;
	struct skill_tree_entry *ent;
	nullpo_retr(-1, sd);

	if(!message || !*message || sscanf(message, "%d %23[^\r\n]", &skillnum, target) != 2) {
		clif_displaymessage(fd, "Usage: @skilltree <skillnum> <target>");
		return -1;
	}

	if ( (pl_sd = map_nick2sd(target)) == NULL )
	{
		clif_displaymessage(fd, msg_txt(3)); // Character not found.
		return -1;
	}

	c = pc_calc_skilltree_normalize_job(pl_sd);
	c = pc_mapid2jobid(c, pl_sd->status.sex);

	tbl = job_name(c);

	sprintf(atcmd_output, "Player is using %s skill tree (%d basic points)", tbl, pc_checkskill(pl_sd, 1));
	clif_displaymessage(fd, atcmd_output);

	ARR_FIND( 0, MAX_SKILL_TREE, j, skill_tree[c][j].id == 0 || skill_tree[c][j].id == skillnum );
	if( j == MAX_SKILL_TREE || skill_tree[c][j].id == 0 )
	{
		sprintf(atcmd_output, "I do not believe the player can use that skill");
		clif_displaymessage(fd, atcmd_output);
		return 0;
	}

	ent = &skill_tree[c][j];

	meets = 1;
	for(j=0;j<5;j++)
	{
		if( ent->need[j].id && pc_checkskill(sd,ent->need[j].id) < ent->need[j].lv)
		{
			sprintf(atcmd_output, "player requires level %d of skill %s", ent->need[j].lv, skill_db[ent->need[j].id].desc);
			clif_displaymessage(fd, atcmd_output);
			meets = 0;
		}
	}
	if (meets == 1) {
		sprintf(atcmd_output, "I believe the player meets all the requirements for that skill");
		clif_displaymessage(fd, atcmd_output);
	}

	return 0;
}

// Hand a ring with partners name on it to this char
void getring (struct map_session_data* sd)
{
	int flag, item_id;
	struct item item_tmp;
	item_id = (sd->status.sex) ? WEDDING_RING_M : WEDDING_RING_F;

	memset(&item_tmp, 0, sizeof(item_tmp));
	item_tmp.nameid = item_id;
	item_tmp.identify = 1;
	item_tmp.card[0] = 255;
	item_tmp.card[2] = sd->status.partner_id;
	item_tmp.card[3] = sd->status.partner_id >> 16;

	if((flag = pc_additem(sd,&item_tmp,1))) {
		clif_additem(sd,0,0,flag);
		map_addflooritem(&item_tmp,1,sd->bl.m,sd->bl.x,sd->bl.y,0,0,0,sd->status.guild_id,0);
	}

	//Logs (A)dmins items [Lupus]
	if(log_config.enable_logs&0x400)
		log_pick_pc(sd, "A", item_id, 1, &item_tmp, item_tmp.serial);
}

/*==========================================
 * @marry by [MouseJstr], fixed by Lupus
 * Marry two players
 *------------------------------------------*/
int atcommand_marry(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
  struct map_session_data *pl_sd1 = NULL;
  struct map_session_data *pl_sd2 = NULL;
  char player1[128], player2[128]; //Length used for return error msgs

  nullpo_retr(-1, sd);

  if (!message || !*message || sscanf(message, "%23[^,], %23[^\r\n]", player1, player2) != 2) {
	clif_displaymessage(fd, "Usage: @marry <player1>,<player2>");
	return -1;
  }

  if((pl_sd1=map_nick2sd((char *) player1)) == NULL) {
	sprintf(player2, "Cannot find player '%s' online", player1);
	clif_displaymessage(fd, player2);
	return -1;
  }

  if((pl_sd2=map_nick2sd((char *) player2)) == NULL) {
	sprintf(player1, "Cannot find player '%s' online", player2);
	clif_displaymessage(fd, player1);
	return -1;
  }

  if (pc_marriage(pl_sd1, pl_sd2) == 0) {
	clif_displaymessage(fd, "They are married.. wish them well");
	clif_wedding_effect(&sd->bl);	//wedding effect and music [Lupus]
	// Auto-give named rings (Aru)
	getring (pl_sd1);
	getring (pl_sd2);
	return 0;
  }

	clif_displaymessage(fd, "The two cannot wed because one of them is either a baby or is already married.");
	return -1;
}

/*==========================================
 * @divorce by [MouseJstr], fixed by [Lupus]
 * divorce two players
 *------------------------------------------*/
int atcommand_divorce(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
  struct map_session_data *pl_sd = NULL;

  nullpo_retr(-1, sd);

  if (!message || !*message || sscanf(message, "%23[^\r\n]", atcmd_player_name) != 1) {
	clif_displaymessage(fd, "Usage: @divorce <player>.");
	return -1;
  }

	if ( (pl_sd = map_nick2sd(atcmd_player_name)) == NULL )
	{
		clif_displaymessage(fd, msg_txt(3)); // Character not found.
		return -1;
	}

	if (pc_divorce(pl_sd) != 0) {
		sprintf(atcmd_output, "The divorce has failed.. Cannot find player '%s' or his(her) partner online.", atcmd_player_name);
		clif_displaymessage(fd, atcmd_output);
		return -1;
	}
	
	sprintf(atcmd_output, "'%s' and his(her) partner are now divorced.", atcmd_player_name);
	clif_displaymessage(fd, atcmd_output);
	return 0;
}

/*==========================================
 * @changelook by [Celest]
 *------------------------------------------*/
int atcommand_changelook(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	int i, j = 0, k = 0;
	int pos[6] = { LOOK_HEAD_TOP,LOOK_HEAD_MID,LOOK_HEAD_BOTTOM,LOOK_WEAPON,LOOK_SHIELD,LOOK_SHOES };

	if((i = sscanf(message, "%d %d", &j, &k)) < 1) {
		clif_displaymessage(fd, "Usage: @changelook [<position>] <view id> -- [] = optional");
		clif_displaymessage(fd, "Position: 1-Top 2-Middle 3-Bottom 4-Weapon 5-Shield");
		return -1;
	} else if (i == 2) {
		if (j < 1) j = 1;
		else if (j > 6) j = 6;	// 6 = Shoes - for beta clients only perhaps
		j = pos[j - 1];
	} else if (i == 1) {	// position not defined, use HEAD_TOP as default
		k = j;	// swap
		j = LOOK_HEAD_TOP;
	}

	clif_changelook(&sd->bl,j,k);

	return 0;
}

/*==========================================
 * @autotrade by durf [Lupus] [Paradox924X]
 * Turns on/off Autotrade for a specific player
 *------------------------------------------*/
int atcommand_autotrade(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	nullpo_retr(-1, sd);
	
	if( map[sd->bl.m].flag.autotrade != battle_config.autotrade_mapflag ) {
		clif_displaymessage(fd, "Autotrade is not allowed on this map.");
		return -1;
	}

	if( pc_isdead(sd) ) {
		clif_displaymessage(fd, "Cannot Autotrade if you are dead.");
		return -1;
	}
	
	if( !sd->vender_id && !sd->buyer_id ) { //check if player is vending
		clif_displaymessage(fd, msg_txt(549)); // You should be vending to use @Autotrade.
		return -1;
	}
	
	sd->state.autotrade = 1;
	if( battle_config.at_timeout )
	{
		int timeout = atoi(message);
		int limit = pc_isPremium(sd) ? battle_config.at_timeout * 5 : battle_config.at_timeout;
		status_change_start(&sd->bl, SC_AUTOTRADE, 10000, 0, 0, 0, 0, ((timeout > 0) ? min(timeout,limit) : limit) * 60000, 0);
	}
	clif_authfail_fd(fd, 15);
		
	return 0;
}

/*==========================================
 * @changegm by durf (changed by Lupus)
 * Changes Master of your Guild to a specified guild member
 *------------------------------------------*/
int atcommand_changegm(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	struct guild *g;
	struct map_session_data *pl_sd;
	nullpo_retr(-1, sd);

	if( sd->status.guild_id == 0 || (g = guild_search(sd->status.guild_id)) == NULL || strcmp(g->master,sd->status.name) )
	{
		clif_displaymessage(fd, "You need to be a Guild Master to use this command.");
		return -1;
	}

	if( map[sd->bl.m].flag.guildlock )
	{
		clif_displaymessage(fd, "You cannot change guild leaders on this map.");
		return -1;
	}

	if( strlen(message) == 0 )
	{
		clif_displaymessage(fd, "Command usage: @changegm <guildmember name>");
		return -1;
	}

	if( agit_flag ) // No dejar cambiar de GM en WOE [Tab]
	{
		clif_displaymessage(fd, "No se puede cambiar de Guild Master en WOE.");
		return -1;
	}

	if( (pl_sd = map_nick2sd((char *) message)) == NULL || pl_sd->status.guild_id != sd->status.guild_id )
	{
		clif_displaymessage(fd, "Target character must be online and be a guildmate.");
		return -1;
	}

	if( !battle_config.super_woe_enable && battle_config.at_changegm_cost && sd->status.zeny < battle_config.at_changegm_cost )
	{
		memset(atcmd_output, '\0', sizeof(atcmd_output));

		sprintf(atcmd_output, "You don't have enough zeny to transfer GM. Cost %d zeny.", battle_config.at_changegm_cost);
		clif_displaymessage(fd, atcmd_output);
		return -1;
	}

	guild_gm_change(sd->status.guild_id, pl_sd);
	return 0;
}

/*==========================================
 * @changeleader by Skotlex
 * Changes the leader of a party.
 *------------------------------------------*/
int atcommand_changeleader(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	nullpo_retr(-1, sd);
	
	if (strlen(message)==0)
	{
		clif_displaymessage(fd, "Command usage: @changeleader <party member name>");
		return -1;
	}

	if (party_changeleader(sd, map_nick2sd((char *) message)))
		return 0;
	return -1;
}

/*==========================================
 * @partyoption by Skotlex
 * Used to change the item share setting of a party.
 *------------------------------------------*/
int atcommand_partyoption(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	struct party_data *p;
	int mi, option;
	char w1[16], w2[16];
	nullpo_retr(-1, sd);

	if (sd->status.party_id == 0 || (p = party_search(sd->status.party_id)) == NULL)
	{
		clif_displaymessage(fd, msg_txt(282));
		return -1;
	}

	ARR_FIND( 0, MAX_PARTY, mi, p->data[mi].sd == sd );
	if (mi == MAX_PARTY)
		return -1; //Shouldn't happen

	if (!p->party.member[mi].leader)
	{
		clif_displaymessage(fd, msg_txt(282));
		return -1;
	}

	if(!message || !*message || sscanf(message, "%15s %15s", w1, w2) < 2)
	{
		clif_displaymessage(fd, "Command usage: @partyoption <pickup share: yes/no> <item distribution: yes/no>");
		return -1;
	}
	
	option = (config_switch(w1)?1:0)|(config_switch(w2)?2:0);

	//Change item share type.
	if (option != p->party.item)
		party_changeoption(sd, p->party.exp, option);
	else
		clif_displaymessage(fd, msg_txt(286));

	return 0;
}

/*==========================================
 * @autoloot by Zephyrus
 *------------------------------------------*/
int atcommand_autoloot(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	int p = 0;
	char subcmd[100], data[100];
	memset(subcmd, '\0', sizeof(subcmd));
	memset(data, '\0', sizeof(data));

	if( !message || !*message )
	{ // Normal Autoloot usage
		if( sd->aloot.rate )
		{
			sd->aloot.rate = 0;
			clif_displaymessage(fd, msg_txt(877));
		}
		else
		{
			sd->aloot.rate = 10000;
			clif_displaymessage(fd, msg_txt(878));
		}

		clif_displaymessage(fd, msg_txt(879));
		return 0;
	}

	if( (p = sscanf(message, "%99s %99[^\n]", subcmd, data)) < 1 )
	{
		clif_displaymessage(fd, msg_txt(879));
		return -1;
	}

	if( !strcmp(subcmd, "rate") )
	{ // Set autoloot rate value
		int rate;
		if( p < 2 )
		{
			clif_displaymessage(fd, msg_txt(880));
			return -1;
		}

		rate = (int)(atof(data) * 100);
		sd->aloot.rate = cap_value(rate, 0, 10000);

		if( sd->aloot.rate )
		{
			snprintf(atcmd_output, sizeof(atcmd_output), msg_txt(881),((double)sd->aloot.rate)/100.);
			clif_displaymessage(fd, atcmd_output);
		}
		else
			clif_displaymessage(fd, msg_txt(877));
	}
	else if( !strcmp(subcmd, "clear") )
	{
		memset(&sd->aloot, 0, sizeof(sd->aloot));
		clif_displaymessage(fd, msg_txt(882));
	}
	else if( !strcmp(subcmd, "info") )
	{
		int i, c = 0;
		struct item_data *it = NULL;

		snprintf(atcmd_output, sizeof(atcmd_output), msg_txt(881),((double)sd->aloot.rate)/100.);
		clif_displaymessage(fd, atcmd_output);
		for( i = 0; i < MAX_AUTOLOOTID; i++ )
		{
			if( sd->aloot.nameid[i] == 0 || (it = itemdb_exists(sd->aloot.nameid[i])) == NULL )
				continue;

			snprintf(atcmd_output, sizeof(atcmd_output), msg_txt(883), it->jname, it->nameid);
			clif_displaymessage(fd, atcmd_output);
			c++;
		}

		snprintf(atcmd_output, sizeof(atcmd_output), msg_txt(884), c, MAX_AUTOLOOTID);
		clif_displaymessage(fd, atcmd_output);
	}
	else if( !strcmp(subcmd, "item") )
	{
		int i, j;
		struct item_data *it = NULL;

		if( p < 2 )
		{
			clif_displaymessage(fd, msg_txt(885));
			return -1;
		}

		ARR_FIND(0, MAX_AUTOLOOTID, i, sd->aloot.nameid[i] == 0);
		if( i == MAX_AUTOLOOTID )
		{
			clif_displaymessage(fd, msg_txt(886));
			return -1;
		}

		if( (it = itemdb_exists(atoi(data))) == NULL )
			it = itemdb_searchname(data);

		if( !it )
		{
			clif_displaymessage(fd, msg_txt(885));
			return -1;
		}

		ARR_FIND(0, MAX_AUTOLOOTID, j, sd->aloot.nameid[i] == it->nameid);
		if( j < MAX_AUTOLOOTID )
		{
			clif_displaymessage(fd, msg_txt(887));
			return -1;
		}

		sd->aloot.nameid[i] = it->nameid;
		snprintf(atcmd_output, sizeof(atcmd_output), msg_txt(883), it->jname, it->nameid);
		clif_displaymessage(fd, atcmd_output);
	}
	else if( !strcmp(subcmd, "help") )
	{
		clif_displaymessage(fd, msg_txt(888));
		clif_displaymessage(fd, msg_txt(889));
		clif_displaymessage(fd, msg_txt(890));
		clif_displaymessage(fd, msg_txt(891));
	}
	else
	{
		clif_displaymessage(fd, msg_txt(879));
		return -1;
	}

	return 0;
}
/*==========================================
 * @ddrop
 * Turns on/off Drop information display
 *------------------------------------------*/
int atcommand_displaydrop(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	int rate;
	double drate;
	nullpo_retr(-1, sd);

	if( !message || !*message )
	{
		if (sd->state.displaydrop)
			rate = 0;
		else
			rate = 10000;
	}
	else
	{
		drate = atof(message);
		rate = (int)(drate*100);
	}

	rate = cap_value(rate,0,10000);
	sd->state.displaydrop = rate;

	if( sd->state.displaydrop )
	{
		snprintf(atcmd_output, sizeof atcmd_output, "Displaying information about dropped items with %0.02f%% drop rate and below.",((double)sd->state.displaydrop)/100.);
		clif_displaymessage(fd, atcmd_output);
	}
	else
		clif_displaymessage(fd, "Information about dropped items is no longer displayed.");

	return 0;
}

/*==========================================
 * It is made to rain.
 *------------------------------------------*/
int atcommand_rain(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	nullpo_retr(-1, sd);
	if (map[sd->bl.m].flag.rain) {
		map[sd->bl.m].flag.rain=0;
		clif_weather(sd->bl.m);
		clif_displaymessage(fd, "The rain has stopped.");
	} else {
		map[sd->bl.m].flag.rain=1;
		clif_weather(sd->bl.m);
		clif_displaymessage(fd, "It is made to rain.");
	}
	return 0;
}

/*==========================================
 * It is made to snow.
 *------------------------------------------*/
int atcommand_snow(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	nullpo_retr(-1, sd);
	if (map[sd->bl.m].flag.snow) {
		map[sd->bl.m].flag.snow=0;
		clif_weather(sd->bl.m);
		clif_displaymessage(fd, "Snow has stopped falling.");
	} else {
		map[sd->bl.m].flag.snow=1;
		clif_weather(sd->bl.m);
		clif_displaymessage(fd, "It is made to snow.");
	}

	return 0;
}

/*==========================================
 * Cherry tree snowstorm is made to fall. (Sakura)
 *------------------------------------------*/
int atcommand_sakura(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	nullpo_retr(-1, sd);
	if (map[sd->bl.m].flag.sakura) {
		map[sd->bl.m].flag.sakura=0;
		clif_weather(sd->bl.m);
		clif_displaymessage(fd, "Cherry tree leaves no longer fall.");
	} else {
		map[sd->bl.m].flag.sakura=1;
		clif_weather(sd->bl.m);
		clif_displaymessage(fd, "Cherry tree leaves is made to fall.");
	}
	return 0;
}

/*==========================================
 * Clouds appear.
 *------------------------------------------*/
int atcommand_clouds(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	nullpo_retr(-1, sd);
	if (map[sd->bl.m].flag.clouds) {
		map[sd->bl.m].flag.clouds=0;
		clif_weather(sd->bl.m);
		clif_displaymessage(fd, "The clouds has disappear.");
	} else {
		map[sd->bl.m].flag.clouds=1;
		clif_weather(sd->bl.m);
		clif_displaymessage(fd, "Clouds appear.");
	}

	return 0;
}

/*==========================================
 * Different type of clouds using effect 516
 *------------------------------------------*/
int atcommand_clouds2(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	nullpo_retr(-1, sd);
	if (map[sd->bl.m].flag.clouds2) {
		map[sd->bl.m].flag.clouds2=0;
		clif_weather(sd->bl.m);
		clif_displaymessage(fd, "The alternative clouds disappear.");
	} else {
		map[sd->bl.m].flag.clouds2=1;
		clif_weather(sd->bl.m);
		clif_displaymessage(fd, "Alternative clouds appear.");
	}

	return 0;
}

/*==========================================
 * Fog hangs over.
 *------------------------------------------*/
int atcommand_fog(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	nullpo_retr(-1, sd);
	if (map[sd->bl.m].flag.fog) {
		map[sd->bl.m].flag.fog=0;
		clif_weather(sd->bl.m);
		clif_displaymessage(fd, "The fog has gone.");
	} else {
		map[sd->bl.m].flag.fog=1;
		clif_weather(sd->bl.m);
		clif_displaymessage(fd, "Fog hangs over.");
	}
		return 0;
}

/*==========================================
 * Fallen leaves fall.
 *------------------------------------------*/
int atcommand_leaves(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	nullpo_retr(-1, sd);
	if (map[sd->bl.m].flag.leaves) {
		map[sd->bl.m].flag.leaves=0;
		clif_weather(sd->bl.m);
		clif_displaymessage(fd, "Leaves no longer fall.");
	} else {
		map[sd->bl.m].flag.leaves=1;
		clif_weather(sd->bl.m);
		clif_displaymessage(fd, "Fallen leaves fall.");
	}

	return 0;
}

/*==========================================
 * Fireworks appear.
 *------------------------------------------*/
int atcommand_fireworks(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	nullpo_retr(-1, sd);
	if (map[sd->bl.m].flag.fireworks) {
		map[sd->bl.m].flag.fireworks=0;
		clif_weather(sd->bl.m);
		clif_displaymessage(fd, "Fireworks are ended.");
	} else {
		map[sd->bl.m].flag.fireworks=1;
		clif_weather(sd->bl.m);
		clif_displaymessage(fd, "Fireworks are launched.");
	}

	return 0;
}

/*==========================================
 * Clearing Weather Effects by Dexity
 *------------------------------------------*/
int atcommand_clearweather(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	nullpo_retr(-1, sd);
	map[sd->bl.m].flag.rain=0;
	map[sd->bl.m].flag.snow=0;
	map[sd->bl.m].flag.sakura=0;
	map[sd->bl.m].flag.clouds=0;
	map[sd->bl.m].flag.clouds2=0;
	map[sd->bl.m].flag.fog=0;
	map[sd->bl.m].flag.fireworks=0;
	map[sd->bl.m].flag.leaves=0;
	clif_weather(sd->bl.m);
	clif_displaymessage(fd, msg_txt(291));
	
	return 0;
}

/*===============================================================
 * Sound Command - plays a sound for everyone around! [Codemaster]
 *---------------------------------------------------------------*/
int atcommand_sound(const int fd, struct map_session_data *sd, const char *command, const char *message)
{
	char sound_file[100];

	memset(sound_file, '\0', sizeof(sound_file));

		if(!message || !*message || sscanf(message, "%99[^\n]", sound_file) < 1) {
		clif_displaymessage(fd, "Please, enter a sound filename. (usage: @sound <filename>)");
		return -1;
	}

	if(strstr(sound_file, ".wav") == NULL)
		strcat(sound_file, ".wav");

	clif_soundeffectall(&sd->bl, sound_file, 0, AREA);

	return 0;
}

/*==========================================
 * 	MOB Search
 *------------------------------------------*/
int atcommand_mobsearch(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	char mob_name[100];
	int mob_id;
	int number = 0;
	struct s_mapiterator* it;

	nullpo_retr(-1, sd);

	if (!message || !*message || sscanf(message, "%99[^\n]", mob_name) < 1) {
		clif_displaymessage(fd, "Please, enter a monster name (usage: @mobsearch <monster name>).");
		return -1;
	}

	if ((mob_id = atoi(mob_name)) == 0)
		 mob_id = mobdb_searchname(mob_name);
	if(mob_id > 0 && mobdb_checkid(mob_id) == 0){
		snprintf(atcmd_output, sizeof atcmd_output, "Invalid mob id %s!",mob_name);
		clif_displaymessage(fd, atcmd_output);
		return -1;
	}
	if(mob_id == atoi(mob_name) && mob_db(mob_id)->jname)
				strcpy(mob_name,mob_db(mob_id)->jname);	// --ja--
//				strcpy(mob_name,mob_db(mob_id)->name);	// --en--

	snprintf(atcmd_output, sizeof atcmd_output, "Mob Search... %s %s", mob_name, mapindex_id2name(sd->mapindex));
	clif_displaymessage(fd, atcmd_output);

	it = mapit_geteachmob();
	while( true )
	{
		TBL_MOB* md = (TBL_MOB*)mapit_next(it);
		if( md == NULL )
			break;// no more mobs

		if( md->bl.m != sd->bl.m )
			continue;
		if( mob_id != -1 && md->class_ != mob_id )
			continue;

		++number;
		if( md->spawn_timer == INVALID_TIMER )
			snprintf(atcmd_output, sizeof(atcmd_output), "%2d[%3d:%3d] %s", number, md->bl.x, md->bl.y, md->name);
		else
			snprintf(atcmd_output, sizeof(atcmd_output), "%2d[%s] %s", number, "dead", md->name);
		clif_displaymessage(fd, atcmd_output);
	}
	mapit_free(it);

	return 0;
}

/*==========================================
 * @cleanmap - cleans items on the ground
 *------------------------------------------*/
static int atcommand_cleanmap_sub(struct block_list *bl, va_list ap)
{
	nullpo_retr(0, bl);
	map_clearflooritem(bl->id);

	return 0;
}

int atcommand_cleanmap(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	map_foreachinarea(atcommand_cleanmap_sub, sd->bl.m,
		sd->bl.x-AREA_SIZE*2, sd->bl.y-AREA_SIZE*2,
		sd->bl.x+AREA_SIZE*2, sd->bl.y+AREA_SIZE*2,
		BL_ITEM);
	clif_displaymessage(fd, "All dropped items have been cleaned up.");
	return 0;
}

/*==========================================
 * make a NPC/PET talk
 * @npctalkc [SnakeDrak]
 *------------------------------------------*/
int atcommand_npctalk(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	char name[NAME_LENGTH],mes[100],temp[100];
	struct npc_data *nd;
	bool ifcolor=(*(command + 8) != 'c' && *(command + 8) != 'C')?0:1;
	unsigned long color=0;

	if (sd->sc.count && //no "chatting" while muted.
		(sd->sc.data[SC_BERSERK] ||
		(sd->sc.data[SC_NOCHAT] && sd->sc.data[SC_NOCHAT]->val1&MANNER_NOCHAT) ||
		(sd->sc.data[SC_DEEPSLEEP] && sd->sc.data[SC_DEEPSLEEP]->val2) ||
		sd->sc.data[SC_SATURDAYNIGHTFEVER]))
		return -1;

	if(!ifcolor) {
		if (!message || !*message || sscanf(message, "%23[^,], %99[^\n]", name, mes) < 2) {
			clif_displaymessage(fd, "Please, enter the correct info (usage: @npctalk <npc name>, <message>).");
			return -1;
		}
	}
	else {
		if (!message || !*message || sscanf(message, "%lx %23[^,], %99[^\n]", &color, name, mes) < 3) {
			clif_displaymessage(fd, "Please, enter the correct info (usage: @npctalkc <color> <npc name>, <message>).");
			return -1;
		}
	}

	if (!(nd = npc_name2id(name))) {
		clif_displaymessage(fd, msg_txt(111)); // This NPC doesn't exist
		return -1;
	}
	
	strtok(name, "#"); // discard extra name identifier if present
	snprintf(temp, sizeof(temp), "%s : %s", name, mes);
	
	if(ifcolor) clif_messagecolor(&nd->bl,color,temp);
	else clif_message(&nd->bl, temp);

	return 0;
}

int atcommand_pettalk(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	char mes[100],temp[100];
	struct pet_data *pd;

	nullpo_retr(-1, sd);

	if(!sd->status.pet_id || !(pd=sd->pd))
	{
		clif_displaymessage(fd, msg_txt(184));
		return -1;
	}

	if (sd->sc.count && //no "chatting" while muted.
		(sd->sc.data[SC_BERSERK] ||
		(sd->sc.data[SC_NOCHAT] && sd->sc.data[SC_NOCHAT]->val1&MANNER_NOCHAT) ||
		(sd->sc.data[SC_DEEPSLEEP] && sd->sc.data[SC_DEEPSLEEP]->val2) ||
		sd->sc.data[SC_SATURDAYNIGHTFEVER]))
		return -1;

	if (!message || !*message || sscanf(message, "%99[^\n]", mes) < 1) {
		clif_displaymessage(fd, "Please, enter a message (usage: @pettalk <message>");
		return -1;
	}

	if (message[0] == '/')
	{// pet emotion processing
		const char* emo[] = {
			"/!", "/?", "/ho", "/lv", "/swt", "/ic", "/an", "/ag", "/$", "/...",
			"/scissors", "/rock", "/paper", "/korea", "/lv2", "/thx", "/wah", "/sry", "/heh", "/swt2",
			"/hmm", "/no1", "/??", "/omg", "/O", "/X", "/hlp", "/go", "/sob", "/gg",
			"/kis", "/kis2", "/pif", "/ok", "-?-", "-?-", "/bzz", "/rice", "/awsm", "/meh",
			"/shy", "/pat", "/mp", "/slur", "/com", "/yawn", "/grat", "/hp", "/philippines", "/usa",
			"/indonesia", "/brazil", "/fsh", "/spin", "/sigh", "/dum", "/crwd", "/desp", "/dice"
		};
		int i;
		ARR_FIND( 0, ARRAYLENGTH(emo), i, stricmp(message, emo[i]) == 0 );
		if( i < ARRAYLENGTH(emo) )
		{
			clif_emotion(&pd->bl, i);
			return 0;
		}
	}

	snprintf(temp, sizeof temp ,"%s : %s", pd->pet.name, mes);
	clif_message(&pd->bl, temp);

	return 0;
}

/// @users - displays the number of players present on each map (and percentage)
/// #users displays on the target user instead of self
int atcommand_users(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	char buf[CHAT_SIZE_MAX];
	int i;
	int users[MAX_MAPINDEX];
	int users_all;
	struct s_mapiterator* iter;

	memset(users, 0, sizeof(users));
	users_all = 0;

	// count users on each map
	iter = mapit_getallusers();
	while( true )
	{
		struct map_session_data* sd2 = (struct map_session_data*)mapit_next(iter);
		if( sd2 == NULL )
			break;// no more users

		if( sd2->mapindex >= MAX_MAPINDEX )
			continue;// invalid mapindex

		if( users[sd2->mapindex] < INT_MAX ) ++users[sd2->mapindex];
		if( users_all < INT_MAX ) ++users_all;
	}
	mapit_free(iter);

	// display results for each map
	for( i = 0; i < MAX_MAPINDEX; ++i )
	{
		if( users[i] == 0 )
			continue;// empty

		safesnprintf(buf, sizeof(buf), "%s: %d (%.2f%%)", mapindex_id2name(i), users[i], (float)(100.0f*users[i]/users_all));
		clif_displaymessage(sd->fd, buf);
	}

	// display overall count
	safesnprintf(buf, sizeof(buf), "all: %d", users_all);
	clif_displaymessage(sd->fd, buf);

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
int atcommand_reset(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	pc_resetstate(sd);
	pc_resetskill(sd,1);
	sprintf(atcmd_output, msg_txt(208), sd->status.name); // '%s' skill and stats points reseted!
	clif_displaymessage(fd, atcmd_output);
	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
int atcommand_summon(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	char name[NAME_LENGTH];
	int mob_id = 0;
	int duration = 0;
	struct mob_data *md;
	unsigned int tick=gettick();

	nullpo_retr(-1, sd);

	if (!message || !*message || sscanf(message, "%23s %d", name, &duration) < 1)
	{
		clif_displaymessage(fd, "Please, enter a monster name (usage: @summon <monster name> [duration]");
		return -1;
	}

	if (duration < 1)
		duration =1;
	else if (duration > 60)
		duration =60;
	
	if ((mob_id = atoi(name)) == 0)
		mob_id = mobdb_searchname(name);
	if(mob_id == 0 || mobdb_checkid(mob_id) == 0)
	{
		clif_displaymessage(fd, msg_txt(40));	// Invalid monster ID or name.
		return -1;
	}

	md = mob_once_spawn_sub(&sd->bl, sd->bl.m, -1, -1, "--ja--", mob_id, "");

	if(!md)
		return -1;
	
	md->master_id=sd->bl.id;
	md->special_state.ai=1;
	md->deletetimer=add_timer(tick+(duration*60000),mob_timer_delete,md->bl.id,0);
	clif_misceffect2(&md->bl,344);
	mob_spawn(md);
	sc_start4(&md->bl, SC_MODECHANGE, 100, 1, 0, MD_AGGRESSIVE, 0, 60000);
	clif_skill_poseffect(&sd->bl,AM_CALLHOMUN,1,md->bl.x,md->bl.y,tick);
	clif_displaymessage(fd, msg_txt(39));	// All monster summoned!
	
	return 0;
}

/*==========================================
 * @adjcmdlvl by [MouseJstr]
 *
 * Temp adjust the GM level required to use a GM command
 * Useful during beta testing to allow players to use GM commands for short periods of time
 *------------------------------------------*/
int atcommand_adjcmdlvl(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	int newlev, newremotelev;
	char name[100];
	AtCommandInfo* cmd;

	nullpo_retr(-1, sd);

	if (!message || !*message || sscanf(message, "%d %d %99s", &newlev, &newremotelev, name) != 3)
	{
		clif_displaymessage(fd, "Usage: @adjcmdlvl <lvl> <remote lvl> <command>.");
		return -1;
	}

	cmd = get_atcommandinfo_byname(name);
	if (cmd == NULL)
	{
		clif_displaymessage(fd, "@command not found.");
		return -1;
	}
	else if (newlev > pc_isGM(sd) || newremotelev > pc_isGM(sd) )
	{
		clif_displaymessage(fd, "You can't make a command require higher GM level than your own.");
		return -1;
	}
	else if (cmd->level > pc_isGM(sd) || cmd->level2 > pc_isGM(sd) )
	{
		clif_displaymessage(fd, "You can't adjust the level of a command which's level is above your own.");
		return -1;
	}
	else
	{
		cmd->level = newlev;
		cmd->level2 = newremotelev;
		clif_displaymessage(fd, "@command level changed.");
		return 0;
	}
}

/*==========================================
 * @adjgmlvl by [MouseJstr]
 * Create a temp GM
 * Useful during beta testing to allow players to use GM commands for short periods of time
 *------------------------------------------*/
int atcommand_adjgmlvl(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	int newlev;
	char user[NAME_LENGTH];
	struct map_session_data *pl_sd;
	nullpo_retr(-1, sd);

	if (!message || !*message || sscanf(message, "%d %23[^\r\n]", &newlev, user) != 2) {
		clif_displaymessage(fd, "Usage: @adjgmlvl <lvl> <user>.");
		return -1;
	}

	if ( (pl_sd = map_nick2sd(user)) == NULL )
	{
		clif_displaymessage(fd, msg_txt(3)); // Character not found.
		return -1;
	}

	pl_sd->gmlevel = newlev;

    return 0;
}

/*==========================================
 * @trade by [MouseJstr]
 * Open a trade window with a remote player
 *------------------------------------------*/
int atcommand_trade(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
    struct map_session_data *pl_sd = NULL;
	nullpo_retr(-1, sd);

	if (!message || !*message) {
		clif_displaymessage(fd, "Please, enter a player name (usage: @trade <player>).");
		return -1;
	}

	if ( (pl_sd = map_nick2sd((char *)message)) == NULL )
	{
		clif_displaymessage(fd, msg_txt(3)); // Character not found.
		return -1;
	}

	trade_traderequest(sd, pl_sd);
	return 0;
}

/*==========================================
 * @setbattleflag by [MouseJstr]
 * set a battle_config flag without having to reboot
 *------------------------------------------*/
int atcommand_setbattleflag(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	char flag[128], value[128];
	nullpo_retr(-1, sd);

	if (!message || !*message || sscanf(message, "%127s %127s", flag, value) != 2) {
        	clif_displaymessage(fd, "Usage: @setbattleflag <flag> <value>.");
        	return -1;
    	}

	if (battle_set_value(flag, value) == 0)
	{
		clif_displaymessage(fd, "unknown battle_config flag");
		return -1;
	}

	clif_displaymessage(fd, "battle_config set as requested");

	return 0;
}

/*==========================================
 * @unmute [Valaris]
 *------------------------------------------*/
int atcommand_unmute(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	struct map_session_data *pl_sd = NULL;
	nullpo_retr(-1, sd);

	if (!message || !*message) {
		clif_displaymessage(fd, "Please, enter a player name (usage: @unmute <player>).");
		return -1;
	}

	if ( (pl_sd = map_nick2sd((char *)message)) == NULL )
	{
		clif_displaymessage(fd, msg_txt(3)); // Character not found.
		return -1;
	}

	if(!pl_sd->sc.data[SC_NOCHAT]) {
		clif_displaymessage(sd->fd,"Player is not muted");
		return -1;
	}

	pl_sd->status.manner = 0;
	status_change_end(&pl_sd->bl,SC_NOCHAT,-1);
	clif_displaymessage(sd->fd,"Player unmuted");
	
	return 0;
}

/*==========================================
 * @uptime by MC Cameri
 *------------------------------------------*/
int atcommand_uptime(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	unsigned long seconds = 0, day = 24*60*60, hour = 60*60,
		minute = 60, days = 0, hours = 0, minutes = 0;
	nullpo_retr(-1, sd);

	seconds = get_uptime();
	days = seconds/day;
	seconds -= (seconds/day>0)?(seconds/day)*day:0;
	hours = seconds/hour;
	seconds -= (seconds/hour>0)?(seconds/hour)*hour:0;
	minutes = seconds/minute;
	seconds -= (seconds/minute>0)?(seconds/minute)*minute:0;

	snprintf(atcmd_output, sizeof(atcmd_output), msg_txt(245), days, hours, minutes, seconds);
	clif_displaymessage(fd, atcmd_output);

	return 0;
}

/*==========================================
 * @changesex <sex>
 * => Changes one's sex. Argument sex can be 0 or 1, m or f, male or female.
 *------------------------------------------*/
int atcommand_changesex(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	nullpo_retr(-1, sd);
	chrif_changesex(sd);
	return 0;
}

/*================================================
 * @mute - Mutes a player for a set amount of time
 *------------------------------------------------*/
int atcommand_mute(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	struct map_session_data *pl_sd = NULL;
	int manner;
	nullpo_retr(-1, sd);

	if (!message || !*message || sscanf(message, "%d %23[^\n]", &manner, atcmd_player_name) < 1) {
		clif_displaymessage(fd, "Usage: @mute <time> <character name>.");
		return -1;
	}

	if ( (pl_sd = map_nick2sd(atcmd_player_name)) == NULL )
	{
		clif_displaymessage(fd, msg_txt(3)); // Character not found.
		return -1;
	}

	if ( pc_isGM(sd) < pc_isGM(pl_sd) )
	{
		clif_displaymessage(fd, msg_txt(81)); // Your GM level don't authorise you to do this action on this player.
		return -1;
	}

	clif_manner_message(sd, 0);
	clif_manner_message(pl_sd, 5);

	if( pl_sd->status.manner < manner ) {
		pl_sd->status.manner -= manner;
		sc_start(&pl_sd->bl,SC_NOCHAT,100,0,0);
	} else {
		pl_sd->status.manner = 0;
		status_change_end(&pl_sd->bl,SC_NOCHAT,-1);
	}

	clif_GM_silence(sd, pl_sd, (manner > 0 ? 1 : 0));

	return 0;
}

/*==========================================
 * @refresh (like @jumpto <<yourself>>)
 *------------------------------------------*/
int atcommand_refresh(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	nullpo_retr(-1, sd);

	if( sd->npc_id || sd->vender_id || sd->buyer_id || sd->state.trading || sd->state.storage_flag )
	return -1;

	clif_refresh(sd);
	return 0;
}

int atcommand_autorefresh(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	int time; // Default Min Value.
	nullpo_retr(-1,sd);

	if( sd->npc_id || sd->vender_id || sd->buyer_id || sd->state.trading || sd->state.storage_flag )
	return -1;

	if( !message || !*message )
		clif_displaymessage(fd,"Please, enter a value between 30 to 300 seconds or off to disable it. Usage @autorefresh <seconds/off>");
	else if( !strcmpi(message,"off") )
	{
		if( sd->sc.data[SC_AUTOREFRESH] )
		{
			status_change_end(&sd->bl,SC_AUTOREFRESH,-1);
			clif_displaymessage(fd,"-- AutoRefresh disabled --");
			return 0;
		}

		clif_displaymessage(fd,"AutoRefresh is not Enable.");
	}
	else if( (time = atoi(message)) < 30 || time > 300 )
		clif_displaymessage(fd,"Please, enter a value between 30 to 300 seconds");
	else
	{
		if( sd->sc.data[SC_AUTOREFRESH] )
			sprintf(atcmd_output,"Adjusting AutoRefresh to execute each %d seconds.",time);
		else
			sprintf(atcmd_output,"Starting AutoRefresh to execute each %d seconds.",time);
		
		clif_displaymessage(fd,atcmd_output);
		sc_start(&sd->bl,SC_AUTOREFRESH,100,time,-1);

		return 0;
	}

	return -1;
}

/*==========================================
 * @identify
 * => GM's magnifier.
 *------------------------------------------*/
int atcommand_identify(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	int i,num;

	nullpo_retr(-1, sd);

	for(i=num=0;i<MAX_INVENTORY;i++){
		if(sd->status.inventory[i].nameid > 0 && sd->status.inventory[i].identify!=1){
			num++;
		}
	}
	if (num > 0) {
		clif_item_identify_list(sd);
	} else {
		clif_displaymessage(fd,"There are no items to appraise.");
	}
	return 0;
}

/*==========================================
 * @gmotd (Global MOTD)
 * by davidsiaw :P
 *------------------------------------------*/
int atcommand_gmotd(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
		char buf[CHAT_SIZE_MAX];
		FILE *fp;
	nullpo_retr(-1, sd);
		if((fp = fopen(motd_txt, "r"))!=NULL){
			while(fgets(buf, sizeof(buf), fp) != NULL)
			{
				int i;
				if (buf[0] == '/' && buf[1] == '/')
					continue;
				for(i=0; buf[i]; i++){
					if(buf[i]=='\r' || buf[i]=='\n'){
						buf[i]=0;
						break;
					}
				}
				intif_broadcast(buf, strlen(buf)+1, 0);
			}
			fclose(fp);
		}
		return 0;
}

int atcommand_misceffect(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	int effect = 0;
	nullpo_retr(-1, sd);
	if (!message || !*message)
		return -1;
	if (sscanf(message, "%d", &effect) < 1)
		return -1;
	clif_misceffect(&sd->bl,effect);

	return 0;
}

/*==========================================
 * MAIL SYSTEM
 *------------------------------------------*/
int atcommand_mail(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	nullpo_retr(0,sd);
#ifndef TXT_ONLY
	mail_openmail(sd);
#endif
	return 0;
}

/*==========================================
 * Show Monster DB Info   v 1.0
 * originally by [Lupus] eAthena
 *------------------------------------------*/
int atcommand_mobinfo(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	unsigned char msize[3][7] = {"Small", "Medium", "Large"};
	unsigned char mrace[12][11] = {"Formless", "Undead", "Beast", "Plant", "Insect", "Fish", "Demon", "Demi-Human", "Angel", "Dragon", "Boss", "Non-Boss"};
	unsigned char melement[10][8] = {"Neutral", "Water", "Earth", "Fire", "Wind", "Poison", "Holy", "Dark", "Ghost", "Undead"};

	unsigned int mmodei[15] = { MD_CANMOVE, MD_LOOTER, MD_AGGRESSIVE, MD_ASSIST, MD_CASTSENSOR_IDLE, MD_BOSS, MD_PLANT, MD_CANATTACK, MD_DETECTOR, MD_CASTSENSOR_CHASE, MD_CHANGECHASE, MD_ANGRY, MD_CHANGETARGET_MELEE, MD_CHANGETARGET_CHASE, MD_TARGETWEAK };
	unsigned char mmode[15][20] = {"Can Move", "Looter", "Aggresive", "Assist", "Cast Sensor Idle", "Boss", "Plant", "Can Attack", "Detector", "Cast Sensor Chase", "Change Chase", "Angry", "Change Target Melee", "Change Target Chase", "Target Weak"};

	char atcmd_output2[CHAT_SIZE_MAX];
	struct item_data *item_data;
	struct mob_db *mob, *mob_array[MAX_SEARCH];
	int count;
	int i, j, k;

	memset(atcmd_output, '\0', sizeof(atcmd_output));
	memset(atcmd_output2, '\0', sizeof(atcmd_output2));

	if (!message || !*message) {
		clif_displaymessage(fd, "Please, enter a Monster/ID (usage: @mobinfo <monster_name_or_monster_ID>).");
		return -1;
	}

	// If monster identifier/name argument is a name
	if ((i = mobdb_checkid(atoi(message))))
	{
		mob_array[0] = mob_db(i);
		count = 1;
	} else
		count = mobdb_searchname_array(mob_array, MAX_SEARCH, message);

	if (!count) {
		clif_displaymessage(fd, msg_txt(40)); // Invalid monster ID or name.
		return -1;
	}

	if (count > MAX_SEARCH) {
		sprintf(atcmd_output, msg_txt(269), MAX_SEARCH, count);
		clif_displaymessage(fd, atcmd_output);
		count = MAX_SEARCH;
	}
	for (k = 0; k < count; k++) {
		mob = mob_array[k];

		// stats
		if (mob->mexp)
			sprintf(atcmd_output, "MVP Monster: '%s'/'%s'/'%s' (%d)", mob->name, mob->jname, mob->sprite, mob->vd.class_);
		else
			sprintf(atcmd_output, "Monster: '%s'/'%s'/'%s' (%d)", mob->name, mob->jname, mob->sprite, mob->vd.class_);
		clif_displaymessage(fd, atcmd_output);
		sprintf(atcmd_output, " Level:%d  HP:%d  SP:%d  Base EXP:%u  Job EXP:%u", mob->lv, mob->status.max_hp, mob->status.max_sp, mob->base_exp, mob->job_exp);
		clif_displaymessage(fd, atcmd_output);
		sprintf(atcmd_output, " DEF:%d-%d  MDEF:%d-%d  STR:%d  AGI:%d  VIT:%d  INT:%d  DEX:%d  LUK:%d",
			mob->status.def, mob->status.def2, mob->status.mdef, mob->status.mdef2, mob->status.str, mob->status.agi,
			mob->status.vit, mob->status.int_, mob->status.dex, mob->status.luk);
		clif_displaymessage(fd, atcmd_output);
		
		sprintf(atcmd_output, " ATK:%d~%d  Range:%d~%d~%d  Size:%s  Race: %s  Element: %s (Lv:%d)",
			mob->status.rhw.atk, mob->status.rhw.atk2, mob->status.rhw.range,
			mob->range2 , mob->range3, msize[mob->status.size],
			mrace[mob->status.race], melement[mob->status.def_ele], mob->status.ele_lv);
		clif_displaymessage(fd, atcmd_output);
		// Extra Settings
		if( mob->ffa )
			clif_displaymessage(fd, "- Free For All Monster");
		if( mob->hunting )
			clif_displaymessage(fd, "- Hunting Mission Enable Monster");
		// modes
		clif_displaymessage(fd, " Modes:");
		strcpy(atcmd_output, " ");
		j = 0;
		for( i = 0; i < 15; i++ )
		{
			if( mob->status.mode&mmodei[i] )
			{
				sprintf(atcmd_output2, " - %s", mmode[i]);
				strcat(atcmd_output, atcmd_output2);
				if( ++j % 3 == 0 )
				{
					clif_displaymessage(fd, atcmd_output);
					strcpy(atcmd_output, " ");
				}
			}
		}
		if( j % 3 != 0 ) clif_displaymessage(fd, atcmd_output);
		// drops
		clif_displaymessage(fd, " Drops:");
		strcpy(atcmd_output, " ");
		j = 0;
		for (i = 0; i < MAX_MOB_DROP; i++) {
			if (mob->dropitem[i].nameid <= 0 || mob->dropitem[i].p < 1 || (item_data = itemdb_search(mob->dropitem[i].nameid)) == NULL)
				continue;
			if (item_data->slot)
				sprintf(atcmd_output2, " - %s[%d]  %02.02f%%", item_data->jname, item_data->slot, (float)mob->dropitem[i].p / 100);
			else
				sprintf(atcmd_output2, " - %s  %02.02f%%", item_data->jname, (float)mob->dropitem[i].p / 100);
			strcat(atcmd_output, atcmd_output2);
			if (++j % 3 == 0) {
				clif_displaymessage(fd, atcmd_output);
				strcpy(atcmd_output, " ");
			}
		}
		if (j == 0)
			clif_displaymessage(fd, "This monster has no drops.");
		else if (j % 3 != 0)
			clif_displaymessage(fd, atcmd_output);
		// mvp
		if (mob->mexp) {
			sprintf(atcmd_output, " MVP Bonus EXP:%u  %02.02f%%", mob->mexp, (float)mob->mexpper / 100);
			clif_displaymessage(fd, atcmd_output);
			strcpy(atcmd_output, " MVP Items:");
			j = 0;
			for (i = 0; i < 3; i++) {
				if (mob->mvpitem[i].nameid <= 0 || (item_data = itemdb_search(mob->mvpitem[i].nameid)) == NULL)
					continue;
				if (mob->mvpitem[i].p > 0) {
					j++;
					if (j == 1)
						sprintf(atcmd_output2, " %s  %02.02f%%", item_data->name, (float)mob->mvpitem[i].p / 100);
					else
						sprintf(atcmd_output2, " - %s  %02.02f%%", item_data->name, (float)mob->mvpitem[i].p / 100);
					strcat(atcmd_output, atcmd_output2);
				}
			}
			if (j == 0)
				clif_displaymessage(fd, "This monster has no MVP prizes.");
			else
				clif_displaymessage(fd, atcmd_output);
		}
	}
	return 0;
}

/*=========================================
* @showmobs by KarLaeda
* => For 5 sec displays the mobs on minimap
*------------------------------------------*/
int atshowmobs_timer(int tid, unsigned int tick, int id, intptr data)
{
	struct map_session_data* sd = map_id2sd(id);
	if( sd == NULL )
		return 0;

	// remove indicator
	clif_viewpoint(sd, 1, 2, 0, 0, (int)data, 0xFFFFFF);
	return 1;
}

int atcommand_showmobs(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	char mob_name[100];
	int mob_id;
	int number = 0;
	struct s_mapiterator* it;

	nullpo_retr(-1, sd);

	if(sscanf(message, "%99[^\n]", mob_name) < 0)
		return -1;

	if((mob_id = atoi(mob_name)) == 0)
		mob_id = mobdb_searchname(mob_name);
	if(mob_id > 0 && mobdb_checkid(mob_id) == 0){
		snprintf(atcmd_output, sizeof atcmd_output, "Invalid mob id %s!",mob_name);
		clif_displaymessage(fd, atcmd_output);
		return 0;
	}
// Uncomment the following line to show mini-bosses & MVP.
//#define SHOW_MVP
#ifndef SHOW_MVP
	if(mob_db(mob_id)->status.mode&MD_BOSS){
		snprintf(atcmd_output, sizeof atcmd_output, "Can't show Boss mobs!");
		clif_displaymessage(fd, atcmd_output);
		return 0;
	}
#endif
	if(mob_id == atoi(mob_name) && mob_db(mob_id)->jname)
		strcpy(mob_name,mob_db(mob_id)->jname);    // --ja--
		//strcpy(mob_name,mob_db(mob_id)->name);    // --en--

	snprintf(atcmd_output, sizeof atcmd_output, "Mob Search... %s %s",
		mob_name, mapindex_id2name(sd->mapindex));
	clif_displaymessage(fd, atcmd_output);

	it = mapit_geteachmob();
	while( true )
	{
		TBL_MOB* md = (TBL_MOB*)mapit_next(it);
		if( md == NULL )
			break;// no more mobs

		if( md->bl.m != sd->bl.m )
			continue;
		if( mob_id != -1 && md->class_ != mob_id )
			continue;
		if( md->special_state.ai || md->master_id )
			continue; // hide slaves and player summoned mobs
		if( md->spawn_timer != INVALID_TIMER )
			continue; // hide mobs waiting for respawn

		++number;
		clif_viewpoint(sd, 1, 1, md->bl.x, md->bl.y, number, 0xFFFFFF);
		add_timer(gettick()+5000, atshowmobs_timer, sd->bl.id, number);
	}
	mapit_free(it);

	return 0;
}

/*==========================================
 * homunculus level up [orn]
 *------------------------------------------*/
int atcommand_homlevel(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	TBL_HOM * hd;
	int level = 0, i = 0;

	nullpo_retr(-1, sd);

	if (!message || !*message) {
		clif_displaymessage(fd, "Please, enter a level adjustment: (usage: @homlevel <+/- # of levels>.");
		return -1;
	}
		
	if ( !merc_is_hom_active(sd->hd) ) {
		clif_displaymessage(fd, "You do not have a homunculus.");
		return -1;
	}

	level = atoi(message);
	hd = sd->hd;
	
	for (i = 1; i <= level && hd->exp_next; i++){
		hd->homunculus.exp += hd->exp_next;
		merc_hom_levelup(hd);
	}
	status_calc_homunculus(hd,0);
	status_percent_heal(&hd->bl, 100, 100);
	clif_misceffect2(&hd->bl,568);
	return 0;
}

/*==========================================
 * homunculus evolution H [orn]
 *------------------------------------------*/
int atcommand_homevolution(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	nullpo_retr(-1, sd);

	if ( !merc_is_hom_active(sd->hd) ) {
		clif_displaymessage(fd, "You do not have a homunculus.");
		return -1;
	}

	if ( !merc_hom_evolution(sd->hd) ) {
		clif_displaymessage(fd, "Your homunculus doesn't evolve.");
		return -1;
	}

	return 0;
}

/*==========================================
 * call choosen homunculus [orn]
 *------------------------------------------*/
int atcommand_makehomun(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	int homunid;
	nullpo_retr(-1, sd);

	if ( merc_is_hom_active(sd->hd) ) {
		clif_displaymessage(fd, msg_txt(450));
		return -1;
	}

	if (!message || !*message) {
		clif_displaymessage(fd, "Please, enter a homunculus id: (usage: @makehomun <homunculus id>.");
		return -1;
	}

	homunid = atoi(message);
	if( homunid < HM_CLASS_BASE || homunid > HM_CLASS_BASE + MAX_HOMUNCULUS_CLASS - 1 )
	{
		clif_displaymessage(fd, "Invalid Homunculus id.");
		return -1;
	}

	merc_create_homunculus_request(sd,homunid);
	return 0;
}

/*==========================================
 * modify homunculus intimacy [orn]
 *------------------------------------------*/
int atcommand_homfriendly(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	int friendly = 0;

	nullpo_retr(-1, sd);

	if ( !merc_is_hom_active(sd->hd) ) {
		clif_displaymessage(fd, "You do not have a homunculus.");
		return -1;
	}

	if (!message || !*message) {
		clif_displaymessage(fd, "Please, enter a friendly value: (usage: @homfriendly <friendly value[0-1000]>.");
		return -1;
	}

	friendly = atoi(message);
	friendly = cap_value(friendly, 0, 1000);

	sd->hd->homunculus.intimacy = friendly * 100 ;
	clif_send_homdata(sd,SP_INTIMATE,friendly);
	return 0;
}

/*==========================================
 * modify homunculus hunger [orn]
 *------------------------------------------*/
int atcommand_homhungry(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	int hungry = 0;

	nullpo_retr(-1, sd);

	if ( !merc_is_hom_active(sd->hd) ) {
		clif_displaymessage(fd, "You do not have a homunculus.");
		return -1;
	}

	if (!message || !*message) {
		clif_displaymessage(fd, "Please, enter a hunger value: (usage: @homhungry <hunger value[0-100]>.");
		return -1;
	}

	hungry = atoi(message);
	hungry = cap_value(hungry, 0, 100);

	sd->hd->homunculus.hunger = hungry;
	clif_send_homdata(sd,SP_HUNGRY,hungry);
	return 0;
}

/*==========================================
 * make the homunculus speak [orn]
 *------------------------------------------*/
int atcommand_homtalk(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	char mes[100],temp[100];

	nullpo_retr(-1, sd);

	if (sd->sc.count && //no "chatting" while muted.
		(sd->sc.data[SC_BERSERK] ||
		(sd->sc.data[SC_NOCHAT] && sd->sc.data[SC_NOCHAT]->val1&MANNER_NOCHAT) ||
		(sd->sc.data[SC_DEEPSLEEP] && sd->sc.data[SC_DEEPSLEEP]->val2) ||
		sd->sc.data[SC_SATURDAYNIGHTFEVER]))
		return -1;

	if ( !merc_is_hom_active(sd->hd) ) {
		clif_displaymessage(fd, "You do not have a homunculus.");
		return -1;
	}

	if (!message || !*message || sscanf(message, "%99[^\n]", mes) < 1) {
		clif_displaymessage(fd, "Please, enter a message (usage: @homtalk <message>");
		return -1;
	}

	snprintf(temp, sizeof temp ,"%s : %s", sd->hd->homunculus.name, mes);
	clif_message(&sd->hd->bl, temp);

	return 0;
}

/*==========================================
 * Show homunculus stats
 *------------------------------------------*/
int atcommand_hominfo(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	struct homun_data *hd;
	struct status_data *status;
	nullpo_retr(-1, sd);

	if ( !merc_is_hom_active(sd->hd) ) {
		clif_displaymessage(fd, "You do not have a homunculus.");
		return -1;
	}

	hd = sd->hd;
	status = status_get_status_data(&hd->bl);
	clif_displaymessage(fd, "Homunculus stats :");

	snprintf(atcmd_output, sizeof(atcmd_output) ,"HP : %d/%d - SP : %d/%d",
		status->hp, status->max_hp, status->sp, status->max_sp);
	clif_displaymessage(fd, atcmd_output);

	snprintf(atcmd_output, sizeof(atcmd_output) ,"ATK : %d - MATK : %d~%d",
		status->rhw.atk2 + status->batk, status_calc_matk_min(&hd->bl,status), status_calc_matk_max(&hd->bl,status));
	clif_displaymessage(fd, atcmd_output);

	snprintf(atcmd_output, sizeof(atcmd_output) ,"Hungry : %d - Intimacy : %u",
		hd->homunculus.hunger, hd->homunculus.intimacy/100);
	clif_displaymessage(fd, atcmd_output);

	snprintf(atcmd_output, sizeof(atcmd_output) ,
		"Stats: Str %d / Agi %d / Vit %d / Int %d / Dex %d / Luk %d",
		status->str, status->agi, status->vit,
		status->int_, status->dex, status->luk);
	clif_displaymessage(fd, atcmd_output);

	return 0;
}

int atcommand_homstats(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	struct homun_data *hd;
	struct s_homunculus_db *db;
	struct s_homunculus *hom;
	int lv, min, max, evo;

	nullpo_retr(-1, sd);

	if ( !merc_is_hom_active(sd->hd) ) {
		clif_displaymessage(fd, "You do not have a homunculus.");
		return -1;
	}

	hd = sd->hd;
	
	hom = &hd->homunculus;
	db = hd->homunculusDB;
	lv = hom->level;

	snprintf(atcmd_output, sizeof(atcmd_output) ,
		"Homunculus growth stats (Lv %d %s):", lv, db->name);
	clif_displaymessage(fd, atcmd_output);
	lv--; //Since the first increase is at level 2.
	
	evo = (hom->class_ == db->evo_class);
	min = db->base.HP +lv*db->gmin.HP +(evo?db->emin.HP:0);
	max = db->base.HP +lv*db->gmax.HP +(evo?db->emax.HP:0);;
	snprintf(atcmd_output, sizeof(atcmd_output) ,"Max HP: %d (%d~%d)", hom->max_hp, min, max);
	clif_displaymessage(fd, atcmd_output);

	min = db->base.SP +lv*db->gmin.SP +(evo?db->emin.SP:0);
	max = db->base.SP +lv*db->gmax.SP +(evo?db->emax.SP:0);;
	snprintf(atcmd_output, sizeof(atcmd_output) ,"Max SP: %d (%d~%d)", hom->max_sp, min, max);
	clif_displaymessage(fd, atcmd_output);

	min = db->base.str +lv*(db->gmin.str/10) +(evo?db->emin.str:0);
	max = db->base.str +lv*(db->gmax.str/10) +(evo?db->emax.str:0);;
	snprintf(atcmd_output, sizeof(atcmd_output) ,"Str: %d (%d~%d)", hom->str/10, min, max);
	clif_displaymessage(fd, atcmd_output);

	min = db->base.agi +lv*(db->gmin.agi/10) +(evo?db->emin.agi:0);
	max = db->base.agi +lv*(db->gmax.agi/10) +(evo?db->emax.agi:0);;
	snprintf(atcmd_output, sizeof(atcmd_output) ,"Agi: %d (%d~%d)", hom->agi/10, min, max);
	clif_displaymessage(fd, atcmd_output);

	min = db->base.vit +lv*(db->gmin.vit/10) +(evo?db->emin.vit:0);
	max = db->base.vit +lv*(db->gmax.vit/10) +(evo?db->emax.vit:0);;
	snprintf(atcmd_output, sizeof(atcmd_output) ,"Vit: %d (%d~%d)", hom->vit/10, min, max);
	clif_displaymessage(fd, atcmd_output);

	min = db->base.int_ +lv*(db->gmin.int_/10) +(evo?db->emin.int_:0);
	max = db->base.int_ +lv*(db->gmax.int_/10) +(evo?db->emax.int_:0);;
	snprintf(atcmd_output, sizeof(atcmd_output) ,"Int: %d (%d~%d)", hom->int_/10, min, max);
	clif_displaymessage(fd, atcmd_output);

	min = db->base.dex +lv*(db->gmin.dex/10) +(evo?db->emin.dex:0);
	max = db->base.dex +lv*(db->gmax.dex/10) +(evo?db->emax.dex:0);;
	snprintf(atcmd_output, sizeof(atcmd_output) ,"Dex: %d (%d~%d)", hom->dex/10, min, max);
	clif_displaymessage(fd, atcmd_output);

	min = db->base.luk +lv*(db->gmin.luk/10) +(evo?db->emin.luk:0);
	max = db->base.luk +lv*(db->gmax.luk/10) +(evo?db->emax.luk:0);;
	snprintf(atcmd_output, sizeof(atcmd_output) ,"Luk: %d (%d~%d)", hom->luk/10, min, max);
	clif_displaymessage(fd, atcmd_output);

	return 0;
}

int atcommand_homshuffle(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	nullpo_retr(-1, sd);

	if(!sd->hd)
		return -1; // nothing to do

	if(!merc_hom_shuffle(sd->hd))
		return -1;

	clif_displaymessage(sd->fd, "[Homunculus Stats Altered]");
	atcommand_homstats(fd, sd, command, message); //Print out the new stats
	return 0;
}

/*==========================================
 * Show Items DB Info   v 1.0
 * originally by [Lupus] eAthena
 *------------------------------------------*/
int atcommand_iteminfo(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	char *itype[12] = {"Potion/Food", "BUG!", "Usable", "Etc", "Weapon", "Protection", "Card", "Egg", "Pet Acessory", "BUG!", "Arrow"};
	//, "Lure/Scroll"}; No need, type 11 items are converted to type 2 upon loading [Skotlex]

	struct item_data *item_data, *item_array[MAX_SEARCH];
	int i, count = 1;

	if (!message || !*message) {
		clif_displaymessage(fd, "Please, enter Item name or its ID (usage: @ii/@iteminfo <item name or ID>).");
		return -1;
	}
	if ((item_array[0] = itemdb_exists(atoi(message))) == NULL)
		count = itemdb_searchname_array(item_array, MAX_SEARCH, message);

	if (!count) {
		clif_displaymessage(fd, msg_txt(19));	// Invalid item ID or name.
		return -1;
	}

	if (count > MAX_SEARCH) {
		sprintf(atcmd_output, msg_txt(269), MAX_SEARCH, count); // Displaying first %d out of %d matches
		clif_displaymessage(fd, atcmd_output);
		count = MAX_SEARCH;
	}
	for (i = 0; i < count; i++) {
		item_data = item_array[i];
		sprintf(atcmd_output, "Item %d : '%s'[%d] | %s | Script : %s | Ancient : %s",
			item_data->nameid, item_data->jname, item_data->slot,
			item_data->type < 12 ? itype[item_data->type] : "BUG!",
			(item_data->script == NULL) ? "No" : "Yes",
			item_data->ancient ? "Yes" : "No"
		);
		clif_displaymessage(fd, atcmd_output);

		sprintf(atcmd_output, "NPC Buy:%dz%s, Sell:%dz%s | Weight: %.1f ", item_data->value_buy, item_data->flag.value_notdc ? "(No Discount!)":"", item_data->value_sell, item_data->flag.value_notoc ? "(No Overcharge!)":"", item_data->weight/10. );
		clif_displaymessage(fd, atcmd_output);

		if (item_data->maxchance == 10000)
			strcpy(atcmd_output, " - Available in the shops only");
		else if (item_data->maxchance)
			sprintf(atcmd_output, " - Maximal monsters drop chance: %02.02f%%", (float)item_data->maxchance / 100 );
		else
			strcpy(atcmd_output, " - Monsters don't drop this item");
		clif_displaymessage(fd, atcmd_output);

	}
	return 0;
}

/*==========================================
 * Show who drops the item.
 *------------------------------------------*/
int atcommand_whodrops(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	struct item_data *item_data, *item_array[MAX_SEARCH];
	int i,j, count = 1;

	if (!message || !*message) {
		clif_displaymessage(fd, "Please, enter Item name or its ID (usage: @whodrops <item name or ID>).");
		return -1;
	}
	if ((item_array[0] = itemdb_exists(atoi(message))) == NULL)
		count = itemdb_searchname_array(item_array, MAX_SEARCH, message);

	if (!count) {
		clif_displaymessage(fd, msg_txt(19));	// Invalid item ID or name.
		return -1;
	}

	if (count > MAX_SEARCH) {
		sprintf(atcmd_output, msg_txt(269), MAX_SEARCH, count); // Displaying first %d out of %d matches
		clif_displaymessage(fd, atcmd_output);
		count = MAX_SEARCH;
	}
	for( i = 0; i < count; i++ )
	{
		item_data = item_array[i];
		sprintf(atcmd_output, "Item: '%s'[%d]", item_data->jname,item_data->slot);
		clif_displaymessage(fd, atcmd_output);

		if( item_data->mob[0].chance == 0 )
		{
			strcpy(atcmd_output, " - Item is not dropped by mobs.");
			clif_displaymessage(fd, atcmd_output);
		}
		else
		{
			sprintf(atcmd_output, "- Common mobs with highest drop chance (only max %d are listed):", MAX_SEARCH);
			clif_displaymessage(fd, atcmd_output);
		
			for( j = 0; j < MAX_SEARCH && item_data->mob[j].chance > 0; j++ )
			{
				sprintf(atcmd_output, "- %s (%02.02f%%) %02.02f%% Base -", mob_db(item_data->mob[j].id)->jname, item_data->mob[j].chance/100., item_data->mob[j].bchance/100.);
				clif_displaymessage(fd, atcmd_output);
			}
		}
	}
	return 0;
}

int atcommand_whereis(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	struct mob_db *mob, *mob_array[MAX_SEARCH];
	int count;
	int i, j, k;

	if (!message || !*message) {
		clif_displaymessage(fd, "Please, enter a Monster/ID (usage: @whereis<monster_name_or_monster_ID>).");
		return -1;
	}

	// If monster identifier/name argument is a name
	if ((i = mobdb_checkid(atoi(message))))
	{
		mob_array[0] = mob_db(i);
		count = 1;
	} else
		count = mobdb_searchname_array(mob_array, MAX_SEARCH, message);

	if (!count) {
		clif_displaymessage(fd, msg_txt(40)); // Invalid monster ID or name.
		return -1;
	}

	if (count > MAX_SEARCH) {
		sprintf(atcmd_output, msg_txt(269), MAX_SEARCH, count);
		clif_displaymessage(fd, atcmd_output);
		count = MAX_SEARCH;
	}
	for (k = 0; k < count; k++) {
		mob = mob_array[k];
		snprintf(atcmd_output, sizeof atcmd_output, "%s spawns in:", mob->jname);
		clif_displaymessage(fd, atcmd_output);

		for (i = 0; i < ARRAYLENGTH(mob->spawn) && mob->spawn[i].qty; i++)
		{
			j = map_mapindex2mapid(mob->spawn[i].mapindex);
			if (j < 0) continue;
			snprintf(atcmd_output, sizeof atcmd_output, "%s (%d)", map[j].name, mob->spawn[i].qty);
			clif_displaymessage(fd, atcmd_output);
		}
		if (i == 0)
			clif_displaymessage(fd, "This monster does not spawn normally.");
	}

	return 0;
}

/*==========================================
 * @wherelevel <level>
 * To display a list of monsters of this Level
 *------------------------------------------*/
int atcommand_wherelevel(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	struct mob_db *mob, *mob_array[MAX_SEARCH];
	int count;
	int lv, i, m, k, mod = 100;
	unsigned int base_exp, job_exp, next_base, next_job;
	float next_base_p = 0, next_job_p = 0;

	if( !message || !*message )
	{
		clif_displaymessage(fd, "Please, enter a Monster search Level (usage: @wherelevel <level>).");
		return -1;
	}

	lv = atoi(message);
	if( lv < 1 || lv > 161 )
	{
		clif_displaymessage(fd, "Invalid monster level. Search values are from 1 to 160.");
		return -1;
	}

	count = mobdb_searchlevel_array(mob_array, MAX_SEARCH, lv);
	if( count == 0 )
	{
		clif_displaymessage(fd, "No results found...");
		return -1;
	}
	if( count > MAX_SEARCH )
	{
		sprintf(atcmd_output, msg_txt(269), MAX_SEARCH, count);
		clif_displaymessage(fd, atcmd_output);
		count = MAX_SEARCH;
	}

	// Experience Mod Calculation
	if( battle_config.renewal_system_enable )
	{
		short diff = lv - sd->status.base_level + 31; // Set it to starting supported index
		short exp_mod[] = { 
			 10,                     // ... ~ -31
			 30,  30,  30,  30,  30, // -30 ~ -26
			 60,  60,  60,  60,  60, // -25 ~ -21
			 85,  85,  85,  85,  85, // -20 ~ -16
			 90,  90,  90,  90,  90, // -15 ~ -11
			 95,  95,  95,  95,  95, // -10 ~  -6
			100, 100, 100, 100, 100, //  -5 ~  -1
			100, 100, 100, 105, 110, //   0 ~   4
			115, 120, 125, 130, 135, //   5 ~   9
			140, 135, 130, 125, 120, //  10 ~  14
			115,  40,  40,  40,  40, //  15 ~  19
		};

		diff = cap_value(diff,0,50);
		mod = exp_mod[diff];

		sprintf(atcmd_output, "-- Renewal Exp Modification is %d%% for Monsters of Level %d --", mod, lv);
		clif_displaymessage(fd, atcmd_output);
	}

	next_base = pc_nextbaseexp(sd);
	next_job = pc_nextjobexp(sd);

	for( k = 0; k < count; k++ )
	{
		mob = mob_array[k];
		// Calculate how much do you receive...
		if( mob->mexp > 0 )
		{
			base_exp = mob->base_exp;
			job_exp = mob->job_exp;
		}
		else
		{
			base_exp = (unsigned int)cap_value(mob->base_exp * mod / 100,1,UINT_MAX);
			job_exp = (unsigned int)cap_value(mob->job_exp * mod / 100,1,UINT_MAX);
		}
		// Calculates how much percent this means for your current level
		if( next_base > 0 ) next_base_p = (float)base_exp / (float)next_base;
		if( next_base_p > 1. ) next_base_p = 1.;
		if( next_job > 0 ) next_job_p = (float)job_exp / (float)next_job;
		if( next_job_p > 1. ) next_job_p = 1.;
		// Prepares information
		snprintf(atcmd_output, sizeof(atcmd_output), "%s [Exp %u (%.2f%%) Job %u (%.2f%%)] in:", mob->jname, base_exp, next_base_p*(float)100, job_exp, next_job_p*(float)100);
		clif_displaymessage(fd, atcmd_output);

		for( i = 0; i < ARRAYLENGTH(mob->spawn) && mob->spawn[i].qty; i++ )
		{
			m = map_mapindex2mapid(mob->spawn[i].mapindex);
			if( m < 0 ) continue;
			snprintf(atcmd_output, sizeof(atcmd_output), "----> %s (%d)", map[m].name, mob->spawn[i].qty);
			clif_displaymessage(fd, atcmd_output);
		}
	}

	return 0;
}

/*==========================================
 * @adopt by [Veider]
 * adopt a novice
 *------------------------------------------*/
int atcommand_adopt(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	struct map_session_data *pl_sd1, *pl_sd2, *pl_sd3;
	char player1[NAME_LENGTH], player2[NAME_LENGTH], player3[NAME_LENGTH];
	char output[CHAT_SIZE_MAX];

	nullpo_retr(-1, sd);

	if (!message || !*message || sscanf(message, "%23[^,],%23[^,],%23[^\r\n]", player1, player2, player3) < 3) {
		clif_displaymessage(fd, "usage: @adopt <father>,<mother>,<child>.");
		return -1;
	}

	if (battle_config.etc_log)
		ShowInfo("Adopting: --%s--%s--%s--\n",player1,player2,player3);

	if((pl_sd1=map_nick2sd((char *) player1)) == NULL) {
		sprintf(output, "Cannot find player %s online", player1);
		clif_displaymessage(fd, output);
		return -1;
	}

	if((pl_sd2=map_nick2sd((char *) player2)) == NULL) {
		sprintf(output, "Cannot find player %s online", player2);
		clif_displaymessage(fd, output);
		return -1;
	}
 
	if((pl_sd3=map_nick2sd((char *) player3)) == NULL) {
		sprintf(output, "Cannot find player %s online", player3);
		clif_displaymessage(fd, output);
		return -1;
	}

	if( !pc_adoption(pl_sd1, pl_sd2, pl_sd3) ) {
		return -1;
	}
	
	clif_displaymessage(fd, "They are family... wish them luck");
	return 0;
}

int atcommand_version(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	const char * revision;

	if ((revision = get_svn_revision()) != 0) {
		sprintf(atcmd_output,"eAthena Version SVN r%s",revision);
		clif_displaymessage(fd,atcmd_output);
	} else 
		clif_displaymessage(fd,"Cannot determine SVN revision");

	return 0;
}

/*==========================================
 * @mutearea by MouseJstr
 *------------------------------------------*/
static int atcommand_mutearea_sub(struct block_list *bl,va_list ap)
{
	
	int time, id;
	struct map_session_data *pl_sd = (struct map_session_data *)bl;
	if (pl_sd == NULL)
		return 0;

	id = va_arg(ap, int);
	time = va_arg(ap, int);

	if (id != bl->id && !pc_isGM(pl_sd)) {
		pl_sd->status.manner -= time;
		if (pl_sd->status.manner < 0)
			sc_start(&pl_sd->bl,SC_NOCHAT,100,0,0);
		else if (pl_sd->sc.data[SC_NOCHAT])
			status_change_end(&pl_sd->bl, SC_NOCHAT, -1);
	}
	return 0;
}

int atcommand_mutearea(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	int time;
	nullpo_retr(0, sd);

	if (!message || !*message) {
		clif_displaymessage(fd, "Please, enter a time in minutes (usage: @mutearea/@stfu <time in minutes>.");
		return -1;
	}
	
	time = atoi(message);

	map_foreachinarea(atcommand_mutearea_sub,sd->bl.m, 
		sd->bl.x-AREA_SIZE, sd->bl.y-AREA_SIZE, 
		sd->bl.x+AREA_SIZE, sd->bl.y+AREA_SIZE, BL_PC, sd->bl.id, time);

	return 0;
}


int atcommand_rates(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	char buf[CHAT_SIZE_MAX];
	
	nullpo_ret(sd);
	memset(buf, '\0', sizeof(buf));
	
	snprintf(buf, CHAT_SIZE_MAX, "Experience rates: Base %.2fx / Job %.2fx",
		battle_config.base_exp_rate/100., battle_config.job_exp_rate/100.);
	clif_displaymessage(fd, buf);
	snprintf(buf, CHAT_SIZE_MAX, "Normal Drop Rates: Common %.2fx / Healing %.2fx / Usable %.2fx / Equipment %.2fx / Card %.2fx",
		battle_config.item_rate_common/100., battle_config.item_rate_heal/100., battle_config.item_rate_use/100., battle_config.item_rate_equip/100., battle_config.item_rate_card/100.);
	clif_displaymessage(fd, buf);
	snprintf(buf, CHAT_SIZE_MAX, "Boss Drop Rates: Common %.2fx / Healing %.2fx / Usable %.2fx / Equipment %.2fx / Card %.2fx",
		battle_config.item_rate_common_boss/100., battle_config.item_rate_heal_boss/100., battle_config.item_rate_use_boss/100., battle_config.item_rate_equip_boss/100., battle_config.item_rate_card_boss/100.);
	clif_displaymessage(fd, buf);
	snprintf(buf, CHAT_SIZE_MAX, "Other Drop Rates: MvP %.2fx / Card-Based %.2fx / Treasure %.2fx",
		battle_config.item_rate_mvp/100., battle_config.item_rate_adddrop/100., battle_config.item_rate_treasure/100.);
	clif_displaymessage(fd, buf);
	
	return 0;
}

/*==========================================
 * @me by lordalfa
 * => Displays the OUTPUT string on top of the Visible players Heads.
 *------------------------------------------*/
int atcommand_me(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	char tempmes[CHAT_SIZE_MAX];
	nullpo_retr(-1, sd);

	memset(tempmes, '\0', sizeof(tempmes));
	memset(atcmd_output, '\0', sizeof(atcmd_output));

	if (sd->sc.count && //no "chatting" while muted.
		(sd->sc.data[SC_BERSERK] ||
		(sd->sc.data[SC_NOCHAT] && sd->sc.data[SC_NOCHAT]->val1&MANNER_NOCHAT) ||
		(sd->sc.data[SC_DEEPSLEEP] && sd->sc.data[SC_DEEPSLEEP]->val2) ||
		sd->sc.data[SC_SATURDAYNIGHTFEVER]))
		return -1;

	if (!message || !*message || sscanf(message, "%199[^\n]", tempmes) < 0) {
		clif_displaymessage(fd, "Please, enter a message (usage: @me <message>).");
		return -1;
	}
	
	sprintf(atcmd_output, msg_txt(270), sd->status.name, tempmes);	// *%s %s*
	clif_disp_overhead(sd, atcmd_output);
	
	return 0;
	
}

/*==========================================
 * @size
 * => Resize your character sprite. [Valaris]
 *------------------------------------------*/
int atcommand_size(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	int size=0;

	nullpo_retr(-1, sd);

	size = atoi(message);
	if(sd->state.size) {
		sd->state.size=0;
		pc_setpos(sd, sd->mapindex, sd->bl.x, sd->bl.y, 3);
	}

	if(size==1) {
		sd->state.size=1;
		clif_specialeffect(&sd->bl,420,AREA);
	} else if(size==2) {
		sd->state.size=2;
		clif_specialeffect(&sd->bl,422,AREA);
	}

	return 0;
}

int atcommand_sizeall(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	int size;
	struct map_session_data *pl_sd;
	struct s_mapiterator* iter;

	size = atoi(message);
	size = cap_value(size,0,2);

	global_size = size;
	iter = mapit_getallusers();
	for( pl_sd = (TBL_PC*)mapit_first(iter); mapit_exists(iter); pl_sd = (TBL_PC*)mapit_next(iter) )
	{
		if( pl_sd->state.size != size )
		{
			if( pl_sd->state.size )
			{
				pl_sd->state.size = 0;
				pc_setpos(pl_sd, pl_sd->mapindex, pl_sd->bl.x, pl_sd->bl.y, 3);
			}

			pl_sd->state.size = size;
			if( size == 1 )
				clif_specialeffect(&pl_sd->bl,420,AREA);
			else if( size == 2 )
				clif_specialeffect(&pl_sd->bl,422,AREA);
		}
	}
	mapit_free(iter);
	clif_displaymessage(fd, "Default Server character size set...");

	return 0;
}

/*==========================================
 * @monsterignore
 * => Makes monsters ignore you. [Valaris]
 *------------------------------------------*/
int atcommand_monsterignore(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	nullpo_retr(-1, sd);

	if (!sd->state.monster_ignore) {
		sd->state.monster_ignore = 1;
		clif_displaymessage(sd->fd, "You are now immune to attacks.");
	} else {
		sd->state.monster_ignore = 0;
		clif_displaymessage(sd->fd, "Returned to normal state.");
	}

	return 0;
}
/*==========================================
 * @fakename
 * => Gives your character a fake name. [Valaris]
 *------------------------------------------*/
int atcommand_fakename(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	char name[NAME_LENGTH];
	nullpo_retr(-1, sd);
	
	if((!message || !*message) && strlen(sd->fakename) > 1) {
		sd->fakename[0]='\0';
		clif_charnameack(NULL, &sd->bl);
		clif_displaymessage(sd->fd,"Returned to real name.");
		return 0;
	}

	if (!message || !*message || sscanf(message, "%23[^\n]", name) < 1) {
		clif_displaymessage(sd->fd,"You must enter a name.");
		return -1;
	}

	if(strlen(name) < 2) {
		clif_displaymessage(sd->fd,"Fake name must be at least two characters.");
		return -1;
	}
	
	memcpy(sd->fakename,name,NAME_LENGTH);
	sd->fakename[NAME_LENGTH-1] = '\0';
	clif_charnameack(NULL, &sd->bl);
	clif_displaymessage(sd->fd,"Fake name enabled.");
	
	return 0;
}

/*==========================================
 * @mapflag [flag name] [1|0|on|off] [map name] by Lupus
 * => Shows information about the map flags [map name]
 * Also set flags
 *------------------------------------------*/
int atcommand_mapflag(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
// WIP
	return 0;
}

/*===================================
 * Remove some messages
 *-----------------------------------*/
int atcommand_showexp(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	if (sd->state.showexp) {
		sd->state.showexp = 0;
		clif_displaymessage(fd, "Gained exp will not be shown.");
		return 0;
	}

	sd->state.showexp = 1;
	clif_displaymessage(fd, "Gained exp is now shown");
	return 0;
}

int atcommand_showzeny(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	if (sd->state.showzeny) {
		sd->state.showzeny = 0;
		clif_displaymessage(fd, "Gained zeny will not be shown.");
		return 0;
	}

	sd->state.showzeny = 1;
	clif_displaymessage(fd, "Gained zeny is now shown");
	return 0;
}

int atcommand_showdelay(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	if (sd->state.showdelay) {
		sd->state.showdelay = 0;
		clif_displaymessage(fd, "Skill delay failures won't be shown.");
		return 0;
	}
	
	sd->state.showdelay = 1;
	clif_displaymessage(fd, "Skill delay failures are shown now.");
	return 0;
}

int atcommand_showcast(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	sd->state.showcast = 1;
	clif_displaymessage(fd, "Skill casting time will be show the next time.");

	return 0;
}

int atcommand_showcastdelay(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	sd->state.showcastdelay = 1;
	clif_displaymessage(fd, "Skill delay time will be show the next time.");

	return 0;
}

/*==========================================
 * Activar o desactivar modo Resident Evil. [Tab]
 *------------------------------------------*/
int atcommand_residente(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	int Rtype = 0;

	nullpo_retr(-1, sd);

	if (!message || !*message) {
		if (map[sd->bl.m].flag.residentevil > 0)
			map[sd->bl.m].flag.residentevil = 0;
		else
			map[sd->bl.m].flag.residentevil = 1; // Default is 1
	} else {
		if (sscanf(message,"%d", &Rtype) < 1) {
			clif_displaymessage(fd, "Use @undeadmode <Type>. 0=OFF, 1=ON (Noexp Nodrop), 2=ON (Exp and Drop)");
			return -1;
		}

		if (Rtype < 0) Rtype = 0; else if (Rtype > 2) Rtype = 2;

		switch (Rtype) {
			case 0:
				map[sd->bl.m].flag.residentevil = 0;
				break;
			case 1:
				map[sd->bl.m].flag.residentevil = 1;
				break;
			case 2:
				map[sd->bl.m].flag.residentevil = 2;
				break;
		}
	}

	switch (map[sd->bl.m].flag.residentevil) {
		case 0:
			clif_displaymessage(fd, "INFO GM: Undead Mode OFF.");
			break;
		case 1:
			clif_displaymessage(fd, "INFO GM: Undead Mode ON (No Exp no Drop).");
			break;
		case 2:
			clif_displaymessage(fd, "INFO GM: Undead Mode ON (Exp and Drop).");
			break;
	}

	return 0;
}

/*==========================================
 * Activar o desactivar modo contador muertos. [Tab]
 *------------------------------------------*/
int atcommand_contadormuertes(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	int cnt_var0 = 0, cnt_var1 = 0;
	char mensaje[1024];
	nullpo_retr(-1, sd);

	if( !message || !*message || sscanf(message, "%d %d", &cnt_var0, &cnt_var1) < 1 || cnt_var0 < 0 || cnt_var1 < 0 )
	{ //Defensa de Midgard XXX / YYY Ejercitos Invasores
		if( map[sd->bl.m].flag.diecounter )
		{
			sprintf (mensaje, "[ Players %d / %d Monsters ]", map[sd->bl.m].pjmuertos, map[sd->bl.m].mobmuertos);
			clif_broadcast(&sd->bl, mensaje, strlen(mensaje) + 1, 0, ALL_SAMEMAP);
		}
		else
		{
			clif_displaymessage(fd, "- @contador <cantidad player> <cantidad mob>");
			clif_displaymessage(fd, "- 20000 Valor maximo");
		}
		return -1;
	}

	cnt_var0 = cap_value(cnt_var0, 1, 20000);
	cnt_var1 = cap_value(cnt_var1, 1, 20000);

	if( map[sd->bl.m].flag.diecounter && (cnt_var0 < 1 || cnt_var1 < 1) )
	{
		clif_displaymessage(fd, "INFO GM: map dead counter turned OFF");
		map[sd->bl.m].flag.diecounter = 0;
		map[sd->bl.m].pjmuertos = 0;
		map[sd->bl.m].mobmuertos = 0;
	}
	else
	{
		clif_displaymessage(fd, "INFO GM: map dead counter turned ON");
		map[sd->bl.m].flag.diecounter = 1;
		map[sd->bl.m].pjmuertos = cnt_var0;
		map[sd->bl.m].mobmuertos = cnt_var1;
	}

	return 0;
}

/*==========================================
 * Activar o desactivar penalty en mapa. [Tab]
 *------------------------------------------*/
int atcommand_exppenalty(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	nullpo_retr(-1, sd);

	if (map[sd->bl.m].flag.noexppenalty) {
		clif_displaymessage(fd, "INFO GM: Desactivado penalty de exp en mapa");
		map[sd->bl.m].flag.noexppenalty = 0;
	} else {
		clif_displaymessage(fd, "INFO GM: Activado penalty de exp en mapa");
		map[sd->bl.m].flag.noexppenalty = 1;
	}

	return 0;
}

/*==========================================
 * Comando para configurar bonus a gusto [Tab]
 *------------------------------------------*/
int atcommand_maspowerr(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	int masp_var0 = 0, masp_var1 = 0, masp_var2 = 0, masp_var3 = 0, masp_var4 = 0, masp_var5 = 0, masp_var6 = 0;
	nullpo_retr(-1, sd);

	if (!message || !*message || sscanf(message, "%d %d %d %d %d %d %d", &masp_var0, &masp_var1, &masp_var2, &masp_var3, &masp_var4, &masp_var5, &masp_var6) < 1 || masp_var0 < 0 || masp_var1 < 0 || masp_var2 < 0 || masp_var3 < 0 || masp_var4 < 0 || masp_var5 < 0  || masp_var6 < 0) {
		clif_displaymessage(fd, "- Uso @power <Activo/inactivo> <str> <agi> <vit> <int> <dex> <luk>");
		clif_displaymessage(fd, "- 1 = Activo / 0 = inactivo.");
		clif_displaymessage(fd, "- Valor maximo de variables 2000.");
		return -1;
	}
	if (masp_var0) { // Si es para activar... entonces...
		sd->gm_power = 1;
		clif_displaymessage(fd, "- Activados Bonus especiales -");

		sd->gm_stats[0] = cap_value(masp_var1, 0, 3000);
		sd->gm_stats[1] = cap_value(masp_var2, 0, 3000);
		sd->gm_stats[2] = cap_value(masp_var3, 0, 3000);
		sd->gm_stats[3] = cap_value(masp_var4, 0, 3000);
		sd->gm_stats[4] = cap_value(masp_var5, 0, 3000);
		sd->gm_stats[5] = cap_value(masp_var6, 0, 3000);
	} else { // Si es para cualquier otro caso... entonces...
		sd->gm_power = 0;
		clif_displaymessage(fd, "- Desactivados Bonus especiales -");
	}
	status_calc_pc(sd,0); // Recalcula status

	return 0;
}

/*==========================================
 * Comando para Coraz�n de Ciudad [Zephyrus]
 *------------------------------------------
 */
void cityheart_sub(int id, const int master_id)
{
	struct mob_data *md = BL_CAST(BL_MOB,map_id2bl(id));

	if (md) {
		md->master_id = master_id;
		clif_misceffect2(&md->bl,344);
	}

	return;
}

int atcommand_cityheart(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	int allied = 0, hpmas = 0, master_id = 0;
	char name[NAME_LENGTH];
	struct mob_data *md;

	nullpo_retr(-1, sd);

	if (sscanf(message, "%d %d %23[^\n]", &allied, &hpmas, name) < 2) {
		clif_displaymessage(fd, "Forma de Uso: @cityheart <allied> <hp> <nombre>");
		return -1;
	}

	hpmas = cap_value(hpmas,1,10000000);
	allied = cap_value(allied,0,1);

	// Creaci�n del Coraz�n
	md = (struct mob_data *)map_id2bl(mob_once_spawn_especial(sd, "this", sd->bl.x, sd->bl.y, name, MOBID_EMPERIUM, 1, "", hpmas, 0, allied, false, 0, 1, true, false, true, 0, 0, 0, false, 0, 0));
	if (md) {
		clif_misceffect2(&md->bl,344);
		master_id = md->bl.id;
	} else {
		clif_displaymessage(fd, "Fallo al crear el emperium, revise su posici�n.");
		return -1;
	}

	cityheart_sub(mob_once_spawn_especial(sd, "this", sd->bl.x, sd->bl.y + 5, "Pilar Norte", 1674, 1, "", 10000000, 2, allied, false, 0, 0, false, false, true, 0, 0, 0, false, 0, 0), master_id);
	cityheart_sub(mob_once_spawn_especial(sd, "this", sd->bl.x, sd->bl.y - 5, "Pilar Sur", 1674, 1, "", 10000000, 2, allied, false, 0, 0, false, false, true, 0, 0, 0, false, 0, 0), master_id);
	cityheart_sub(mob_once_spawn_especial(sd, "this", sd->bl.x + 5, sd->bl.y, "Pilar Este", 1674, 1, "", 10000000, 2, allied, false, 0, 0, false, false, true, 0, 0, 0, false, 0, 0), master_id);
	cityheart_sub(mob_once_spawn_especial(sd, "this", sd->bl.x - 5, sd->bl.y, "Pilar Oeste", 1674, 1, "", 10000000, 2, allied, false, 0, 0, false, false, true, 0, 0, 0, false, 0, 0), master_id);

	cityheart_sub(mob_once_spawn_especial(sd, "this", sd->bl.x + 5, sd->bl.y + 5, "Pilar Menor", 1674, 1, "", 10000000, 1, allied, false, 0, 0, false, false, true, 0, 0, 0, false, 0, 0), master_id);
	cityheart_sub(mob_once_spawn_especial(sd, "this", sd->bl.x + 5, sd->bl.y - 5, "Pilar Menor", 1674, 1, "", 10000000, 1, allied, false, 0, 0, false, false, true, 0, 0, 0, false, 0, 0), master_id);
	cityheart_sub(mob_once_spawn_especial(sd, "this", sd->bl.x - 5, sd->bl.y + 5, "Pilar Menor", 1674, 1, "", 10000000, 1, allied, false, 0, 0, false, false, true, 0, 0, 0, false, 0, 0), master_id);
	cityheart_sub(mob_once_spawn_especial(sd, "this", sd->bl.x - 5, sd->bl.y - 5, "Pilar Menor", 1674, 1, "", 10000000, 1, allied, false, 0, 0, false, false, true, 0, 0, 0, false, 0, 0), master_id);

	return 0;
}

/*==========================================
 * Special Mob Summons
 *------------------------------------------*/
int atcommand_mobdemolition(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	int amount = 0, type = 0, power = 0, ratio = 0;

	nullpo_retr(-1, sd);

	if (!message || !*message || (sscanf(message, "%d %d %d %d", &type, &amount, &ratio, &power) < 4)) {
		// Modo de Uso
		clif_displaymessage(fd, "Modo de Uso: @mobdemolition <0=all 1=only mobs> <amount> <radio de da�o> <poder>");
		return -1;
	}

	amount = cap_value(amount,1,20);
	ratio = cap_value(ratio,1,20);
	power = cap_value(power,1,1000000);

	mob_demolition (sd->bl.m, sd->bl.x, sd->bl.y, (short)ratio, type, amount, power);
	return 0;
}

int atcommand_mobevent(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	char name[NAME_LENGTH];
	int mob_id = 0, range, x = 0, y = 0;
	int number = 1, tamano = 0, hpmas = 0, slaves = 0, spawntipo = 0, showkill = 0, mostrarhp = 0, pasawarps = 0, aliado = 0, hprateannounce = 0, noexpnodrop = 1, item_drop = 0, item_amount = 0;
	int count, i, k;
	short mx, my;
	struct mob_data *md;

	nullpo_retr(-1, sd);

	if( !message || !*message || (sscanf(message, "%23s %d %d %d %d %d %d %d %d %d %d %d %d %d", name, &number, &hpmas, &tamano, &aliado, &spawntipo, &slaves, &pasawarps, &mostrarhp, &hprateannounce, &showkill, &noexpnodrop, &item_drop, &item_amount) < 1) )
	{ // Modo de Uso
		clif_displaymessage(fd, "================================================================================");
		clif_displaymessage(fd, "@mobevent <name/id> <amount> <HP> ...");
		clif_displaymessage(fd, "... <size> 0 normal | 1 small | 2 big");
		clif_displaymessage(fd, "... <type> | 0 : Normal Mob aggresive status.");
		clif_displaymessage(fd, ".......... | 1 : PC Allied / Enemy vs Mob Type 0 and 2.");
		clif_displaymessage(fd, ".......... | 2 : PC and MOB Type 1 Enemy (Aggresive).");
		clif_displaymessage(fd, ".......... | 3 : Ignores PC and PC cannot attack it / Aggresive vs MOD type 1.");
		clif_displaymessage(fd, "... <spawn position> 0 random | 1 your current pos");
		clif_displaymessage(fd, "... <Slaves?> 0 Allow to summon it if the mob can | 1 no slaves allowed");
		clif_displaymessage(fd, "... <warps> 0 cannot use warps | 1 can use map warps | 2 and warp skill too");
		clif_displaymessage(fd, "... <showHP?> 0 Normal | 1 HpMeter Bar | 2 Current/MaxHP | 3 Percent");
		clif_displaymessage(fd, "... <HPrateAnnounce?> 0 off | 1 each 10% less or more HP a map announce appears");
		clif_displaymessage(fd, "... <AnnounceKiller?> 0 off | 1 Map announce of the killer of the mob");
		clif_displaymessage(fd, "... <NoExpnoDrop?> 0 off | 1 on (No exp and job awards)");
		clif_displaymessage(fd, "... <Item ID> Item to be droped when killed (Use -1 to do Item rain event)");
		clif_displaymessage(fd, "... <Item Amount> Amount of previous Item to be droped when killed");
		clif_displaymessage(fd, "================================================================================");
		return -1;
	}

	if( (mob_id = atoi(name)) == 0 )
		mob_id = mobdb_searchname(name);

	if( mob_id == 0 || mobdb_checkid(mob_id) == 0 )
	{
		clif_displaymessage(fd, msg_table[40]); // Invalid monster ID or name.
		return -1;
	}

	if( battle_config.atc_spawn_quantity_limit >= 1 && number > battle_config.atc_spawn_quantity_limit )
		number = battle_config.atc_spawn_quantity_limit;

	hpmas = cap_value(hpmas, 0, 10000000);
	tamano = cap_value(tamano, 0, 2);
	aliado = cap_value(aliado, 0, 3);
	spawntipo = cap_value(spawntipo, 0, 1);
	slaves = cap_value(slaves, 0, 1);
	pasawarps = cap_value(pasawarps, 0, 2);

	mostrarhp = cap_value(mostrarhp, 0, 3);
	hprateannounce = cap_value(hprateannounce, 0, 1);
	showkill = cap_value(showkill, 0, 1);
	noexpnodrop = cap_value(noexpnodrop, 0, 1);

	// Aditional Drop Event
	if( item_drop > 0 )
	{
		if( !itemdb_exists(item_drop) )
		{
			clif_displaymessage(fd, msg_txt(19)); // Invalid item ID or name.
			return -1;
		}
		else
			item_amount = cap_value(item_amount, 1, 200);
	}
	else
		item_amount = 0;


	count = 0;
	range = (int)sqrt((float)number) +2;
	range = range * 2 + 5; // calculation of an odd number (+ 4 area around)
	if( spawntipo )
	{
		mx = sd->bl.x;
		my = sd->bl.y;
	}

	for( i = 0; i < number; i++ )
	{
		if( !spawntipo )
			map_search_freecell(&sd->bl, 0, &mx, &my, range, range, 0);

		k = mob_once_spawn_especial(sd, "this", mx, my, "--ja--", mob_id, 1, "",
			hpmas, tamano, aliado, slaves, pasawarps, mostrarhp, hprateannounce, showkill, noexpnodrop, 0, item_drop, item_amount, false, 0, 0);

		if( (md = (struct mob_data *)map_id2bl(k)) )
			count ++;
	}

	if( count != 0 )
		if( number == count )
			clif_displaymessage(fd, msg_table[39]); // All monster summoned!
		else
		{
			sprintf(atcmd_output, msg_table[240], count); // %d monster(s) summoned!
			clif_displaymessage(fd, atcmd_output);
		}
	else
	{
		clif_displaymessage(fd, msg_table[40]); // Invalid monster ID or name.
		return -1;
	}

	return 0;
}

#ifndef TXT_ONLY

/*==========================================
 * Personajes usando IP [Zephyrus]
 *------------------------------------------*/
int atcommand_whoip(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	StringBuf buf;
	int ip1 = 0, ip2 = 0, ip3 = 0, ip4 = 0;
	char last_ip[20];

	memset(last_ip, '\0', sizeof(last_ip));

	if (!message || !*message || sscanf(message, "%d.%d.%d.%d", &ip1, &ip2, &ip3, &ip4) < 4) {
		clif_displaymessage(fd, "Debes ingresar la direccin IP (Use: @whoip <ip1>.<ip2>.<ip3>.<ip4>).");
		return -1;
	}

	if (ip1 < 0 || ip1 > 255) ip1 = 0;
	if (ip2 < 0 || ip2 > 255) ip2 = 0;
	if (ip3 < 0 || ip3 > 255) ip3 = 0;
	if (ip4 < 0 || ip4 > 255) ip4 = 0;

	sprintf(last_ip, "%d.%d.%d.%d", ip1, ip2, ip3, ip4);

	StringBuf_Init(&buf);
	StringBuf_Printf(&buf, "SELECT `char`.`char_id`, `login`.`member_id` FROM `char` LEFT JOIN `login` ON `login`.`account_id` = `char`.`account_id` WHERE `login`.`last_ip` = '%s' AND `char`.`online` = '1'", last_ip);

	if( SQL_ERROR == Sql_Query(mmysql_handle, StringBuf_Value(&buf)) )
	{
		Sql_ShowDebug(mmysql_handle);
		ShowWarning("Atcommand_whoip: Error de consulta SQL al obtener informacion de IP.\n");
	}
	else if ( Sql_NumRows(mmysql_handle) == 0 )
	{
		clif_displaymessage(fd, "No se encontraron resultados...");
	}
	else
	{
		while( SQL_SUCCESS == Sql_NextRow(mmysql_handle) )
		{
			char* data;
			int char_id, member_id;
			struct map_session_data* pl_sd;

			Sql_GetData(mmysql_handle, 0, &data, NULL); char_id = atoi(data);
			Sql_GetData(mmysql_handle, 1, &data, NULL); member_id = atoi(data);

			if( (pl_sd = map_charid2sd(char_id)) != NULL )
			{
				sprintf(atcmd_output, "Personaje: %s Mapa: %s (CHR: %d | ACC: %d | MEMBER: %d)", pl_sd->status.name, map[pl_sd->bl.m].name, pl_sd->status.char_id, pl_sd->status.account_id, member_id);
				clif_displaymessage(fd, atcmd_output);
			}
		}
	}

	StringBuf_Destroy(&buf);
	Sql_FreeResult(mmysql_handle);

	return 0;
}

/*==========================================
 * Reporte de Cuentas de una IP [Zephyrus]
 *------------------------------------------*/
int atcommand_ipinfo(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	StringBuf buf;
	int ip1 = 0, ip2 = 0, ip3 = 0, ip4 = 0;
	char last_ip[20];

	memset(last_ip, '\0', sizeof(last_ip));

	if( !message || !*message || sscanf(message, "%d.%d.%d.%d", &ip1, &ip2, &ip3, &ip4) < 4 )
	{
		clif_displaymessage(fd, "Debes ingresar la direccin IP (Use: @ipinfo <ip1>.<ip2>.<ip3>.<ip4>).");
		return -1;
	}

	if (ip1 < 0 || ip1 > 255) ip1 = 0;
	if (ip2 < 0 || ip2 > 255) ip2 = 0;
	if (ip3 < 0 || ip3 > 255) ip3 = 0;
	if (ip4 < 0 || ip4 > 255) ip4 = 0;

	sprintf(last_ip, "%d.%d.%d.%d", ip1, ip2, ip3, ip4);

	StringBuf_Init(&buf);
	StringBuf_Printf(&buf, "SELECT `account_id`, `member_id`, `userid`, `lastlogin`, `email` FROM `login` WHERE `last_ip` = '%s'", last_ip);

	if( SQL_ERROR == Sql_Query(mmysql_handle, StringBuf_Value(&buf)) )
	{
		Sql_ShowDebug(mmysql_handle);
		ShowWarning("Atcommand_ipinfo: Error de consulta SQL al obtener informacion de IP.\n");
	}
	else if ( Sql_NumRows(mmysql_handle) == 0 )
	{
		clif_displaymessage(fd, "No se encontraron resultados...");
	}
	else
	{
		while( SQL_SUCCESS == Sql_NextRow(mmysql_handle) )
		{
			char* data;
			int account_id, member_id;
			char userid[NAME_LENGTH], lastlogin[24], email[40];

			Sql_GetData(mmysql_handle, 0, &data, NULL); account_id = atoi(data);
			Sql_GetData(mmysql_handle, 1, &data, NULL); member_id = atoi(data);
			Sql_GetData(mmysql_handle, 2, &data, NULL); safestrncpy(userid, data, sizeof(userid));
			Sql_GetData(mmysql_handle, 3, &data, NULL); safestrncpy(lastlogin, data, sizeof(lastlogin));
			Sql_GetData(mmysql_handle, 4, &data, NULL); safestrncpy(email, data, sizeof(email));

			sprintf(atcmd_output, "Cuenta: %s (%d) | ID: %d | Email: %s | %s", userid, member_id, account_id, email, lastlogin);
			clif_displaymessage(fd, atcmd_output);
		}
	}

	StringBuf_Destroy(&buf);
	Sql_FreeResult(mmysql_handle);

	return 0;
}

/*==========================================
 * Consulta de personajes en una misma cuenta [Zephyrus]
 *------------------------------------------*/
int atcommand_charlist(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	struct map_session_data *pl_sd;
	char name[NAME_LENGTH], esc_name[NAME_LENGTH*2+1];
	char *data;
	int account_id = 0;

	nullpo_retr(-1, sd);
	memset(name, '\0', sizeof(name));

	if( !message || !*message || sscanf(message, "%23[^\n]", name) < 1 )
	{
		clif_displaymessage(fd, "Por favor, ingresa el nombre de un jugador (usa: @charlist <nombre char>).");
		return -1;
	}

	if ((pl_sd = map_nick2sd(name)) != NULL)
		account_id = pl_sd->status.account_id;
	else
	{
		Sql_EscapeStringLen(mmysql_handle, esc_name, name, strnlen(name, NAME_LENGTH));
		if ( SQL_ERROR == Sql_Query(mmysql_handle, "SELECT `account_id` FROM `char` WHERE `name` = '%s'", esc_name) )
			clif_displaymessage(fd, "Error en la consulta SQL. Reporte al Dev.");

		if ( Sql_NumRows(mmysql_handle) == 0 )
			clif_displaymessage(fd, "No existe el nombre del personaje.");
		else
		{
			Sql_NextRow(mmysql_handle);
			Sql_GetData(mmysql_handle, 0, &data, NULL); account_id = atoi(data);
		}

		Sql_FreeResult(mmysql_handle);
	}

	if( account_id )
	{
		if ( SQL_ERROR == Sql_Query(mmysql_handle, "SELECT `name`, `class`, `base_level`, `char_num`, `online` FROM `char` WHERE `account_id` = '%d' ORDER BY `char_num`", account_id) )
			clif_displaymessage(fd, "Error en la consulta SQL. Reporte al Dev.");
		else
		{
			sprintf(atcmd_output, "-- Personajes Encontrados en Cuenta %d --", account_id);
			clif_displaymessage(fd, atcmd_output);
			while ( SQL_SUCCESS == Sql_NextRow(mmysql_handle) )
			{
				int class_;
				short base_level, char_num, online;

				Sql_GetData(mmysql_handle, 0, &data, NULL); safestrncpy(name, data, sizeof(name));
				Sql_GetData(mmysql_handle, 1, &data, NULL); class_ = atoi(data);
				Sql_GetData(mmysql_handle, 2, &data, NULL); base_level = atoi(data);
				Sql_GetData(mmysql_handle, 3, &data, NULL); char_num = atoi(data);
				Sql_GetData(mmysql_handle, 4, &data, NULL); online = atoi(data);

				sprintf(atcmd_output, "[%d] %s | Clase: %s | Nivel: %d | Online: %d.", char_num, name, job_name(class_), base_level, online);
				clif_displaymessage(fd, atcmd_output);
			}
		}

		Sql_FreeResult(mmysql_handle);
	}

	return 0;
}

/*==========================================
 * Informaci�n de una cuenta [Account ID]
 *------------------------------------------*/
void account_info(const int fd, struct map_session_data *sd, int account_id)
{
	int member_id = 0;
	char userid[NAME_LENGTH], user_pass[NAME_LENGTH], email[40], last_ip[20];
	short level = -1;
	char *data;

	if( SQL_ERROR == Sql_Query(mmysql_handle, "SELECT `userid`, `user_pass`, `email`, `last_ip`, `level`, `member_id` FROM `login` WHERE `account_id` = '%d'", account_id) )
		clif_displaymessage(fd, "Error en consulta de Cuenta. Notifique al Dev.");
	else if( Sql_NumRows(mmysql_handle) == 0 )
		clif_displaymessage(fd, "Cuenta no encontrada.");
	else
	{
		Sql_NextRow(mmysql_handle);
		Sql_GetData(mmysql_handle, 0, &data, NULL); safestrncpy(userid, data, sizeof(userid));
		Sql_GetData(mmysql_handle, 1, &data, NULL); safestrncpy(user_pass, data, sizeof(user_pass));
		Sql_GetData(mmysql_handle, 2, &data, NULL); safestrncpy(email, data, sizeof(email));
		Sql_GetData(mmysql_handle, 3, &data, NULL); safestrncpy(last_ip, data, sizeof(last_ip));
		Sql_GetData(mmysql_handle, 4, &data, NULL); level = atoi(data);
		Sql_GetData(mmysql_handle, 5, &data, NULL); member_id = atoi(data);
	}

	Sql_FreeResult(mmysql_handle);

	if( level == -1 )
		return;

	if( level >= pc_isGM(sd) )
	{
		clif_displaymessage(fd, "No puedes obtener informaci�n de Cuentas de tu mismo nivel");
		return;
	}

	sprintf(atcmd_output, "-- Reporte de Cuenta %d --", account_id);
	clif_displaymessage(fd, atcmd_output);
	sprintf(atcmd_output, "Usuario: %s | Membres�a: %d", userid, member_id);
	clif_displaymessage(fd, atcmd_output);

	if( pc_isGM(sd) > 1 )
	{
		sprintf(atcmd_output, "Clave: %s.", user_pass);
		clif_displaymessage(fd, atcmd_output);
	}

	sprintf(atcmd_output, "Correo: %s.", email);
	clif_displaymessage(fd, atcmd_output);
	sprintf(atcmd_output, "Ultima IP: %s.", last_ip);
	clif_displaymessage(fd, atcmd_output);

	clif_displaymessage(fd, "-- Detalle de Personajes --");

	if( SQL_ERROR == Sql_Query(mmysql_handle, "SELECT `char_id`, `name`, `char_num`, `class`, `base_level`, `online` FROM `char` WHERE `account_id` = '%d' ORDER BY `char_num`", account_id) )
		clif_displaymessage(fd, "Error en consulta de Personajes. Notifique al Dev.");
	else if( Sql_NumRows(mmysql_handle) == 0 )
		clif_displaymessage(fd, "No hay personajes en la Cuenta.");
	else
	{
		while ( SQL_SUCCESS == Sql_NextRow(mmysql_handle) )
		{
			int char_id, class_;
			short char_num, base_level, online;
			char name[NAME_LENGTH];

			Sql_GetData(mmysql_handle, 0, &data, NULL); char_id = atoi(data);
			Sql_GetData(mmysql_handle, 1, &data, NULL); safestrncpy(name, data, sizeof(name));
			Sql_GetData(mmysql_handle, 2, &data, NULL); char_num = atoi(data);
			Sql_GetData(mmysql_handle, 3, &data, NULL); class_ = atoi(data);
			Sql_GetData(mmysql_handle, 4, &data, NULL); base_level = atoi(data);
			Sql_GetData(mmysql_handle, 5, &data, NULL); online = atoi(data);

			sprintf(atcmd_output, "[%d] %s (%d) | %s level %d | Online: %d", char_num, name, char_id, job_name(class_), base_level, online);
			clif_displaymessage(fd, atcmd_output);
		}
	}

	Sql_FreeResult(mmysql_handle);

	clif_displaymessage(fd, "-- Contrase�a de Storage --");

	if( SQL_ERROR == Sql_Query(mmysql_handle, "SELECT `value` FROM `global_reg_value` WHERE `account_id` = '%d' AND `str` = '#kafra_code' AND `value` <> '0'", account_id) )
		clif_displaymessage(fd, "Error en consulta de Clave Storage. Notifique al Dev.");
	else if( Sql_NumRows(mmysql_handle) == 0 )
		clif_displaymessage(fd, "No hay clave de Storage en la Cuenta.");
	else
	{
		int value;
		Sql_NextRow(mmysql_handle);
		Sql_GetData(mmysql_handle, 0, &data, NULL); value = atoi(data);

		sprintf(atcmd_output, "...> %d <...", value - account_id - 1337);
		clif_displaymessage(fd, atcmd_output);
	}

	Sql_FreeResult(mmysql_handle);

	return;
}

int atcommand_accountinfo(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	int account_id = 0;

	nullpo_retr(-1, sd);

	if( !message || !*message )
	{
		clif_displaymessage(fd, "Ingresa el ID de la Cuenta (usa: @accountinfo <account_id>).");
		return -1;
	}

	account_id = atoi(message);

	if( account_id < START_ACCOUNT_NUM )
	{
		clif_displaymessage(fd, "Numero de Cuenta Invalido.");
		return -1;
	}

	account_info(fd, sd, account_id);

	return 0;
}

int atcommand_logininfo(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	char userid[NAME_LENGTH], esc_userid[NAME_LENGTH*2+1];
	int account_id = 0;

	nullpo_retr(-1, sd);

	if( !message || !*message )
	{
		clif_displaymessage(fd, "Ingresa el Login de la Cuenta (usa: @logininfo <userid>).");
		return -1;
	}

	safestrncpy(userid, message, sizeof(userid));

	Sql_EscapeStringLen(mmysql_handle, esc_userid, userid, strnlen(userid, NAME_LENGTH));
	if ( SQL_ERROR == Sql_Query(mmysql_handle, "SELECT `account_id` FROM `login` WHERE `userid` = BINARY '%s'", esc_userid) )
		clif_displaymessage(fd, "Error de consulta de Login. Reporte al Dev.");
	else if ( Sql_NumRows(mmysql_handle) == 0 )
		clif_displaymessage(fd, "No se encontr� la Cuenta.");
	else
	{
		char *data;

		Sql_NextRow(mmysql_handle);
		Sql_GetData(mmysql_handle, 0, &data, NULL);
		account_id = atoi(data);
	}

	Sql_FreeResult(mmysql_handle);

	if (!account_id)
		return 0;

	if (account_id < START_ACCOUNT_NUM)
		clif_displaymessage(fd, "Cuenta invalida para realizar consultas.");
	else
		account_info(fd, sd, account_id);

	return 0;
}

/*==========================================
 * Informaci�n de Membres�as
 *------------------------------------------*/
int atcommand_memberinfo(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	int member_id = 0, i;
	char esc_email[81], email[40], mpass[40];
	bool is_gm = false;
	short mlevel;
	char *data;

	nullpo_retr(-1,sd);

	if( !message || !*message )
	{
		clif_displaymessage(fd, "Enter the Member ID or Email. Usage: @memberinfo <member_id/email>.");
		return -1;
	}

	if( (member_id = atoi(message)) > 0 )
		i = Sql_Query(mmysql_handle, "SELECT `member_id`, `email`, `mpass`, `mlevel` FROM `members` WHERE `member_id` = '%d'", member_id);
	else
	{ // Search by Email
		Sql_EscapeStringLen(mmysql_handle, esc_email, message, strnlen(message, 40));
		i = Sql_Query(mmysql_handle, "SELECT `member_id`, `email`, `mpass`, `mlevel` FROM `members` WHERE `email` LIKE '%s'", esc_email);
	}

	if( i == SQL_ERROR )
	{
		clif_displaymessage(fd, "MySQL Query Error. Please notify a Developer.");
		member_id = 0;
	}
	else if( SQL_SUCCESS == Sql_NextRow(mmysql_handle) )
	{
		Sql_GetData(mmysql_handle, 0, &data, NULL); member_id = atoi(data);
		Sql_GetData(mmysql_handle, 1, &data, NULL); safestrncpy(email, data, sizeof(email));
		Sql_GetData(mmysql_handle, 2, &data, NULL); safestrncpy(mpass, data, sizeof(mpass));
		Sql_GetData(mmysql_handle, 3, &data, NULL); mlevel = atoi(data);
	}
	else
	{
		clif_displaymessage(fd, "No Member information found.");
		member_id = 0;
	}

	Sql_FreeResult(mmysql_handle);
	if( member_id == 0 )
		return -1;

	if( SQL_ERROR == Sql_Query(mmysql_handle, "SELECT `account_id`, `userid`, `level`, `user_pass`, `last_ip` FROM `login` WHERE `member_id` = '%d'", member_id) )
		clif_displaymessage(fd, "MySQL Query Error. Please report to a Developer.");
	else if ( Sql_NumRows(mmysql_handle) == 0 )
		clif_displaymessage(fd, "No accounts founds in this membership.");
	else
	{
		while ( SQL_SUCCESS == Sql_NextRow(mmysql_handle) )
		{
			int account_id; short level;
			char userid[NAME_LENGTH], user_pass[NAME_LENGTH], last_ip[20];
			char *data;

			Sql_GetData(mmysql_handle, 0, &data, NULL); account_id = atoi(data);
			Sql_GetData(mmysql_handle, 1, &data, NULL); safestrncpy(userid, data, sizeof(userid));
			Sql_GetData(mmysql_handle, 2, &data, NULL); level = atoi(data);
			Sql_GetData(mmysql_handle, 3, &data, NULL); safestrncpy(user_pass, data, sizeof(user_pass));
			Sql_GetData(mmysql_handle, 4, &data, NULL); safestrncpy(last_ip, data, sizeof(last_ip));

			if( level >= pc_isGM(sd) )
			{
				is_gm = true;
				clif_displaymessage(fd, "-- GM Account with highest Level --");
			}
			else
			{
				sprintf(atcmd_output, "[%d] %s | Password: %s | Last IP: %s", account_id, userid, user_pass, last_ip);
				clif_displaymessage(fd, atcmd_output);
			}
		}
	}
	Sql_FreeResult(mmysql_handle);

	if( !is_gm )
	{
		if( mlevel > 0 )
			clif_displaymessage(fd, "-- Special Membership. Cannot access more information --");
		else
		{
			sprintf(atcmd_output, "Member ID [%d] Email %s Password %s", member_id, email, mpass);
			clif_displaymessage(fd, atcmd_output);
		}
	}
	else
		clif_displaymessage(fd, "-- This Membership belong to another GM. Cannot access more information --");

	return 0;
}

/*==========================================
 * Account Transfer2Membership
 *------------------------------------------*/
int atcommand_moveaccount(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	int account_id = 0, member_id = 0, level = 0;
	char email[40], userid[NAME_LENGTH];
	char *data;
	nullpo_retr(-1,sd);

	if( !message || !*message || sscanf(message, "%d %d", &account_id, &member_id) < 2 )
	{
		clif_displaymessage(fd, "Please enter Account ID and destination Member ID. @moveaccount <account id> <member id>");
		return -1;
	}

	// Searching for Member Information
	if( SQL_ERROR == Sql_Query(mmysql_handle, "SELECT `email` FROM `members` WHERE `member_id` = '%d'", member_id) )
	{
		clif_displaymessage(fd, "Query Error... Please notify a Developer.");
		member_id = 0;
	}
	else if( SQL_SUCCESS == Sql_NextRow(mmysql_handle) )
	{
		Sql_GetData(mmysql_handle, 0, &data, NULL);
		safestrncpy(email, data, sizeof(email));
	}
	else
	{
		clif_displaymessage(fd, "Member ID don't exists.");
		member_id = 0;
	}

	Sql_FreeResult(mmysql_handle);

	// Searching for Login Information
	if( SQL_ERROR == Sql_Query(mmysql_handle, "SELECT `userid`, `level` FROM `login` WHERE `account_id` = '%d'", account_id) )
	{
		clif_displaymessage(fd, "Query Error... Please notify a Developer.");
		account_id = 0;
	}
	else if( SQL_SUCCESS == Sql_NextRow(mmysql_handle) )
	{
		Sql_GetData(mmysql_handle, 0, &data, NULL); safestrncpy(userid, data, sizeof(userid));
		Sql_GetData(mmysql_handle, 1, &data, NULL); level = atoi(data);
	}
	else
	{
		clif_displaymessage(fd, "Account ID don't exists.");
		account_id = 0;
	}

	Sql_FreeResult(mmysql_handle);

	// Account Update
	if( member_id && account_id )
	{
		if( level >= pc_isGM(sd) )
			clif_displaymessage(fd, "You cannot move Game Master account highest than yours.");
		else
		{
			char esc_email[81];
			Sql_EscapeStringLen(mmysql_handle, esc_email, email, strnlen(email, 40));
			if( SQL_ERROR == Sql_Query(mmysql_handle, "UPDATE `login` SET `member_id` = '%d', `email` = '%s' WHERE `account_id` = '%d'", member_id, esc_email, account_id) )
				clif_displaymessage(fd, "Account Transfer Error. Please notify a Developer.");
			else
			{
				struct map_session_data *pl_sd;
				clif_displaymessage(fd, "Account Transfer Completed.");
				if( (pl_sd = map_id2sd(account_id)) != NULL )
				{ // Notify Player
					sprintf(atcmd_output, "Your account have been transfered to another membership. New Email %s.", email);
					clif_displaymessage(pl_sd->fd, atcmd_output);
				}
			}
		}
	}

	return 0;
}

#endif

/*==========================================
 * Comando para buscar un vendedor de X item [Zephyrus]
 *------------------------------------------*/
int atcommand_whosell(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	struct map_session_data *pl_sd, *b_sd[MAX_SEARCH];
	struct s_mapiterator* iter;

	struct item_data *item_array[MAX_SEARCH];
	int total[MAX_SEARCH], amount[MAX_SEARCH];
	unsigned int MinPrice[MAX_SEARCH], MaxPrice[MAX_SEARCH];
	char output[256];
	int i, j, count = 1;

	if( !message || !*message )
	{
		clif_displaymessage(fd, "Please, enter Item name or its ID (usage: @whosell <item name or ID>).");
		return -1;
	}

	if( (item_array[0] = itemdb_exists(atoi(message))) == NULL )
		count = itemdb_searchname_array(item_array, MAX_SEARCH, message);

	if( count < 1 )
	{ // No items found
		clif_displaymessage(fd, msg_txt(19));
		return -1;
	}

	if( count > MAX_SEARCH ) count = MAX_SEARCH;

	// Preparing Search Recorders
	for( i = 0; i < MAX_SEARCH; i++ )
	{
		total[i] = amount[i] = MaxPrice[i] = 0;
		MinPrice[i] = battle_config.vending_max_value + 1;
		b_sd[i] = NULL;
	}

	iter = mapit_getallusers();
	for( pl_sd = (TBL_PC*)mapit_first(iter); mapit_exists(iter); pl_sd = (TBL_PC*)mapit_next(iter) )
	{
		if( !pl_sd->vender_id ) continue;
		for( i = 0; i < pl_sd->vend_num; i++ )
		{ // Searching in the Vending List
			for( j = 0; j < count; j++ )
			{ // Compares with each search result
				if( pl_sd->status.cart[pl_sd->vending[i].index].nameid != item_array[j]->nameid )
					continue;

				amount[j] += pl_sd->vending[i].amount;
				total[j]++;

				if( pl_sd->vending[i].value < MinPrice[j] )
				{ // Best Price
					MinPrice[j] = pl_sd->vending[i].value;
					b_sd[j] = pl_sd;
				}
				if( pl_sd->vending[i].value > MaxPrice[j] )
					MaxPrice[j] = pl_sd->vending[i].value;
			}
		}
	}
	mapit_free(iter);

	for( i = 0; i < count; i++ )
	{
		if( total[i] > 0 && b_sd[i] != NULL )
		{
			sprintf(output, "[%d] El mejor precio de '%s' es %u por '%s' en %s <%d,%d>. Max %u. Encontrado en %d shops, %d en venta.", item_array[i]->nameid, item_array[i]->jname, MinPrice[i], b_sd[i]->status.name, map[b_sd[i]->bl.m].name, b_sd[i]->bl.x, b_sd[i]->bl.y, MaxPrice[i], total[i], amount[i]);
			if( sd->bl.m == b_sd[i]->bl.m )
				clif_viewpoint(sd, 1, 1, b_sd[i]->bl.x, b_sd[i]->bl.y, i, 0xFFFFFF);
		}
		else
			sprintf(output, "[%d] '%s' no est� en tiendas...", item_array[i]->nameid, item_array[i]->jname);

		clif_displaymessage(sd->fd, output);
	}

	return 0;
}

/*==========================================
 * Creaci�n de items en el suelo, en un Area.
 *------------------------------------------*/
int atcommand_flooritem(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	char item_name[100];
	int number = 0, count;
	struct item_data *item_data;
	nullpo_retr(-1, sd);

	memset(item_name, '\0', sizeof(item_name));

	if( !message || !*message || (
		sscanf(message, "\"%99[^\"]\" %d", item_name, &number) < 1 &&
		sscanf(message, "%99s %d", item_name, &number) < 1
	)) {
		clif_displaymessage(fd, "Usage: @flooritem <itemname/itemid> [quantity]");
		return -1;
	}

	if( number <= 0 )
		number = 1;

	if ((item_data = itemdb_searchname(item_name)) == NULL &&
	    (item_data = itemdb_exists(atoi(item_name))) == NULL)
	{
		clif_displaymessage(fd, msg_txt(19)); // Invalid item ID or name.
		return -1;
	}

	count = map_addflooritem_area(&sd->bl, 0, 0, 0, item_data->nameid, number);

	if( count != 0 )
		if( number == count )
			clif_displaymessage(fd, "All items created.");
		else
		{
			sprintf(atcmd_output, "%d item(s) created.", count);
			clif_displaymessage(fd, atcmd_output);
		}
	else {
		clif_displaymessage(fd, "No items created!!.");
		return -1;
	}

	return 0;
}

/*==========================================
 * Duel organizing functions [LuzZza]
 *
 * @duel [limit|nick] - create a duel
 * @invite <nick> - invite player
 * @accept - accept invitation
 * @reject - reject invitation
 * @leave - leave duel
 *------------------------------------------*/
int atcommand_invite(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	unsigned int did = sd->duel_group;
	struct map_session_data *target_sd = map_nick2sd((char *)message);

	if(did <= 0)	{
		// "Duel: @invite without @duel."
		clif_displaymessage(fd, msg_txt(350));
		return 0;
	}
	
	if(duel_list[did].max_players_limit > 0 &&
		duel_list[did].members_count >= duel_list[did].max_players_limit) {
		
		// "Duel: Limit of players is reached."
		clif_displaymessage(fd, msg_txt(351));
		return 0;
	}
	
	if(target_sd == NULL) {
		// "Duel: Player not found."
		clif_displaymessage(fd, msg_txt(352));
		return 0;
	}
	
	if(target_sd->duel_group > 0 || target_sd->duel_invite > 0) {
		// "Duel: Player already in duel."
		clif_displaymessage(fd, msg_txt(353));
		return 0;
	}

	if(battle_config.duel_only_on_same_map && target_sd->bl.m != sd->bl.m)
	{
		sprintf(atcmd_output, msg_txt(364), message);
		clif_displaymessage(fd, atcmd_output);
		return 0;
	}
	
	duel_invite(did, sd, target_sd);
	// "Duel: Invitation has been sent."
	clif_displaymessage(fd, msg_txt(354));
	return 0;
}

int atcommand_duel(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	char output[CHAT_SIZE_MAX];
	unsigned int maxpl=0, newduel;
	struct map_session_data *target_sd;

	if(sd->duel_group > 0) {
		duel_showinfo(sd->duel_group, sd);
		return 0;
	}

	if(sd->duel_invite > 0) {
		// "Duel: @duel without @reject."
		clif_displaymessage(fd, msg_txt(355));
		return 0;
	}

	if(sd->state.pvpmode) {
		clif_displaymessage(fd, "No puedes crear un duelo en modo PK.");
		return 0;
	}

	if(sd->status.guild_id && guild_isatwar(sd->status.guild_id)) {
		clif_displaymessage(fd, "No puedes crear un duelo si tu guild esta en Guerra.");
		return 0;
	}

	if(!duel_checktime(sd)) {
		// "Duel: You can take part in duel only one time per %d minutes."
		sprintf(output, msg_txt(356), battle_config.duel_time_interval);
		clif_displaymessage(fd, output);
		return 0;
	}

	if(strlen(message) > 0) {
		if(sscanf(message, "%d", &maxpl) >= 1) {
			if(maxpl < 2 || maxpl > 65535) {
				clif_displaymessage(fd, msg_txt(357)); // "Duel: Invalid value."
				return 0;
			}
			duel_create(sd, maxpl);
		} else {
			target_sd = map_nick2sd((char *)message);
			if(target_sd != NULL) {
				if((newduel = duel_create(sd, 2)) != -1) {
					if(target_sd->duel_group > 0 ||	target_sd->duel_invite > 0) {
						clif_displaymessage(fd, msg_txt(353)); // "Duel: Player already in duel."
						return 0;
					}
					duel_invite(newduel, sd, target_sd);
					clif_displaymessage(fd, msg_txt(354)); // "Duel: Invitation has been sent."
				}
			} else {
				// "Duel: Player not found."
				clif_displaymessage(fd, msg_txt(352));
				return 0;
			}
		}
	} else
		duel_create(sd, 0);

	return 0;
}


int atcommand_leave(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	if(sd->duel_group <= 0) {
		// "Duel: @leave without @duel."
		clif_displaymessage(fd, msg_txt(358));
		return 0;
	}

	duel_leave(sd->duel_group, sd);
	clif_displaymessage(fd, msg_txt(359)); // "Duel: You left the duel."
	return 0;
}

int atcommand_accept(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	char output[CHAT_SIZE_MAX];

	if(sd->state.pvpmode) {
		clif_displaymessage(fd, "No puedes unirte a un duelo estando en Modo PK");
		return 0;
	}

	if(sd->status.guild_id && guild_isatwar(sd->status.guild_id)) {
		clif_displaymessage(fd, "No puedes unirte a duelo si tu guild esta en Guerra.");
		return 0;
	}

	if(!duel_checktime(sd)) {
		// "Duel: You can take part in duel only one time per %d minutes."
		sprintf(output, msg_txt(356), battle_config.duel_time_interval);
		clif_displaymessage(fd, output);
		return 0;
	}

	if(sd->duel_invite <= 0) {
		// "Duel: @accept without invititation."
		clif_displaymessage(fd, msg_txt(360));
		return 0;
	}

	duel_accept(sd->duel_invite, sd);
	// "Duel: Invitation has been accepted."
	clif_displaymessage(fd, msg_txt(361));
	return 0;
}

int atcommand_reject(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	if(sd->duel_invite <= 0) {
		// "Duel: @reject without invititation."
		clif_displaymessage(fd, msg_txt(362));
		return 0;
	}

	duel_reject(sd->duel_invite, sd);
	// "Duel: Invitation has been rejected."
	clif_displaymessage(fd, msg_txt(363));
	return 0;
}

/*===================================
 * Cash Points
 *-----------------------------------*/
int atcommand_cash(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	int value;
	nullpo_retr(-1, sd);

	if( !message || !*message || (value = atoi(message)) == 0 ) {
		clif_displaymessage(fd, "Please, enter an amount.");
		return -1;
	}

	if( !strcmpi(command+1,"cash") )
	{
		if( value > 0 )
			pc_getcash(sd, value, 0);
		else
			pc_paycash(sd, -value, 0);
	}
	else
	{ // @points
		if( value > 0 )
			pc_getcash(sd, 0, value);
		else
			pc_paycash(sd, -value, -value);
	}

	return 0;
}

int atcommand_gcash(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	int value = 0;
	char user[NAME_LENGTH];
	struct map_session_data *pl_sd;
	nullpo_retr(-1,sd);

	if( !message || !*message || sscanf(message, "%d %23[^\r\n]", &value, user) != 2 )
	{
		clif_displaymessage(fd, "Usage: @gcash <amount> <user>.");
		return -1;
	}

	if( (pl_sd = map_nick2sd(user)) == NULL )
	{
		clif_displaymessage(fd, msg_txt(3)); // Character not found.
		return -1;
	}

	if( !pc_can_give_items(pc_isGM(sd)) )
	{
		clif_displaymessage(fd, "Your GM level don't allow you to give cash.");
		return -1;
	}

	if( value <= 0 || sd->cashPoints < value )
	{
		sprintf(atcmd_output, "Invalid value. You have currently %d cash points.", sd->cashPoints);
		clif_displaymessage(fd, atcmd_output);
		return -1;
	}

	if( sd->state.secure_items )
	{
		clif_displaymessage(sd->fd, "You can't transfer Cash. Blocked with @security");
		return 1;
	}

	sprintf(atcmd_output, "'%s' has transfered %d Cash Points to you.", sd->status.name, value);
	clif_disp_onlyself(pl_sd, atcmd_output, strlen(atcmd_output));
	sprintf(atcmd_output, "You have transfered %d Cash Points to '%s'.", value, pl_sd->status.name);
	clif_disp_onlyself(sd, atcmd_output, strlen(atcmd_output));

	pc_paycash(sd, value, 0);
	pc_getcash(pl_sd, value, 0);
	
	return 0;
}

int atcommand_gpoints(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	int value = 0;
	char user[NAME_LENGTH];
	struct map_session_data *pl_sd;
	nullpo_retr(-1,sd);

	if( !message || !*message || sscanf(message, "%d %23[^\r\n]", &value, user) != 2 )
	{
		clif_displaymessage(fd, "Usage: @gpoints <amount> <user>.");
		return -1;
	}

	if( (pl_sd = map_nick2sd(user)) == NULL )
	{
		clif_displaymessage(fd, msg_txt(3)); // Character not found.
		return -1;
	}

	if( !pc_can_give_items(pc_isGM(sd)) )
	{
		clif_displaymessage(fd, "Your GM level don't allow you to give free cash.");
		return -1;
	}

	if( value <= 0 || sd->kafraPoints < value )
	{
		sprintf(atcmd_output, "Invalid value. You have currently %d kafra points.", sd->kafraPoints);
		clif_displaymessage(fd, atcmd_output);
		return -1;
	}

	if( sd->state.secure_items )
	{
		clif_displaymessage(sd->fd, "You can't transfer Points. Blocked with @security");
		return 1;
	}

	sprintf(atcmd_output, "'%s' has transfered %d Kafra Points to you.", sd->status.name, value);
	clif_disp_onlyself(pl_sd, atcmd_output, strlen(atcmd_output));
	sprintf(atcmd_output, "You have transfered %d Kafra Points to '%s'.", value, pl_sd->status.name);
	clif_disp_onlyself(sd, atcmd_output, strlen(atcmd_output));

	pc_paycash(sd, value, value);
	pc_getcash(pl_sd, 0, value);
	
	return 0;
}

/*===================================
 * Away message (@away, @aw) [LuzZza]
 *-----------------------------------*/
int atcommand_away(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	if(strlen(message) > 0) {
		if(strlen(message) > 128)
			return -1;
		strcpy(sd->away_message, message);
		//"Away automessage has been activated."
		clif_displaymessage(fd, msg_txt(546));
	} else {
		if(strlen(sd->away_message) > 0) {
			sd->away_message[0] = 0;
			//"Away automessage has been disabled."
			clif_displaymessage(fd, msg_txt(547));
			return 0;
		}
		//"Usage: @away,@aw <message>. Enter empty message for disable it."
		clif_displaymessage(fd, msg_txt(548));
	}
	return 0;
}

// @clone/@slaveclone/@evilclone <playername> [Valaris]
int atcommand_clone(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	int x=0,y=0,flag=0,master=0,i=0;
	struct map_session_data *pl_sd=NULL;

	if (!message || !*message) {
		clif_displaymessage(sd->fd,"You must enter a name or character ID.");
		return 0;
	}

	if((pl_sd=map_nick2sd((char *)message)) == NULL && (pl_sd=map_charid2sd(atoi(message))) == NULL) {
		clif_displaymessage(fd, msg_txt(3));	// Character not found.
		return 0;
	}

	if(pc_isGM(pl_sd) > pc_isGM(sd)) {
		clif_displaymessage(fd, msg_txt(126));	// Cannot clone a player of higher GM level than yourself.
		return 0;
	}

	if (strcmpi(command+1, "clone") == 0) 
		flag = 1;
	else if (strcmpi(command+1, "slaveclone") == 0) {
	  	flag = 2;
		master = sd->bl.id;
		if (battle_config.atc_slave_clone_limit
			&& mob_countslave(&sd->bl) >= battle_config.atc_slave_clone_limit) {
			clif_displaymessage(fd, msg_txt(127));	// You've reached your slave clones limit.
			return 0;
		}
	}

	do {
		x = sd->bl.x + (rand() % 10 - 5);
		y = sd->bl.y + (rand() % 10 - 5);
	} while (map_getcell(sd->bl.m,x,y,CELL_CHKNOPASS) && i++ < 10);

	if (i >= 10) {
		x = sd->bl.x;
		y = sd->bl.y;
	}

	if((x = mob_clone_spawn(pl_sd, sd->bl.m, x, y, "", master, 0, flag?1:0, 0)) > 0) {
		clif_displaymessage(fd, msg_txt(128+flag*2));	// Evil Clone spawned. Clone spawned. Slave clone spawned.
		return 0;
	}
	clif_displaymessage(fd, msg_txt(129+flag*2));	// Unable to spawn evil clone. Unable to spawn clone. Unable to spawn slave clone.
	return 0;
}

/*===================================
 * Chat Channels [Zephyrus]
 *-----------------------------------*/
int atcommand_create(const int fd, struct map_session_data *sd, const char *command, const char *message)
{
	char room_name[NAME_LENGTH], room_pass[NAME_LENGTH];
	memset(room_name, '\0', sizeof(room_name));
	memset(room_pass, '\0', sizeof(room_pass));

	if( !battle_config.channel_system_enable )
	{
		clif_displaymessage(fd, "Chat Channel System disabled.");
		return -1;
	}

	if( !message || !*message || sscanf(message, "%23s %23[^\n]", room_name, room_pass) < 1 )
	{
		clif_displaymessage(fd, "Usage: @create <#channel_name> <optional_password>");
		return -1;
	}

	if( !channel_create(sd, room_name, trim(room_pass), CHN_USER, 38) )
		return -1;

	return 0;
}

int atcommand_join(const int fd, struct map_session_data *sd, const char *command, const char *message)
{
	char room_name[NAME_LENGTH], room_pass[NAME_LENGTH];
	memset(room_name, '\0', sizeof(room_name));
	memset(room_pass, '\0', sizeof(room_pass));

	if( !battle_config.channel_system_enable )
	{
		clif_displaymessage(fd, "Chat Channel System disabled.");
		return -1;
	}

	if( !message || !*message )
	{ // Checks if the user has been invited to another channel
		if( sd->channel_invite_timer != INVALID_TIMER )
		{
			channel_invite_accept(sd);
			return 0;
		}
		else
		{
			clif_displaymessage(fd, "Usage: @join <#channel_name> <optional_password>");
			return -1;
		}
	}

	if( sscanf(message, "%23s %23[^\n]", room_name, room_pass) < 1 )
	{
		clif_displaymessage(fd, "Usage: @join <#channel_name> <optional_password>");
		return -1;
	}

	if( DIFF_TICK(sd->canjoinchn_tick, gettick()) > 0 )
	{
		clif_displaymessage(sd->fd, msg_txt(807));
		return -1;
	}

	channel_join(sd, room_name, room_pass, false);
	return 0;
}

int atcommand_exit(const int fd, struct map_session_data *sd, const char *command, const char *message)
{
	if( !battle_config.channel_system_enable )
	{
		clif_displaymessage(fd, "Chat Channel System disabled.");
		return -1;
	}

	if( !message || !*message )
	{
		clif_displaymessage(fd, "Usage: @exit <#channel_name>");
		return -1;
	}

	channel_leave(sd, message, true);
	return 0;
}

int atcommand_list(const int fd, struct map_session_data *sd, const char *command, const char *message)
{
	if( !battle_config.channel_system_enable )
	{
		clif_displaymessage(fd, "Chat Channel System disabled.");
		return -1;
	}

	channel_list(sd);
	return 0;
}

/*=====================================
 * Autorejecting Invites/Deals [LuzZza]
 * Usage: @noask
 *-------------------------------------*/
int atcommand_noask(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	if(sd->state.noask) {
		clif_displaymessage(fd, msg_txt(391)); // Autorejecting is deactivated.
		sd->state.noask = 0;
	} else {
		clif_displaymessage(fd, msg_txt(390)); // Autorejecting is activated.
		sd->state.noask = 1;
	}
	
	return 0;
}

/*=====================================
 * Send a @request message to all GMs of lowest_gm_level.
 * Usage: @request <petition>
 *-------------------------------------*/
int atcommand_request(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	if (!message || !*message) {
		clif_displaymessage(sd->fd,msg_txt(277));	// Usage: @request <petition/message to online GMs>.
		return -1;
	}

	sprintf(atcmd_output, msg_txt(278), message);	// (@request): %s
	intif_wis_message_to_gm(sd->status.name, battle_config.lowest_gm_level, atcmd_output);
	clif_disp_onlyself(sd, atcmd_output, strlen(atcmd_output));
	clif_displaymessage(sd->fd,msg_txt(279));	// @request sent.
	return 0;
}

/*==========================================
 * Feel (SG save map) Reset [HiddenDragon]
 *------------------------------------------*/
int atcommand_feelreset(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	pc_resetfeel(sd);
	clif_displaymessage(fd, "Reset 'Feeling' maps.");

	return 0;
}

/*==========================================
 * Modo PK para jugadores - Duelos abiertos sin necesidad de crear duelo
 *------------------------------------------*/
int atcommand_pvpmode(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	nullpo_retr(-1, sd);

	// Para desactivar el comando en mapas PVP
	if ((map[sd->bl.m].flag.pvp) && (40 >= pc_isGM(sd))) { // El Numero en rojo, quiere decir que solo los GM con el Nivel 10 o superior podr�n utilizar el comando en los mapas.
  	clif_displaymessage(sd->fd,"Lo siento, no puedes utilizar este comando en un mapa PVP.");
  	return -1;
	}

	// Para desactivar el comando en un Mapa GVG.
	if ((map[sd->bl.m].flag.gvg) && (40 >= pc_isGM(sd))) { // El Numero en rojo, quiere decir que solo los GM con el Nivel 10 o superior podr�n utilizar el comando en los mapas.
  	clif_displaymessage(sd->fd,"Lo Siento, no puedes utilizar este comando en un mapa de GVG.");
  	return -1;
	}

    //if( sd->duel_group > 0 && !sd->state.pvpmode )
	//{
    	//clif_displaymessage(fd, msg_txt(848));
    	//return -1;
    //}

	if( sd->state.pvpmode )
		pc_pvpmodeoff(sd,0,1);
	else
		pc_pvpmode(sd);

	return 0;
}

/*==========================================
 * Comando buscador de Personas en Modo PK
 *------------------------------------------*/
int atwhopk_timer(int tid, unsigned int tick, int id, int data)
{
	struct map_session_data *sd;

	if (!session[id] || (sd = session[id]->session_data) == NULL)
		return 0;

	clif_viewpoint(sd, 1, 2, 0, 0, data, 0xFF0000);
	return 1;
}

static int atwhopk_sub(struct block_list *bl,va_list ap)
{
	int fd;
	struct map_session_data *sd, *pl;
	static int number=0;
	char pl_info[100];

	if( !ap )
	{
		number = 0;
		return 0;
	}
	fd = va_arg(ap,int);
	sd = va_arg(ap,struct map_session_data*);

	pl = (TBL_PC*)bl; // Esto lo retorna el Map_foreachinmap

	if( !pl->state.pvpmode || pl == sd )
		return 0; // Ignora jugadores que no est�n en Modo PK o al Mismo Jugador

	if( fd )
	{
		sprintf(pl_info, msg_txt(849), pl->status.name, bl->x, bl->y, pl->status.pk.score);
		clif_disp_onlyself(sd ,(const char*)pl_info,(int)strlen((const char*)pl_info));
		clif_viewpoint(sd, 1, 1, bl->x, bl->y, ++number, 0xFF0000);
		add_timer(gettick() + 5000, atwhopk_timer, fd, number);
	}

	return 0;
}

int atcommand_whopk(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	int map_id;
	nullpo_retr(-1, sd);

	if( !sd->state.pvpmode )
	{
		clif_displaymessage(fd, msg_txt(850) );
		return -1;
	}

	map_id = sd->bl.m;
	clif_displaymessage(fd, msg_txt(851) );
	map_foreachinmap(atwhopk_sub, map_id, BL_PC, fd, sd);

	atwhopk_sub(&sd->bl,0);

	return 0;
}

/*==========================================
 * Comando para abrir el Storage adicional
 *------------------------------------------*/
int atcommand_rentstorage(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	nullpo_retr(-1, sd);

	if (sd->npc_id || sd->vender_id || sd->buyer_id || sd->state.trading || sd->state.storage_flag)
		return -1;

	if( premium_usage && map_flag_vs(sd->bl.m) )
		return -1;

	if (ext_storage_open(sd) == 1)
  	{	//Already open.
		clif_displaymessage(fd, msg_txt(250));
		return -1;
	}
	return 0;
}

/*==========================================
 * Auras personalizadas
 *------------------------------------------*/
int atcommand_aura(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	int type;

	// Para desactivar el comando en mapas PVP
	if ((map[sd->bl.m].flag.pvp) && (40 >= pc_isGM(sd))) { // El Numero en rojo, quiere decir que solo los GM con el Nivel 10 o superior podr�n utilizar el comando en los mapas.
  	clif_displaymessage(sd->fd,"Lo siento, no puedes utilizar este comando en un mapa PVP.");
  	return -1;
	}

	// Para desactivar el comando en mapas GVG
	if ((map[sd->bl.m].flag.gvg) && (40 >= pc_isGM(sd))) { // El Numero en rojo, quiere decir que solo los GM con el Nivel 10 o superior podr�n utilizar el comando en los mapas.
  	clif_displaymessage(sd->fd,"Lo siento, no puedes utilizar este comando en un mapa GVG/WOE.");
  	return -1;
	}

	// Para desactivar el comando en mapas BG
	if ((map[sd->bl.m].flag.battleground) && (40 >= pc_isGM(sd))) { // El Numero en rojo, quiere decir que solo los GM con el Nivel 10 o superior podr�n utilizar el comando en los mapas.
  	clif_displaymessage(sd->fd,"Lo siento, no puedes utilizar este comando en un mapa Battleground.");
  	return -1;
	}

	if(sd->duel_group > 0) {
		clif_displaymessage(fd, "No puedes usar @aura en modo Duel.");
		return 0;
	}

	if(sd->state.pvpmode) {
		clif_displaymessage(fd, "No puedes usar @aura en modo PK.");
		return 0;
	}

	type = atoi(message);
	sprintf(atcmd_output, "Current Aura: %d", sd->state.user_aura);
	clif_displaymessage(fd, atcmd_output);

	sd->state.user_aura = type;
	pc_setglobalreg(sd,"USERAURA",type);
	pc_setpos(sd, sd->mapindex, sd->bl.x, sd->bl.y, 3);

	return 0;
}

/*==========================================
 * AUCTION SYSTEM
 *------------------------------------------*/
int atcommand_auction(const int fd, struct map_session_data *sd, const char *command, const char *message)
{
	nullpo_retr(0,sd);

#ifndef TXT_ONLY
	clif_Auction_openwindow(sd);
#endif

	return 0;
}

/*==========================================
 * Kill Steal Protection
 *------------------------------------------*/
int atcommand_ksprotection(const int fd, struct map_session_data *sd, const char *command, const char *message)
{
	nullpo_retr(-1,sd);

	if( sd->state.noks )
	{
		sd->state.noks = 0;
		sprintf(atcmd_output, msg_txt(857));
	}
	else
	{
		if( !message || !*message || !strcmpi(message, "party") )
		{ // Default is Party
			sd->state.noks = 2;
			sprintf(atcmd_output, msg_txt(858));
		}
		else if( !strcmpi(message, "self") )
		{
			sd->state.noks = 1;
			sprintf(atcmd_output, msg_txt(859));
		}
		else if( !strcmpi(message, "guild") )
		{
			sd->state.noks = 3;
			sprintf(atcmd_output, msg_txt(860));
		}
		else
			sprintf(atcmd_output, msg_txt(861));
	}

	clif_displaymessage(fd, atcmd_output);
	return 0;
}
/*==========================================
 * Map Kill Steal Protection Setting
 *------------------------------------------*/
int atcommand_allowks(const int fd, struct map_session_data *sd, const char *command, const char *message)
{
	nullpo_retr(-1,sd);

	if( map[sd->bl.m].flag.allowks )
	{
		map[sd->bl.m].flag.allowks = 0;
		sprintf(atcmd_output, msg_txt(862));
	}
	else
	{
		map[sd->bl.m].flag.allowks = 1;
		sprintf(atcmd_output, msg_txt(863));
	}

	clif_displaymessage(fd, atcmd_output);
	return 0;
}

int atcommand_resetstat(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	nullpo_retr(-1, sd);
	
	pc_resetstate(sd);
	sprintf(atcmd_output, msg_txt(207), sd->status.name);
	clif_displaymessage(fd, atcmd_output);
	return 0;
}

int atcommand_resetskill(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	nullpo_retr(-1,sd);
	
	pc_resetskill(sd,1);
	sprintf(atcmd_output, msg_txt(206), sd->status.name);
	clif_displaymessage(fd, atcmd_output);
	return 0;
}

/*==========================================
 * #storagelist: Displays the items list of a player's storage.
 * #cartlist: Displays contents of target's cart.
 * #itemlist: Displays contents of target's inventory.
 *------------------------------------------*/
int atcommand_itemlist(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	int i, j, count, counter;
	const char* location;
	const struct item* items;
	int size;
	StringBuf buf;

	nullpo_retr(-1, sd);

	if( strcmp(command+1, "storagelist") == 0 )
	{
		location = "storage";
		items = sd->status.storage.items;
		size = MAX_STORAGE;
	}
	else
	if( strcmp(command+1, "cartlist") == 0 )
	{
		location = "cart";
		items = sd->status.cart;
		size = MAX_CART;
	}
	else
	if( strcmp(command+1, "itemlist") == 0 )
	{
		location = "inventory";
		items = sd->status.inventory;
		size = MAX_INVENTORY;
	}
	else
		return 1;

	StringBuf_Init(&buf);

	count = 0; // total slots occupied
	counter = 0; // total items found
	for( i = 0; i < size; ++i )
	{
		const struct item* it = &items[i];
		struct item_data* itd;

		if( it->nameid == 0 || (itd = itemdb_search(it->nameid)) == NULL )
			continue;

		counter += it->amount;
		count++;

		if( count == 1 )
		{
			StringBuf_Printf(&buf, "------ %s items list of '%s' ------", location, sd->status.name);
			clif_displaymessage(fd, StringBuf_Value(&buf));
			StringBuf_Clear(&buf);
		}

		if( it->refine )
			StringBuf_Printf(&buf, "%d %s %+d (%s, id: %d)", it->amount, itd->jname, it->refine, itd->name, it->nameid);
		else
			StringBuf_Printf(&buf, "%d %s (%s, id: %d)", it->amount, itd->jname, itd->name, it->nameid);

		if( it->equip )
		{
			char equipstr[CHAT_SIZE_MAX];
			strcpy(equipstr, " | equipped: ");
			if( it->equip & EQP_GARMENT )
				strcat(equipstr, "garment, ");
			if( it->equip & EQP_ACC_L )
				strcat(equipstr, "left accessory, ");
			if( it->equip & EQP_ARMOR )
				strcat(equipstr, "body/armor, ");
			if( (it->equip & EQP_ARMS) == EQP_HAND_R )
				strcat(equipstr, "right hand, ");
			if( (it->equip & EQP_ARMS) == EQP_HAND_L )
				strcat(equipstr, "left hand, ");
			if( (it->equip & EQP_ARMS) == EQP_ARMS )
				strcat(equipstr, "both hands, ");
			if( it->equip & EQP_SHOES )
				strcat(equipstr, "feet, ");
			if( it->equip & EQP_ACC_R )
				strcat(equipstr, "right accessory, ");
			if( (it->equip & EQP_HELM) == EQP_HEAD_LOW )
				strcat(equipstr, "lower head, ");
			if( (it->equip & EQP_HELM) == EQP_HEAD_TOP )
				strcat(equipstr, "top head, ");
			if( (it->equip & EQP_HELM) == (EQP_HEAD_LOW|EQP_HEAD_TOP) )
				strcat(equipstr, "lower/top head, ");
			if( (it->equip & EQP_HELM) == EQP_HEAD_MID )
				strcat(equipstr, "mid head, ");
			if( (it->equip & EQP_HELM) == (EQP_HEAD_LOW|EQP_HEAD_MID) )
				strcat(equipstr, "lower/mid head, ");
			if( (it->equip & EQP_HELM) == EQP_HELM )
				strcat(equipstr, "lower/mid/top head, ");
			// remove final ', '
			equipstr[strlen(equipstr) - 2] = '\0';
			StringBuf_AppendStr(&buf, equipstr);
		}

		clif_displaymessage(fd, StringBuf_Value(&buf));
		StringBuf_Clear(&buf);

		if( it->card[0] == CARD0_PET )
		{// pet egg
			if (it->card[3])
				StringBuf_Printf(&buf, " -> (pet egg, pet id: %u, named)", (unsigned int)MakeDWord(it->card[1], it->card[2]));
			else
				StringBuf_Printf(&buf, " -> (pet egg, pet id: %u, unnamed)", (unsigned int)MakeDWord(it->card[1], it->card[2]));
		}
		else
		if(it->card[0] == CARD0_FORGE)
		{// forged item
			StringBuf_Printf(&buf, " -> (crafted item, creator id: %u, star crumbs %d, element %d)", (unsigned int)MakeDWord(it->card[2], it->card[3]), it->card[1]>>8, it->card[1]&0x0f);
		}
		else
		if(it->card[0] == CARD0_CREATE)
		{// created item
			StringBuf_Printf(&buf, " -> (produced item, creator id: %u)", (unsigned int)MakeDWord(it->card[2], it->card[3]));
		}
		else
		{// normal item
			int counter2 = 0;

			for( j = 0; j < itd->slot; ++j )
			{
				struct item_data* card;

				if( it->card[j] == 0 || (card = itemdb_search(it->card[j])) == NULL )
					continue;

				counter2++;

				if( counter2 == 1 )
					StringBuf_AppendStr(&buf, " -> (card(s): ");

				if( counter2 != 1 )
					StringBuf_AppendStr(&buf, ", ");

				StringBuf_Printf(&buf, "#%d %s (id: %d)", counter2, card->jname, card->nameid);
			}

			if( counter2 > 0 )
				StringBuf_AppendStr(&buf, ")");
		}

		if( StringBuf_Length(&buf) > 0 )
			clif_displaymessage(fd, StringBuf_Value(&buf));

		StringBuf_Clear(&buf);
	}

	if( count == 0 )
		StringBuf_Printf(&buf, "No item found in this player's %s.", location);
	else
		StringBuf_Printf(&buf, "%d item(s) found in %d %s slots.", counter, count, location);

	clif_displaymessage(fd, StringBuf_Value(&buf));

	StringBuf_Destroy(&buf);

	return 0;
}

int atcommand_stats(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	char job_jobname[100];
	char output[CHAT_SIZE_MAX];
	int i;
	struct {
		const char* format;
		int value;
	} output_table[] = {
		{ "Base Level - %d", 0 },
		{ NULL, 0 },
		{ "Hp - %d", 0 },
		{ "MaxHp - %d", 0 },
		{ "Sp - %d", 0 },
		{ "MaxSp - %d", 0 },
		{ "Str - %3d", 0 },
		{ "Agi - %3d", 0 },
		{ "Vit - %3d", 0 },
		{ "Int - %3d", 0 },
		{ "Dex - %3d", 0 },
		{ "Luk - %3d", 0 },
		{ "Zeny - %d", 0 },
		{ "Free SK Points - %d", 0 },
		{ "Used SK Points - %d", 0 },
		{ "JobChangeLvl First Class - %d", 0 },
		{ "JobChangeLvl Second Class - %d", 0 },
		{ "Attack Speed MS - %d", 0 },
		{ NULL, 0 }
	};

	memset(job_jobname, '\0', sizeof(job_jobname));
	memset(output, '\0', sizeof(output));

	//direct array initialization with variables is not standard C compliant.
	output_table[0].value = sd->status.base_level;
	output_table[1].format = job_jobname;
	output_table[1].value = sd->status.job_level;
	output_table[2].value = sd->status.hp;
	output_table[3].value = sd->status.max_hp;
	output_table[4].value = sd->status.sp;
	output_table[5].value = sd->status.max_sp;
	output_table[6].value = sd->status.str;
	output_table[7].value = sd->status.agi;
	output_table[8].value = sd->status.vit;
	output_table[9].value = sd->status.int_;
	output_table[10].value = sd->status.dex;
	output_table[11].value = sd->status.luk;
	output_table[12].value = sd->status.zeny;
	output_table[13].value = sd->status.skill_point;
	output_table[14].value = pc_calc_skillpoint(sd);
	output_table[15].value = sd->change_level[0];
	output_table[16].value = sd->change_level[1];
	output_table[17].value = status_get_adelay(&sd->bl);

	sprintf(job_jobname, "Job - %s %s", job_name(sd->status.class_), "(level %d)");
	sprintf(output, msg_txt(53), sd->status.name); // '%s' stats:

	clif_displaymessage(fd, output);
	
	for (i = 0; output_table[i].format != NULL; i++) {
		sprintf(output, output_table[i].format, output_table[i].value);
		clif_displaymessage(fd, output);
	}

	return 0;
}

int atcommand_delitem(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	char item_name[100];
	int i, number = 0, item_id, item_position, count;
	struct item_data *item_data;
	char output[CHAT_SIZE_MAX];

	nullpo_retr(-1, sd);

	memset(item_name, '\0', sizeof(item_name));
	memset(output, '\0', sizeof(output));

	if (!message || !*message || (
		sscanf(message, "\"%99[^\"]\" %d", item_name, &number) < 2 &&
		sscanf(message, "%s %d", item_name, &number) < 2
	) || number < 1) {
		clif_displaymessage(fd, "Please, enter an item name/id, a quantity and a player name (usage: #delitem <item_name_or_ID> <quantity> <player>).");
		return -1;
	}

	item_id = 0;
	if ((item_data = itemdb_searchname(item_name)) != NULL ||
	    (item_data = itemdb_exists(atoi(item_name))) != NULL)
		item_id = item_data->nameid;
	
	if (item_id > 500) {
		item_position = pc_search_inventory(sd, item_id);
		if (item_position >= 0) {
			count = 0;
			for(i = 0; i < number && item_position >= 0; i++) {

				//Logs (A)dmins items [Lupus]
				if(log_config.enable_logs&0x400)
					log_pick_pc(sd, "A", sd->status.inventory[item_position].nameid, -1, &sd->status.inventory[item_position], sd->status.inventory[item_position].serial);

				pc_delitem(sd, item_position, 1, 0, 0);
				count++;
				item_position = pc_search_inventory(sd, item_id); // for next loop
			}
			sprintf(output, msg_txt(113), count); // %d item(s) removed by a GM.
			clif_displaymessage(sd->fd, output);
			if (number == count)
				sprintf(output, msg_txt(114), count); // %d item(s) removed from the player.
			else
				sprintf(output, msg_txt(115), count, count, number); // %d item(s) removed. Player had only %d on %d items.
			clif_displaymessage(fd, output);
		} else {
			clif_displaymessage(fd, msg_txt(116)); // Character does not have the item.
			return -1;
		}
	}
	else
	{
		clif_displaymessage(fd, msg_txt(19)); // Invalid item ID or name.
		return -1;
	}

	return 0;
}

/*==========================================
 * Exp Gain Information
 *------------------------------------------*/
void atcommand_expinfo_sub(int time, int* day, int* hour, int* minute, int* second)
{
	*day = time / 86400; time -= *day * 84600;
	*hour = time / 3600; time -= *hour * 3600;
	*minute = time / 60; time -= *minute * 60;
	*second = time;

	*day = *day > 0 ? *day : 0;
	*hour = *hour > 0 ? *hour : 0;
	*minute = *minute > 0 ? *minute : 0;
	*second = *second > 0 ? *second : 0;

	return;
}

int atcommand_expinfo(const int fd, struct map_session_data *sd, const char *command, const char *message)
{
	unsigned int nextb, nextj, bexp_ps, jexp_ps, nextbt, nextjt;
	int session_time, day = 0, hour = 0, minute = 0, second = 0;

	nullpo_retr(-1,sd);

	if( !strcmpi(message, "reset") )
	{ // Restart Vars
		sd->custom_data.session_base_exp = 0;
		sd->custom_data.session_job_exp = 0;
		sd->custom_data.session_start = last_tick;
	}
	else if( !strcmpi(message, "off") )
	{
		sd->state.showgain = 0;
		clif_displaymessage(fd, msg_txt(864));

		return 0;
	}
	else if( !strcmpi(message, "on") )
	{
		sd->state.showgain = 1;
		clif_displaymessage(fd, msg_txt(865));

		return 0;
	}
	else
		clif_displaymessage(fd, msg_txt(866));

	session_time = DIFF_TICK(last_tick, sd->custom_data.session_start);

	atcommand_expinfo_sub(session_time, &day, &hour, &minute, &second);
	sprintf(atcmd_output, msg_txt(867), day, hour, minute, second);
	clif_disp_onlyself(sd, atcmd_output, strlen(atcmd_output));

	if( session_time <= 0 )
		return 0;

	nextb = pc_nextbaseexp(sd);
	nextj = pc_nextjobexp(sd);

	sprintf(atcmd_output, msg_txt(868), sd->custom_data.session_base_exp, sd->custom_data.session_job_exp);
	clif_disp_onlyself(sd, atcmd_output, strlen(atcmd_output));

	if( nextb )
	{ // Next Base lvl information
		if( (bexp_ps = sd->custom_data.session_base_exp / session_time) < 1 )
			sprintf(atcmd_output, msg_txt(869), bexp_ps);
		else if( (nextbt = (nextb - sd->status.base_exp) / bexp_ps) < 604800 )
		{
			atcommand_expinfo_sub(nextbt, &day, &hour, &minute, &second);
			if( day )
				sprintf(atcmd_output, msg_txt(870), bexp_ps, day, hour, minute, second);
			else
				sprintf(atcmd_output, msg_txt(871), bexp_ps, hour, minute, second);
		}
		else
			sprintf(atcmd_output, msg_txt(869), bexp_ps);

		clif_disp_onlyself(sd, atcmd_output, strlen(atcmd_output));
	}

	if( nextj )
	{ // Next Job lvl information
		if( (jexp_ps = sd->custom_data.session_job_exp / session_time) < 1 )
			sprintf(atcmd_output, msg_txt(872), jexp_ps);
		else if( (nextjt = (nextj - sd->status.job_exp) / jexp_ps) < 604800 )
		{
			atcommand_expinfo_sub(nextjt, &day, &hour, &minute, &second);
			if( day )
				sprintf(atcmd_output, msg_txt(873), jexp_ps, day, hour, minute, second);
			else
				sprintf(atcmd_output, msg_txt(874), jexp_ps, hour, minute, second);
		}
		else
			sprintf(atcmd_output, msg_txt(872), jexp_ps);

		clif_disp_onlyself(sd, atcmd_output, strlen(atcmd_output));
	}

	return 0;
}
/*==========================================
 * Hunting Mission [Zephyrus]
 *------------------------------------------*/
int atcommand_mission(const int fd, struct map_session_data *sd, const char *command, const char *message)
{
	int i, c = 0;
	nullpo_retr(-1,sd);

	if( sd->hunting[0].mob_id == 0 )
	{
		clif_displaymessage(fd, "You are not hunting.");
		return 0;
	}

	for( i = 0; i < 5; i++ )
	{
		if( sd->hunting[i].count < 1 )
			continue;

		sprintf(atcmd_output, "- Hunting %d '%s' (ID : %d)", sd->hunting[i].count, mob_db(sd->hunting[i].mob_id)->jname, sd->hunting[i].mob_id);
		clif_displaymessage(fd, atcmd_output);

		c = sd->hunting[i].count;
	}

	if( c < 1 )
		clif_displaymessage(fd, "Hunting Mission Completed");
	else if( sd->hunting_time < (int)time(NULL) )
		clif_displaymessage(fd, "Hunting Mission Failed");
	else
	{
		int tick = sd->hunting_time - (int)time(NULL),
			day, hour, minute, second;

		atcommand_expinfo_sub(tick, &day, &hour, &minute, &second);
		sprintf(atcmd_output, "Time Left : %02d:%02d:%02d", hour, minute, second);
		clif_displaymessage(fd, atcmd_output);
	}

	return 0;
}

/*==========================================
 * Custom Fonts
 *------------------------------------------*/
int atcommand_font(const int fd, struct map_session_data *sd, const char *command, const char *message)
{
	int font_id;
	nullpo_retr(-1,sd);

	font_id = atoi(message);
	if( font_id == 0 )
	{
		if( sd->state.user_font )
		{
			sd->state.user_font = 0;
			clif_displaymessage(fd, "Returning to normal font.");
			clif_font(sd);
		}
		else
		{
			clif_displaymessage(fd, "Use @font <1..9> to change your messages font.");
			clif_displaymessage(fd, "Use 0 or no parameter to back to normal font.");
		}
	}
	else if( font_id < 0 || font_id > 9 )
		clif_displaymessage(fd, "Invalid font. Use a Value from 0 to 9.");
	else if( font_id != sd->state.user_font )
	{
		sd->state.user_font = font_id;
		clif_font(sd);
		clif_displaymessage(fd, "Font changed.");
	}
	else
		clif_displaymessage(fd, "Already using this font.");

	return 0;
}

/*==========================================
 * Char Data Backup Generation.
 *------------------------------------------*/
int atcommand_char2dump(const int fd, struct map_session_data *sd, const char *command, const char *message)
{
	int char_id;

	nullpo_retr(-1,sd);

	if( (char_id = atoi(message)) <= 0 )
		return -1;

	chrif_char2dumpfile(char_id);
	clif_displaymessage(fd, "Requesting char server to do a backup of Char data");

	return 0;
}

/*=========================================
 * Item Security System
 *-----------------------------------------*/
int atcommand_security(const int fd, struct map_session_data *sd, const char *command, const char *message)
{
	nullpo_retr(-1,sd);
	if( sd->npc_id || sd->vender_id || sd->buyer_id || sd->state.trading || sd->state.storage_flag )
		return -1;

	npc_event(sd,"SecuritySystem::OnSettings",0);
	return 0;
}

/*==========================================
 * Packet Filter
 *------------------------------------------*/
int atcommand_packetfilter(const int fd, struct map_session_data *sd, const char *command, const char *message)
{
	nullpo_retr(-1,sd);
	if( !message || !*message )
	{
		clif_displaymessage(fd,"<<----- Packet Filtering Usage ----->>");
		clif_displaymessage(fd,".   @packetfilter <options>");
		clif_displaymessage(fd,".   C : To filter global chat messages.");
		clif_displaymessage(fd,".   I : To filter item usage.");
		clif_displaymessage(fd,".   - Samples");
		clif_displaymessage(fd,".   @packetfilter CI : To filter the 2 options.");
		clif_displaymessage(fd,".   @packetfilter off : To turn packet filter off.");
	}
	else if( !strcmpi(message,"off") )
	{
		sd->state.packet_filter = 0;
		clif_displaymessage(fd,"<< Packet Filtering Off >>");
	}
	else
	{
		if( strstr(message,"C") )
			sd->state.packet_filter |= 1;
		if( strstr(message,"I") )
			sd->state.packet_filter |= 2;

		sprintf(atcmd_output,"<< Packet Filtering | Chat %s | Items %s >>", (sd->state.packet_filter&1) ? "ON" : "OFF", (sd->state.packet_filter&2) ? "ON" : "OFF");
		clif_displaymessage(fd,atcmd_output);
	}

	return 0;
}

/*==========================================
 * Network Information
 *------------------------------------------*/
int atcommand_netinfo(const int fd, struct map_session_data *sd, const char *command, const char *message)
{
	uint32 ip;

	nullpo_retr(-1,sd);
	sprintf(atcmd_output, "Network Information Request for %s (AID %d | CID %d)", sd->status.name, sd->status.account_id, sd->status.char_id);
	clif_displaymessage(fd,atcmd_output);
	sprintf(atcmd_output, "- Mac Address : %s", sd->mac_address);
	clif_displaymessage(fd,atcmd_output);

	ip = session[sd->fd]->client_addr;
	sprintf(atcmd_output, "- IP Address : %d.%d.%d.%d", CONVIP(ip));
	clif_displaymessage(fd,atcmd_output);

	return 0;
}

/*==========================================
 * Guild Skill Usage for Guild Masters
 *------------------------------------------*/
int atcommand_guildskill(const int fd, struct map_session_data *sd, const char *command, const char *message)
{
	int i, skillnum = 0, skilllv = 0, idx;
	unsigned int tick = gettick();
	struct battleground_data *bg;
	struct guild *g;

	const struct { char skillstr[3]; int id; } skills[] = {
		{ "BO",	10010 },
		{ "RG",	10011 },
		{ "RS",	10012 },
		{ "EC",	10013 },
	};

	// Check for Skill ID
	for( i = 0; i < ARRAYLENGTH(skills); i++ )
	{
		if( strncmpi(message, skills[i].skillstr, 3) == 0 )
		{
			skillnum = skills[i].id;
			break;
		}
	}
	if( !skillnum )
	{
		clif_displaymessage(fd, "Invalid Skill string. Use @guildskill EC/RS/RG/BO");
		return -1;
	}

	idx = battle_config.guild_skills_separed_delay ? skillnum - GD_SKILLBASE : 0;
	if( !map[sd->bl.m].flag.battleground )
	{
		if( (g = sd->state.gmaster_flag) != NULL )
		{
			if( g->skill_block_timer[idx] == INVALID_TIMER )
				skilllv = guild_checkskill(g, skillnum);
			else
			{
				guild_block_skill_status(g, skillnum);
				skilllv = 0;
			}
		}
		else
		{
			clif_displaymessage(fd, "This command is reserved for Guild Leaders Only.");
			return -1;
		}
	}
	else
	{
		if( (bg = sd->state.bmaster_flag) != NULL )
		{
			if( bg->skill_block_timer[idx] == INVALID_TIMER )
				skilllv = bg_checkskill(bg, skillnum);
			else
			{
				bg_block_skill_status(bg, skillnum);
				skilllv = 0;
			}
		}
		else
		{
			clif_displaymessage(fd, "This command is reserved for Team Leaders Only.");
			return -1;
		}
	}

	if( pc_cant_act(sd) || pc_issit(sd) || skillnotok(skillnum, sd) || sd->ud.skilltimer != -1 || sd->sc.option&(OPTION_WEDDING|OPTION_XMAS|OPTION_SUMMER) || sd->state.only_walk || sd->sc.data[SC_BASILICA] || sd->state.harmony_block )
		return -1;

	if( DIFF_TICK(tick, sd->ud.canact_tick) < 0 )
		return -1;

	if( sd->menuskill_id )
	{
		if( sd->menuskill_id == SA_TAMINGMONSTER )
			sd->menuskill_id = sd->menuskill_val = 0; //Cancel pet capture.
		else if( sd->menuskill_id != SA_AUTOSPELL )
			return -1; //Can't use skills while a menu is open.
	}

	sd->skillitem = sd->skillitemlv = 0;
	if( skilllv ) unit_skilluse_id(&sd->bl, sd->bl.id, skillnum, skilllv);
	return 0;
}

/*==========================================
 * Battleground Leader Commands
 *------------------------------------------*/
int atcommand_order(const int fd, struct map_session_data *sd, const char *command, const char *message)
{
	nullpo_retr(-1,sd);
	if( !message || !*message )
	{
		clif_displaymessage(fd, "Please, enter a message (usage: @order <message>).");
		return -1;
	}

	if( map[sd->bl.m].flag.battleground )
	{
		if( !sd->state.bmaster_flag )
		{
			clif_displaymessage(fd, "This command is reserved for Team Leaders Only.");
			return -1;
		}
		clif_broadcast2(&sd->bl, message, (int)strlen(message)+1, sd->state.bmaster_flag->color, 0x190, 20, 0, 0, BG);
	}
	else
	{
		if( !sd->state.gmaster_flag )
		{
			clif_displaymessage(fd, "This command is reserved for Guild Leaders Only.");
			return -1;
		}
		clif_broadcast2(&sd->bl, message, (int)strlen(message)+1, 0xFF0000, 0x190, 20, 0, 0, GUILD);
	}

	return 0;
}

int atcommand_leader(const int fd, struct map_session_data *sd, const char *command, const char *message)
{
	struct map_session_data *pl_sd;
	nullpo_retr(-1,sd);
	if( !sd->state.bmaster_flag )
		clif_displaymessage(fd, "This command is reserved for Team Leaders Only.");
	else if( sd->ud.skilltimer != INVALID_TIMER )
		clif_displaymessage(fd, "Command not allow while casting a skill.");
	else if( !message || !*message )
		clif_displaymessage(fd, "Please, enter the new Leader name (usage: @leader <name>).");
	else if( (pl_sd = map_nick2sd((char *)message)) == NULL )
		clif_displaymessage(fd, msg_txt(3)); // Character not found.
	else if( sd->state.bg_id != pl_sd->state.bg_id )
		clif_displaymessage(fd, "Destination Player is not in your Team.");
	else if( sd == pl_sd )
		clif_displaymessage(fd, "You are already the Team Leader.");
	else
	{ // Everytest OK!
		sprintf(atcmd_output, "Team Leader transfered to [%s]", pl_sd->status.name);
		clif_broadcast2(&sd->bl, atcmd_output, (int)strlen(atcmd_output)+1, sd->state.bmaster_flag->color, 0x190, 20, 0, 0, BG);

		sd->state.bmaster_flag->leader_char_id = pl_sd->status.char_id;
		pl_sd->state.bmaster_flag = sd->state.bmaster_flag;
		sd->state.bmaster_flag = NULL;

		clif_charnameupdate(sd);
		clif_charnameupdate(pl_sd);
		return 0;
	}
	return -1;
}

int atcommand_reportafk(const int fd, struct map_session_data *sd, const char *command, const char *message)
{
	struct map_session_data *pl_sd;
	nullpo_retr(-1,sd);
	if( !sd->state.bmaster_flag )
		clif_displaymessage(fd, "This command is reserved for Team Leaders Only.");
	else if( !message || !*message )
		clif_displaymessage(fd, "Please, enter the character name (usage: @reportafk <name>).");
	else if( (pl_sd = map_nick2sd((char *)message)) == NULL )
		clif_displaymessage(fd, msg_txt(3)); // Character not found.
	else if( sd->state.bg_id != pl_sd->state.bg_id )
		clif_displaymessage(fd, "Destination Player is not in your Team.");
	else if( sd == pl_sd )
		clif_displaymessage(fd, "You cannot kick yourself.");
	else if( pl_sd->state.bg_afk == 0 )
		clif_displaymessage(fd, "The player is not AFK on this Battleground.");
	else
	{ // Everytest OK!
		bg_team_leave(pl_sd,2);
		clif_displaymessage(pl_sd->fd, "You have been kicked from Battleground because of your AFK status.");
		pc_setpos(pl_sd,pl_sd->status.save_point.map,pl_sd->status.save_point.x,pl_sd->status.save_point.y,3);

		sprintf(atcmd_output, "- AFK [%s] Kicked -", pl_sd->status.name);
		clif_broadcast2(&sd->bl, atcmd_output, (int)strlen(atcmd_output)+1, sd->state.bmaster_flag->color, 0x190, 20, 0, 0, BG);
		return 0;
	}
	return -1;
}

int atcommand_unboundall(const int fd, struct map_session_data *sd, const char *command, const char *message)
{
	struct map_session_data *pl_sd;
	nullpo_retr(-1,sd);
	if( !message || !*message )
		clif_displaymessage(fd, "Please, enter the character name (usage: @unboundall <name>).");
	else if( (pl_sd = map_nick2sd((char *)message)) == NULL )
		clif_displaymessage(fd, msg_txt(3)); // Character not found.
	else
	{ // Start Unbound process
		int i, c = 0;
		for( i = 0; i < MAX_INVENTORY; i++ )
		{
			if( pl_sd->status.inventory[i].nameid && pl_sd->status.inventory[i].bound )
			{
				pl_sd->status.inventory[i].bound = 0;
				c++;
			}
		}

		if( c )
		{
			sprintf(atcmd_output,"Unbounded %d inventory items.", c);
			clif_displaymessage(fd, atcmd_output);
			c = 0;
		}

		for( i = 0; i < MAX_CART; i++ )
		{
			if( pl_sd->status.cart[i].nameid && pl_sd->status.cart[i].bound )
			{
				pl_sd->status.cart[i].bound = 0;
				c++;
			}
		}

		if( c )
		{
			sprintf(atcmd_output,"Unbounded %d cart items.", c);
			clif_displaymessage(fd, atcmd_output);
			c = 0;
		}

		for( i = 0; i < MAX_STORAGE; i++ )
		{
			if( pl_sd->status.storage.items[i].nameid && pl_sd->status.storage.items[i].bound )
			{
				pl_sd->status.storage.items[i].bound = 0;
				c++;
			}
		}

		if( c )
		{
			sprintf(atcmd_output,"Unbounded %d storage items.", c);
			clif_displaymessage(fd, atcmd_output);
			c = 0;
		}

		for( i = 0; i < MAX_EXTRA_STORAGE; i++ )
		{
			if( pl_sd->status.ext_storage.items[i].nameid && pl_sd->status.ext_storage.items[i].bound )
			{
				pl_sd->status.ext_storage.items[i].bound = 0;
				c++;
			}
		}

		if( c )
		{
			sprintf(atcmd_output,"Unbounded %d ext storage items.", c);
			clif_displaymessage(fd, atcmd_output);
			c = 0;
		}

		clif_refresh(pl_sd);
		return 0;
	}
	return -1;
}

int atcommand_bgranked(const int fd, struct map_session_data *sd, const char *command, const char *message)
{
	int i;

	clif_displaymessage(fd, "============= RANKED BATTLEGROUND FAME LIST =============");
	for( i = 0; i < MAX_FAME_LIST && bgrank_fame_list[i].id; i++ )
	{
		sprintf(atcmd_output,"%d - %s - %d points",i+1,bgrank_fame_list[i].name,bgrank_fame_list[i].fame);
		clif_displaymessage(fd, atcmd_output);
	}
	clif_displaymessage(fd, "============= RANKED BATTLEGROUND FAME LIST =============");
	return 0;
}

int atcommand_bgregular(const int fd, struct map_session_data *sd, const char *command, const char *message)
{
	int i;

	clif_displaymessage(fd, "============= REGULAR BATTLEGROUND FAME LIST =============");
	for( i = 0; i < MAX_FAME_LIST && bg_fame_list[i].id; i++ )
	{
		sprintf(atcmd_output,"%d - %s - %d points",i+1,bg_fame_list[i].name,bg_fame_list[i].fame);
		clif_displaymessage(fd, atcmd_output);
	}
	clif_displaymessage(fd, "============= REGULAR BATTLEGROUND FAME LIST =============");
	return 0;
}

int atcommand_battleinfo(const int fd, struct map_session_data *sd, const char *command, const char *message)
{
	if( sd->state.battleinfo )
	{
		clif_displaymessage(fd, "- Battle Information Display OFF - Kill/Death -");
		sd->state.battleinfo = 0;
	}
	else
	{
		clif_displaymessage(fd, "- Battle Information Display ON - Kill/Death -");
		sd->state.battleinfo = 1;
	}
	return 0;
}

/*==========================================
 * CashShop System
 *------------------------------------------*/
int atcommand_cashshop(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	if( !pc_iscarton(sd) )
	{
		clif_displaymessage(fd, "A Cart is required to open a Cash Shop.");
		return -1;
	}
	if( battle_config.super_woe_enable )
	{
		clif_displaymessage(fd, "Vending not available on Super WoE / GvG Events.");
		return -1;
	}
	if( map[sd->bl.m].flag.novending )
	{
		clif_displaymessage(fd, "You can't open shop on this map");
		return -1;
	}
	if( map[sd->bl.m].flag.vending_cell != map_getcell(sd->bl.m,sd->bl.x,sd->bl.y,CELL_CHKNOVENDING) )
	{
		clif_displaymessage(fd, "You can't open shop on this cell");
		return -1;
	}
	if( sd->state.secure_items )
	{
		clif_displaymessage(fd, "You can't open shop. Blocked with @security");
		return -1;
	}
	if ( !pc_can_give_items(pc_isGM(sd)) )
	{
		clif_displaymessage(fd, "Your GM level don't allow you to give items.");
		return -1;
	}

	sd->state.cashshop = 1; // [CashShop System]
	clif_openvendingreq(sd,12);
	clif_displaymessage(fd, "You are opening a Cash Shop. Remember, all Zeny amounts will be converted to Cash Points.");
	return 0;
}

/*==========================================
 * Ranking Reset
 *------------------------------------------*/
int atcommand_rankreset(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	int type;
	nullpo_retr(-1, sd);

	if( (type = atoi(message)) < 1 || type > 3 )
	{
		clif_displaymessage(fd, "Enter reset rank type. Usage: @rankreset <n>");
		clif_displaymessage(fd, "n = 1 WoE | 2 BG | 3 PVP");
		return -1;
	}

	pc_ranking_reset(type - 1, true);
	clif_displaymessage(fd, "Reseting Ranking...");
	return 0;
}

/*==========================================
 * Item Remover
 *------------------------------------------*/
int atcommand_item_remove4all(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	int nameid;
	nullpo_retr(-1, sd);

	if( (nameid = atoi(message)) < 500 || !itemdb_exists(nameid) )
	{
		clif_displaymessage(fd, "Enter a Valid Item ID. Usage: @itemdestroy <itemid>");
		return -1;
	}

	pc_item_remove4all(nameid,true);
	clif_displaymessage(fd, "Destroying Item...");
	return 0;
}

/*==========================================
 * Fame Points
 *------------------------------------------*/
int atcommand_addfame(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	int fame = 0, type = 0;
	nullpo_retr(-1, sd);

	if( !message || !*message || sscanf(message, "%d %d", &type, &fame) < 2 )
	{
		clif_displaymessage(fd, "Usage: @addfame <type> <points>");
		clif_displaymessage(fd, "- type : 0 Class (Alche/BS/TK) | 1 PK | 2 BG Ranked | 3 BG Regular");

		return -1;
	}

	if( type < 0 || type > 3 )
	{
		clif_displaymessage(fd, "Invalid type : 0 Class (Alche/BS/TK) | 1 PK | 2 BG Ranked | 3 BG Regular");
		return -1;
	}

	if( fame <= 0 )
	{
		clif_displaymessage(fd, "Invalid points. Positive value required.");
		return -1;
	}

	pc_addfame(sd,fame,type);
	return 0;
}

/*==========================================
 * Visual Mob Info Mode
 *------------------------------------------*/
static int atcommand_viewmobinfo_sub(struct block_list *bl, va_list ap)
{
	struct map_session_data *sd;
	struct mob_data *md;

	nullpo_retr(0,bl);
	nullpo_retr(0,md = (struct mob_data *)bl);
	nullpo_retr(0,sd = va_arg(ap, struct map_session_data *));

	clif_mobnameack(sd,md,0);
	return 0;
}

int atcommand_viewmobinfo(const int fd, struct map_session_data *sd, const char *command, const char *message)
{
	nullpo_retr(-1,sd);
	if( sd->state.view_mob_info )
	{
		sd->state.view_mob_info = 0;
		clif_displaymessage(fd,"* Viewing mob info data - Disabled *");
	}
	else
	{
		sd->state.view_mob_info = 1;
		clif_displaymessage(fd,"* Viewing mob info data - Enable *");
	}

	map_foreachinarea(atcommand_viewmobinfo_sub, sd->bl.m, sd->bl.x - AREA_SIZE, sd->bl.y - AREA_SIZE, sd->bl.x + AREA_SIZE, sd->bl.y + AREA_SIZE, BL_MOB, sd);
	return 0;
}

/*==========================================
* @dance by OnNplay
* inspired by Anarchist
* => Special effects with dance style
*------------------------------------------
*/
ACMD_FUNC(dance)
{
   nullpo_retr(-1, sd);

   if (!message || !*message) {
 clif_displaymessage(fd, "usage: @dance 1-9");
 return -1;
   }
   if ( atoi(message) == 1 ) {
 clif_specialeffect(&sd->bl, 413, 0);
   } else if ( atoi(message) == 2 ) {
 clif_specialeffect(&sd->bl, 414, 0);
   } else if ( atoi(message) == 3 ) {
 clif_specialeffect(&sd->bl, 415, 0);
   } else if ( atoi(message) == 4 ) {
 clif_specialeffect(&sd->bl, 426, 0);
   } else if ( atoi(message) == 5 ) {
 clif_specialeffect(&sd->bl, 458, 0);
   } else if ( atoi(message) == 6 ) {
 clif_specialeffect(&sd->bl, 466, 0);
   } else if ( atoi(message) == 7 ) {
 clif_specialeffect(&sd->bl, 501, 0);
   } else if ( atoi(message) == 8 ) {
 clif_specialeffect(&sd->bl, 540, 0);
   } else if ( atoi(message) == 9 ) {
 clif_specialeffect(&sd->bl, 550, 0);
   }
   return 0;
}

/*==========================================
 * atcommand_info[] structure definition
 *------------------------------------------*/

AtCommandInfo atcommand_info[] = {
	{ "partybuff",         	1,1,   	0,     atcommand_partybuff },
	{ "spb",               	1,1,   	0,     atcommand_partybuff },
	{ "rura",              40,40,   0,     atcommand_mapmove },
	{ "warp",              40,40,   0,     atcommand_mapmove },
	{ "mapmove",           40,40,   0,     atcommand_mapmove }, // + /mm
	{ "where",              1,1,    0,     atcommand_where },
	{ "jumpto",            20,20,   0,     atcommand_jumpto }, // + /shift
	{ "warpto",            20,20,   0,     atcommand_jumpto },
	{ "goto",              20,20,   0,     atcommand_jumpto },
	{ "jump",              40,40,   0,     atcommand_jump },
	{ "who",               20,20,   0,     atcommand_who },
	{ "whois",             20,20,   0,     atcommand_who },
	{ "who2",              20,20,   0,     atcommand_who2 },
	{ "who3",              20,20,   0,     atcommand_who3 },
	{ "whomap",            20,20,   0,     atcommand_whomap },
	{ "whomap2",           20,20,   0,     atcommand_whomap2 },
	{ "whomap3",           20,20,   0,     atcommand_whomap3 },
	{ "whogm",             20,20,   0,     atcommand_whogm },
	{ "save",              40,40,   0,     atcommand_save },
	{ "return",            40,40,   0,     atcommand_load },
	{ "load",              40,40,   0,     atcommand_load },
	{ "speed",             40,40,   0,     atcommand_speed },
	{ "storage",            1,1,    0,     atcommand_storage },
	{ "gstorage",          50,50,   0,     atcommand_guildstorage },
	{ "option",            40,40,   0,     atcommand_option },
	{ "hide",              40,40,   0,     atcommand_hide }, // + /hide
	{ "jobchange",         40,40,   0,     atcommand_jobchange },
	{ "job",               40,40,   0,     atcommand_jobchange },
	{ "die",                1,1,    0,     atcommand_die },
	{ "kill",              60,60,   0,     atcommand_kill },
	{ "alive",             60,60,   0,     atcommand_alive },
	{ "kami",              40,40,   0,     atcommand_kami },
	{ "kamib",             40,40,   0,     atcommand_kami },
	{ "kamic",             40,40,   0,     atcommand_kami },
	{ "heal",              40,60,   0,     atcommand_heal },
	{ "item",              60,60,   0,     atcommand_item },
	{ "bounditem",         99,99,   0,     atcommand_item },
	{ "item2",             60,60,   0,     atcommand_item2 },
	{ "bounditem2",        99,99,   0,     atcommand_item2 },
	{ "itemreset",         40,40,   0,     atcommand_itemreset },
	{ "blvl",              60,60,   0,     atcommand_baselevelup },
	{ "lvup",              60,60,   0,     atcommand_baselevelup },
	{ "blevel",            60,60,   0,     atcommand_baselevelup },
	{ "baselvl",           60,60,   0,     atcommand_baselevelup },
	{ "baselvup",          60,60,   0,     atcommand_baselevelup },
	{ "baselevel",         60,60,   0,     atcommand_baselevelup },
	{ "baselvlup",         60,60,   0,     atcommand_baselevelup },
	{ "jlvl",              60,60,   0,     atcommand_joblevelup },
	{ "jlevel",            60,60,   0,     atcommand_joblevelup },
	{ "joblvl",            60,60,   0,     atcommand_joblevelup },
	{ "joblevel",          60,60,   0,     atcommand_joblevelup },
	{ "joblvup",           60,60,   0,     atcommand_joblevelup },
	{ "joblvlup",          60,60,   0,     atcommand_joblevelup },
	{ "h",                 20,20,   0,     atcommand_help },
	{ "help",              20,20,   0,     atcommand_help },
	{ "h2",                20,20,   0,     atcommand_help2 },
	{ "help2",             20,20,   0,     atcommand_help2 },
	{ "pvpoff",            40,40,   0,     atcommand_pvpoff },
	{ "pvpon",             40,40,   0,     atcommand_pvpon },
	{ "gvgoff",            40,40,   0,     atcommand_gvgoff },
	{ "gpvpoff",           40,40,   0,     atcommand_gvgoff },
	{ "gvgon",             40,40,   0,     atcommand_gvgon },
	{ "gpvpon",            40,40,   0,     atcommand_gvgon },
	{ "model",             20,20,   0,     atcommand_model },
	{ "go",                10,10,   0,     atcommand_go },
	{ "monster",           50,50,   0,     atcommand_monster },
	{ "spawn",             50,50,   0,     atcommand_monster },
	{ "monstersmall",      50,50,   0,     atcommand_monstersmall },
	{ "monsterbig",        50,50,   0,     atcommand_monsterbig },
	{ "killmonster",       60,60,   0,     atcommand_killmonster },
	{ "killmonster2",      40,40,   0,     atcommand_killmonster2 },
	{ "refine",            60,60,   0,     atcommand_refine },
	{ "produce",           60,60,   0,     atcommand_produce },
	{ "memo",              40,40,   0,     atcommand_memo },
	{ "gat",               99,99,   0,     atcommand_gat },
	{ "displaystatus",     99,99,   0,     atcommand_displaystatus },
	{ "stpoint",           60,60,   0,     atcommand_statuspoint },
	{ "skpoint",           60,60,   0,     atcommand_skillpoint },
	{ "zeny",              60,60,   0,     atcommand_zeny },
	{ "str",               60,60,   0,     atcommand_param },
	{ "agi",               60,60,   0,     atcommand_param },
	{ "vit",               60,60,   0,     atcommand_param },
	{ "int",               60,60,   0,     atcommand_param },
	{ "dex",               60,60,   0,     atcommand_param },
	{ "luk",               60,60,   0,     atcommand_param },
	{ "glvl",              60,60,   0,     atcommand_guildlevelup },
	{ "glevel",            60,60,   0,     atcommand_guildlevelup },
	{ "guildlvl",          60,60,   0,     atcommand_guildlevelup },
	{ "guildlvup",         60,60,   0,     atcommand_guildlevelup },
	{ "guildlevel",        60,60,   0,     atcommand_guildlevelup },
	{ "guildlvlup",        60,60,   0,     atcommand_guildlevelup },
	{ "makeegg",           60,60,   0,     atcommand_makeegg },
	{ "hatch",             60,60,   0,     atcommand_hatch },
	{ "petfriendly",       40,40,   0,     atcommand_petfriendly },
	{ "pethungry",         40,40,   0,     atcommand_pethungry },
	{ "petrename",          1,1,    0,     atcommand_petrename },
	{ "recall",            60,60,   0,     atcommand_recall }, // + /recall
	{ "night",             80,80,   0,     atcommand_night },
	{ "day",               80,80,   0,     atcommand_day },
	{ "doom",              80,80,   0,     atcommand_doom },
	{ "doommap",           80,80,   0,     atcommand_doommap },
	{ "raise",             80,80,   0,     atcommand_raise },
	{ "raisemap",          80,80,   0,     atcommand_raisemap },
	{ "kick",              20,20,   0,     atcommand_kick }, // + right click menu for GM "(name) force to quit"
	{ "kickall",           99,99,   0,     atcommand_kickall },
	{ "allskill",          60,60,   0,     atcommand_allskill },
	{ "allskills",         60,60,   0,     atcommand_allskill },
	{ "skillall",          60,60,   0,     atcommand_allskill },
	{ "skillsall",         60,60,   0,     atcommand_allskill },
	{ "questskill",        40,40,   0,     atcommand_questskill },
	{ "lostskill",         40,40,   0,     atcommand_lostskill },
	{ "spiritball",        40,40,   0,     atcommand_spiritball },
	{ "party",              1,1,    0,     atcommand_party },
	{ "guild",             50,50,   0,     atcommand_guild },
	{ "agitstart",         60,60,   0,     atcommand_agitstart },
	{ "agitend",           60,60,   0,     atcommand_agitend },
	{ "mapexit",           99,99,   0,     atcommand_mapexit },
	{ "idsearch",          60,60,   0,     atcommand_idsearch },
	{ "broadcast",         40,40,   0,     atcommand_broadcast }, // + /b and /nb
	{ "localbroadcast",    40,40,   0,     atcommand_localbroadcast }, // + /lb and /nlb
	{ "recallall",         80,80,   0,     atcommand_recallall },
	{ "reloadhashdb",      99,99,   0,     atcommand_reloadhashdb },
	{ "reloaditemdb",      99,99,   0,     atcommand_reloaditemdb },
	{ "reloadmobdb",       99,99,   0,     atcommand_reloadmobdb },
	{ "reloadskilldb",     99,99,   0,     atcommand_reloadskilldb },
	{ "reloadscript",      99,99,   0,     atcommand_reloadscript },
	{ "reloadatcommand",   99,99,   0,     atcommand_reloadatcommand },
	{ "reloadbattleconf",  99,99,   0,     atcommand_reloadbattleconf },
	{ "reloadstatusdb",    99,99,   0,     atcommand_reloadstatusdb },
	{ "reloadpcdb",        99,99,   0,     atcommand_reloadpcdb },
	{ "reloadmotd",        99,99,   0,     atcommand_reloadmotd },
	{ "mapinfo",           99,99,   0,     atcommand_mapinfo },
	{ "dye",               40,40,   0,     atcommand_dye },
	{ "ccolor",            40,40,   0,     atcommand_dye },
	{ "hairstyle",         40,40,   0,     atcommand_hair_style },
	{ "hstyle",            40,40,   0,     atcommand_hair_style },
	{ "haircolor",         40,40,   0,     atcommand_hair_color },
	{ "hcolor",            40,40,   0,     atcommand_hair_color },
	{ "statall",           60,60,   0,     atcommand_stat_all },
	{ "statsall",          60,60,   0,     atcommand_stat_all },
	{ "allstats",          60,60,   0,     atcommand_stat_all },
	{ "allstat",           60,60,   0,     atcommand_stat_all },
	{ "block",             60,60,   0,     atcommand_char_block },
	{ "charblock",         60,60,   0,     atcommand_char_block },
	{ "ban",               60,60,   0,     atcommand_char_ban },
	{ "banish",            60,60,   0,     atcommand_char_ban },
	{ "charban",           60,60,   0,     atcommand_char_ban },
	{ "charbanish",        60,60,   0,     atcommand_char_ban },
	{ "unblock",           60,60,   0,     atcommand_char_unblock },
	{ "charunblock",       60,60,   0,     atcommand_char_unblock },
	{ "unban",             60,60,   0,     atcommand_char_unban },
	{ "unbanish",          60,60,   0,     atcommand_char_unban },
	{ "charunban",         60,60,   0,     atcommand_char_unban },
	{ "charunbanish",      60,60,   0,     atcommand_char_unban },
	{ "mount",             20,20,   0,     atcommand_mount },
	{ "guildspy",          60,60,   0,     atcommand_guildspy },
	{ "partyspy",          60,60,   0,     atcommand_partyspy },
	{ "repairall",         60,60,   0,     atcommand_repairall },
	{ "guildrecall",       60,60,   0,     atcommand_guildrecall },
	{ "partyrecall",       60,60,   0,     atcommand_partyrecall },
	{ "nuke",              60,60,   0,     atcommand_nuke },
	{ "shownpc",           80,80,   0,     atcommand_shownpc },
	{ "hidenpc",           80,80,   0,     atcommand_hidenpc },
	{ "loadnpc",           80,80,   0,     atcommand_loadnpc },
	{ "unloadnpc",         80,80,   0,     atcommand_unloadnpc },
	{ "time",               1,1,    0,     atcommand_servertime },
	{ "date",               1,1,    0,     atcommand_servertime },
	{ "serverdate",         1,1,    0,     atcommand_servertime },
	{ "servertime",         1,1,    0,     atcommand_servertime },
	{ "jail",              60,60,   0,     atcommand_jail },
	{ "unjail",            60,60,   0,     atcommand_unjail },
	{ "discharge",         60,60,   0,     atcommand_unjail },
	{ "jailfor",           60,60,   0,     atcommand_jailfor },
	{ "jailtime",           1,1,    0,     atcommand_jailtime },
	{ "disguise",          20,20,   0,     atcommand_disguise },
	{ "undisguise",        20,20,   0,     atcommand_undisguise },
	{ "email",              1,1,    0,     atcommand_email },
	{ "effect",            40,40,   0,     atcommand_effect },
	{ "follow",            20,20,   0,     atcommand_follow },
	{ "addwarp",           60,60,   0,     atcommand_addwarp },
	{ "skillon",           80,80,   0,     atcommand_skillon },
	{ "skilloff",          80,80,   0,     atcommand_skilloff },
	{ "killer",            60,60,   0,     atcommand_killer },
	{ "npcmove",           80,80,   0,     atcommand_npcmove },
	{ "killable",          40,40,   0,     atcommand_killable },
	{ "dropall",           40,40,   0,     atcommand_dropall },
	{ "storeall",          40,40,   0,     atcommand_storeall },
	{ "skillid",           40,40,   0,     atcommand_skillid },
	{ "blockskill",        40,40,   0,     atcommand_blockskill },
	{ "useskill",          40,40,   0,     atcommand_useskill },
	{ "displayskill",      99,99,   0,     atcommand_displayskill },
	{ "snow",              99,99,   0,     atcommand_snow },
	{ "sakura",            99,99,   0,     atcommand_sakura },
	{ "clouds",            99,99,   0,     atcommand_clouds },
	{ "clouds2",           99,99,   0,     atcommand_clouds2 },
	{ "fog",               99,99,   0,     atcommand_fog },
	{ "fireworks",         99,99,   0,     atcommand_fireworks },
	{ "leaves",            99,99,   0,     atcommand_leaves },
	{ "summon",            60,60,   0,     atcommand_summon },
	{ "adjgmlvl",          99,99,   0,     atcommand_adjgmlvl },
	{ "adjcmdlvl",         99,99,   0,     atcommand_adjcmdlvl },
	{ "trade",             60,60,   0,     atcommand_trade },
	{ "send",              99,99,   0,     atcommand_send },
	{ "setbattleflag",     99,99,   0,     atcommand_setbattleflag },
	{ "unmute",            80,80,   0,     atcommand_unmute },
	{ "clearweather",      99,99,   0,     atcommand_clearweather },
	{ "uptime",             1,1,    0,     atcommand_uptime },
	{ "changesex",         60,60,   0,     atcommand_changesex },
	{ "mute",              80,80,   0,     atcommand_mute },
	{ "refresh",            1,1,    0,     atcommand_refresh },
	{ "autorefresh",        1,1,    0,     atcommand_autorefresh },
	{ "identify",          40,40,   0,     atcommand_identify },
	{ "gmotd",             20,20,   0,     atcommand_gmotd },
	{ "misceffect",        50,50,   0,     atcommand_misceffect },
	{ "mobsearch",         10,10,   0,     atcommand_mobsearch },
	{ "cleanmap",          40,40,   0,     atcommand_cleanmap },
	{ "npctalk",           20,20,   0,     atcommand_npctalk },
	{ "npctalkc",          20,20,   0,     atcommand_npctalk },
	{ "pettalk",           10,10,   0,     atcommand_pettalk },
	{ "users",             40,40,   0,     atcommand_users },
	{ "reset",             40,40,   0,     atcommand_reset },
	{ "skilltree",         40,40,   0,     atcommand_skilltree },
	{ "marry",             40,40,   0,     atcommand_marry },
	{ "divorce",           40,40,   0,     atcommand_divorce },
	{ "sound",             40,40,   0,     atcommand_sound },
	{ "undisguiseall",     99,99,   0,     atcommand_undisguiseall },
	{ "disguiseall",       99,99,   0,     atcommand_disguiseall },
	{ "changelook",        60,60,   0,     atcommand_changelook },
	{ "autoloot",           0,10,   0,     atcommand_autoloot },
	{ "aloot",              0,10,   0,     atcommand_autoloot },
	{ "mobinfo",            1,1,    0,     atcommand_mobinfo },
	{ "monsterinfo",        1,1,    0,     atcommand_mobinfo },
	{ "mi",                 1,1,    0,     atcommand_mobinfo },
	{ "exp",                1,1,    0,     atcommand_exp },
	{ "adopt",             40,40,   0,     atcommand_adopt },
	{ "version",            1,1,    0,     atcommand_version },
	{ "mutearea",          99,99,   0,     atcommand_mutearea },
	{ "stfu",              99,99,   0,     atcommand_mutearea },
	{ "rates",              1,1,    0,     atcommand_rates },
	{ "iteminfo",           1,1,    0,     atcommand_iteminfo },
	{ "ii",                 1,1,    0,     atcommand_iteminfo },
	{ "whodrops",           1,1,    0,     atcommand_whodrops },
	{ "whereis",           10,10,   0,     atcommand_whereis },
	{ "wherelevel",         0,10,   0,     atcommand_wherelevel },
	{ "mapflag",           99,99,   0,     atcommand_mapflag },
	{ "me",                20,20,   0,     atcommand_me },
	{ "monsterignore",     99,99,   0,     atcommand_monsterignore },
	{ "battleignore",      99,99,   0,     atcommand_monsterignore },
	{ "fakename",          20,20,   0,     atcommand_fakename },
	{ "size",              20,20,   0,     atcommand_size },
	{ "sizeall",           20,20,   0,     atcommand_sizeall },
	{ "showexp",           10,10,   0,     atcommand_showexp},
	{ "showzeny",          10,10,   0,     atcommand_showzeny},
	{ "showdelay",          1,1,    0,     atcommand_showdelay},
	{ "autotrade",         10,10,   0,     atcommand_autotrade },
	{ "at",                10,10,   0,     atcommand_autotrade },
	{ "changegm",          10,10,   0,     atcommand_changegm },
	{ "changeleader",      10,10,   0,     atcommand_changeleader },
	{ "partyoption",       10,10,   0,     atcommand_partyoption},
	{ "invite",             1,1,    0,     atcommand_invite },
	{ "duel",               1,1,    0,     atcommand_duel },
	{ "leave",              1,1,    0,     atcommand_leave },
	{ "accept",             1,1,    0,     atcommand_accept },
	{ "reject",             1,1,    0,     atcommand_reject },
	{ "away",               1,1,    0,     atcommand_away },
	{ "aw",                 1,1,    0,     atcommand_away },
	{ "clone",             50,50,   0,     atcommand_clone },
	{ "slaveclone",        50,50,   0,     atcommand_clone },
	{ "evilclone",         50,50,   0,     atcommand_clone },
	{ "tonpc",             40,40,   0,     atcommand_tonpc },
	{ "commands",           1,1,    0,     atcommand_commands },
	{ "noask",              1,1,    0,     atcommand_noask },
	{ "request",           20,20,   0,     atcommand_request },
	{ "hlvl",              60,60,   0,     atcommand_homlevel },
	{ "hlevel",            60,60,   0,     atcommand_homlevel },
	{ "homlvl",            60,60,   0,     atcommand_homlevel },
	{ "homlvup",           60,60,   0,     atcommand_homlevel },
	{ "homlevel",          60,60,   0,     atcommand_homlevel },
	{ "homevolve",         60,60,   0,     atcommand_homevolution },
	{ "homevolution",      60,60,   0,     atcommand_homevolution },
	{ "makehomun",         60,60,   0,     atcommand_makehomun },
	{ "homfriendly",       60,60,   0,     atcommand_homfriendly },
	{ "homhungry",         60,60,   0,     atcommand_homhungry },
	{ "homtalk",           10,10,   0,     atcommand_homtalk },
	{ "hominfo",            1,1,    0,     atcommand_hominfo },
	{ "homstats",           1,1,    0,     atcommand_homstats },
	{ "homshuffle",        60,60,   0,     atcommand_homshuffle },
	{ "showmobs",          10,10,   0,     atcommand_showmobs },
	{ "feelreset",         10,10,   0,     atcommand_feelreset },
	{ "auction",           60,60,   0,     atcommand_auction },
	{ "mail",               1,1,    0,     atcommand_mail },
	{ "noks",               1,1,    0,     atcommand_ksprotection },
	{ "allowks",            6,6,    0,     atcommand_allowks },
	{ "cash",              60,60,   0,     atcommand_cash },
	{ "gcash",              1,1,    0,     atcommand_gcash },
	{ "points",            60,60,   0,     atcommand_cash },
	{ "gpoints",            1,1,    0,     atcommand_gpoints },
	{ "skreset",           60,60,   0,     atcommand_resetskill },
	{ "streset",           60,60,   0,     atcommand_resetstat },
	{ "storagelist",       40,40,   0,     atcommand_itemlist },
	{ "cartlist",          40,40,   0,     atcommand_itemlist },
	{ "itemlist",          40,40,   0,     atcommand_itemlist },
	{ "stats",             40,40,   0,     atcommand_stats },
	{ "delitem",           60,60,   0,     atcommand_delitem },
	{ "charcommands",       1,1,    0,     atcommand_commands },
	{ "disguiseguild",     99,99,   0,     atcommand_disguiseguild },
	{ "ddrop",              0,1,    0,     atcommand_displaydrop },
	{ "dexp",              10,10,   0,     atcommand_showexp},
	{ "showcast",           1,1,    0,     atcommand_showcast},
	{ "showcastdelay",      1,1,    0,     atcommand_showcastdelay},
	{ "invitar",            1,1,    0,     atcommand_invite },
	{ "duelo",              1,1,    0,     atcommand_duel },
	{ "dejar",              1,1,    0,     atcommand_leave },
	{ "aceptar",            1,1,    0,     atcommand_accept },
	{ "declinar",           1,1,    0,     atcommand_reject },
	{ "away",               1,1,    0,     atcommand_away },
	{ "afk",                1,1,    0,     atcommand_away },
	{ "create",             1,1,    0,     atcommand_create },
	{ "join",               1,1,    0,     atcommand_join },
	{ "exit",               1,1,    0,     atcommand_exit },
	{ "list",               1,1,    0,     atcommand_list },
#ifndef TXT_ONLY
	{ "charlist",	          4,4,    0,     atcommand_charlist },
	{ "accountinfo",        4,4,    0,     atcommand_accountinfo },
	{ "memberinfo",         4,4,    0,     atcommand_memberinfo },
	{ "logininfo",          4,4,    0,     atcommand_logininfo },
	{ "ipinfo",             1,4,    0,     atcommand_ipinfo },
	{ "whoip",              1,1,    0,     atcommand_whoip },
	{ "moveaccount",       99,99,   0,     atcommand_moveaccount },
#endif
	{ "dance",              1,1,    0,     atcommand_dance },
	{ "power",             99,99,   0,     atcommand_maspowerr },
	{ "mobdemolition",     99,99,   0,     atcommand_mobdemolition },
	{ "mobevent",           4,4,    0,     atcommand_mobevent },
	{ "exppenalty",        40,40,   0,     atcommand_exppenalty },
	{ "undeadmode",        40,40,   0,     atcommand_residente },
	{ "mapdeadcounter",    40,40,   0,     atcommand_contadormuertes },
	{ "whosell",            1,1,    0,     atcommand_whosell },
	{ "cityheart",         40,40,   0,     atcommand_cityheart },
	{ "pvpmode",            1,1,    0,     atcommand_pvpmode },
	{ "whopk",              1,1,    0,     atcommand_whopk },
	{ "rentstorage",        1,1,    0,     atcommand_rentstorage },
	{ "aura",               1,1,    0,     atcommand_aura },
	{ "flooritem",         60,60,   0,     atcommand_flooritem },
	{ "expinfo",            1,1,    0,     atcommand_expinfo },
	{ "mission",            1,1,    0,     atcommand_mission },
	{ "char2dump",         60,60,   0,     atcommand_char2dump },
	{ "security",           0,0,    0,     atcommand_security },
	{ "font",               1,1,    1,     atcommand_font },
	{ "packetfilter",       0,1,    0,     atcommand_packetfilter },
	{ "netinfo",            1,1,    0,     atcommand_netinfo },
	{ "guildskill",         0,60,   0,     atcommand_guildskill },
	{ "cashshop",           0,60,   0,     atcommand_cashshop },
	{ "rankreset",         60,60,   0,     atcommand_rankreset },
	{ "itemdestroy",       99,99,   0,     atcommand_item_remove4all },
	{ "viewmobinfo",       60,60,   0,     atcommand_viewmobinfo },
	{ "order",              0,60,   0,     atcommand_order },
	{ "leader",             0,60,   0,     atcommand_leader },
	{ "reportafk",          0,60,   0,     atcommand_reportafk },
	{ "bgranked",           0,60,   0,     atcommand_bgranked },
	{ "bgregular",          0,60,   0,     atcommand_bgregular },
	{ "battleinfo",         0,60,   0,     atcommand_battleinfo },
	{ "addfame",           99,99,   0,     atcommand_addfame },
	{ "unboundall",        60,60,   0,     atcommand_unboundall },
};

/*==========================================
 * Command lookup functions
 *------------------------------------------*/
static AtCommandInfo* get_atcommandinfo_byname(const char* name)
{
	int i;
	if( *name == atcommand_symbol || *name == charcommand_symbol ) name++; // for backwards compatibility
	ARR_FIND( 0, ARRAYLENGTH(atcommand_info), i, strcmpi(atcommand_info[i].command, name) == 0 );
	return ( i < ARRAYLENGTH(atcommand_info) ) ? &atcommand_info[i] : NULL;
}

static AtCommandInfo* get_atcommandinfo_byfunc(const AtCommandFunc func)
{
	int i;
	ARR_FIND( 0, ARRAYLENGTH(atcommand_info), i, atcommand_info[i].func == func );
	return ( i < ARRAYLENGTH(atcommand_info) ) ? &atcommand_info[i] : NULL;
}


/*==========================================
 * Retrieve the command's required gm level
 *------------------------------------------*/
int get_atcommand_level(const AtCommandFunc func)
{
	AtCommandInfo* info = get_atcommandinfo_byfunc(func);
	return ( info != NULL ) ? info->level : 100; // 100: command can not be used
}


/// Executes an at-command.
bool is_atcommand(const int fd, struct map_session_data* sd, const char* message, int type)
{
	char charname[NAME_LENGTH], params[100];
	char charname2[NAME_LENGTH], params2[100];
	char command[100];
	char output[CHAT_SIZE_MAX];
	int x, y, z;
	int lv = 0;
	
	//Reconstructed message
	char atcmd_msg[CHAT_SIZE_MAX];
	
	TBL_PC * ssd = NULL; //sd for target
	AtCommandInfo * info;

	nullpo_retr(false, sd);
	
	//Shouldn't happen
	if( !message || !*message )
		return false;
	
	//Block NOCHAT but do not display it as a normal message
	if( sd->sc.data[SC_NOCHAT] && sd->sc.data[SC_NOCHAT]->val1&MANNER_NOCOMMAND )
		return true;
		
	// skip 10/11-langtype's codepage indicator, if detected
	if( message[0] == '|' && strlen(message) >= 4 && (message[3] == atcommand_symbol || message[3] == charcommand_symbol) )
		message += 3;
		
	//Should display as a normal message
	if ( *message != atcommand_symbol && *message != charcommand_symbol )
		return false;
	
	// type value 0 = server invoked: bypass restrictions
	// 1 = player invoked
	if( type )
	{
		//Commands are disabled on maps flagged as 'nocommand'
		if( map[sd->bl.m].nocommand && pc_isGM(sd) < map[sd->bl.m].nocommand )
		{
			clif_displaymessage(fd, msg_txt(143));
			return false;
		}
		
		//Displays as a normal message for Non-GMs
		if( battle_config.atc_gmonly != 0 && pc_isGM(sd) == 0 )
			return false;	
	}

	while (*message == charcommand_symbol)
	{	
		//Checks to see if #command has a name or a name + parameters.
		x = sscanf(message, "%99s \"%23[^\"]\" %99[^\n]", command, charname, params);
		y = sscanf(message, "%99s %23s %99[^\n]", command, charname2, params2);
		
		//z always has the value of the scan that was successful
		z = ( x > 1 ) ? x : y;
		
		if ( (ssd = map_nick2sd(charname)) == NULL  && ( (ssd = map_nick2sd(charname2)) == NULL ) )
		{
			sprintf(output, "%s failed. Player not found.", command);
			clif_displaymessage(fd, output);
			return true;
		}
		
		//#command + name means the sufficient target was used and anything else after
		//can be looked at by the actual command function since most scan to see if the
		//right parameters are used.
		if ( x > 2 ) {
			sprintf(atcmd_msg, "%s %s", command, params);
			break;
		}
		else if ( y > 2 ) {
			sprintf(atcmd_msg, "%s %s", command, params2);
			break;
		}
		//Regardless of what style the #command is used, if it's correct, it will always have
		//this value if there is no parameter. Send it as just the #command
		else if ( z == 2 ) {
			sprintf(atcmd_msg, "%s", command);
			break;
		}
		
		sprintf(output, "Charcommand failed. Usage: #<command> <char name> <params>.");
		clif_displaymessage(fd, output);
		return true;
	}
	
	if (*message == atcommand_symbol) {
		//atcmd_msg is constructed above differently for charcommands
		//it's copied from message if not a charcommand so it can 
		//pass through the rest of the code compatible with both symbols
		sprintf(atcmd_msg, "%s", message);
	}
	
	//Clearing these to be used once more. 
	memset(command, '\0', sizeof(command));
	memset(params, '\0', sizeof(params));
	
	//check to see if any params exist within this command
	if( sscanf(atcmd_msg, "%99s %99[^\n]", command, params) < 2 )
		params[0] = '\0';
	
	//Grab the command information and check for the proper GM level required to use it or if the command exists
	info = get_atcommandinfo_byname(command);
	if( info == NULL || info->func == NULL )
	{
		sprintf(output, msg_txt(153), command); // "%s is Unknown Command."
		clif_displaymessage(fd, output);
		return true;
	}

	if( type )
	{
		if( *atcmd_msg == atcommand_symbol && pc_isGM(sd) < info->level )
		{
			if( info->premium && pc_isPremium(sd) )
				premium_usage = true; // Obviosly, it's Premium
			else
			{
				sprintf(output, msg_txt(153), command); // "%s is Unknown Command."
				clif_displaymessage(fd, output);
				return true;
			}
		}

		if( *atcmd_msg == charcommand_symbol && pc_isGM(sd) < info->level2 )
		{
			sprintf(output, msg_txt(153), command); // "%s is Unknown Command."
			clif_displaymessage(fd, output);
			return true;
		}
	}
	
	//Attempt to use the command
	if( strcmpi("adjgmlvl",command+1) && ssd ) { lv = ssd->gmlevel; ssd->gmlevel = sd->gmlevel; }
	if( (info->func(fd, (*atcmd_msg == atcommand_symbol) ? sd : ssd, command, params) != 0) )
	{
		sprintf(output,msg_txt(154), command); // %s failed.
		clif_displaymessage(fd, output);
	}
	if( strcmpi("adjgmlvl",command+1) && ssd ) ssd->gmlevel = lv;

	if( type && !premium_usage )
	{
		//Log atcommands
		if( log_config.gm && info->level >= log_config.gm && *atcmd_msg == atcommand_symbol )
			log_atcommand(sd, atcmd_msg);
		//Log Charcommands
		if( log_config.gm && info->level2 >= log_config.gm && *atcmd_msg == charcommand_symbol && ssd != NULL )
			log_atcommand(ssd, message);
	}

	premium_usage = false;
	return true;
}


/*==========================================
 *
 *------------------------------------------*/
int atcommand_config_read(const char* cfgName)
{
	char line[1024], w1[1024], w2[1024], w3[1024], w4[1024];
	AtCommandInfo* p;
	FILE* fp;
	
	if( (fp = fopen(cfgName, "r")) == NULL )
	{
		ShowError("AtCommand configuration file not found: %s\n", cfgName);
		return 1;
	}
	
	while( fgets(line, sizeof(line), fp) )
	{
		if( line[0] == '/' && line[1] == '/' )
			continue;

		if( sscanf(line, "%1023[^:]:%1023[^,],%1023[^,],%1023s", w1, w2, w3, w4) != 4 &&
			sscanf(line, "%1023[^:]:%1023[^,],%1023s", w1, w2, w3) != 3 &&
			(sscanf(line, "%1023[^:]:%1023s", w1, w2) != 2 && strcmpi(w1, "import") != 0) &&
			strcmpi(w1, "command_symbol") != 0 &&
			strcmpi(w1, "char_symbol") != 0 )
			continue;

		p = get_atcommandinfo_byname(w1);
		if( p != NULL )
		{
			p->level = atoi(w2);
			p->level = cap_value(p->level, 0, 100);

			if( sscanf(line, "%1023[^:]:%1023[^,],%1023[^,],%1023s", w1, w2, w3, w4) == 4 )
			{ // command:@level,#level,premium
				p->level2 = atoi(w3);
				p->level2 = cap_value(p->level2, 0, 100);
				p->premium = atoi(w4);
				p->premium = cap_value(p->premium, 0, 1);
			}
			else if( sscanf(line, "%1023[^:]:%1023[^,],%1023s", w1, w2, w3) == 3 )
			{ // command:@level,#level
				p->level2 = atoi(w3);
				p->level2 = cap_value(p->level2, 0, 100);
				p->premium = 0;
			}
			else
			{ // command:@level,#level
				ShowWarning("atcommand_conf: setting %s:%d is deprecated! Please see atcommand_athena.conf for the new setting format.\n",w1,atoi(w2));
				ShowWarning("atcommand_conf: defaulting %s charcommand level to 100.\n",w1);
				p->level2 = 100;
			}
		}
		else
		if( strcmpi(w1, "import") == 0 )
			atcommand_config_read(w2);
		else
		if( strcmpi(w1, "command_symbol") == 0 &&
			w2[0] > 31   && // control characters
			w2[0] != '/' && // symbol of standard ragnarok GM commands
			w2[0] != '%' && // symbol of party chat speaking
			w2[0] != '$' && // symbol of guild chat speaking
			w2[0] != '#' ) // remote symbol
			atcommand_symbol = w2[0];
		else 
		if( strcmpi(w1, "char_symbol") == 0 &&
			w2[0] > 31   &&
			w2[0] != '/' &&
			w2[0] != '%' &&
			w2[0] != '$' &&
			w2[0] != '@' )
			charcommand_symbol = w2[0];
		else
			ShowWarning("Unknown setting '%s' in file %s\n", w1, cfgName);
	}
	fclose(fp);

	return 0;
}

void do_init_atcommand()
{
	add_timer_func_list(atshowmobs_timer, "atshowmobs_timer");
	return;
}

void do_final_atcommand()
{
}


// commands that need to go _after_ the commands table

/*==========================================
 * @commands Lists available @ commands to you
 *------------------------------------------*/
int atcommand_commands(const int fd, struct map_session_data* sd, const char* command, const char* message)
{
	char line_buff[CHATBOX_SIZE];
	int i, gm_lvl = pc_isGM(sd), count = 0;
	char* cur = line_buff;

	memset(line_buff,' ',CHATBOX_SIZE);
	line_buff[CHATBOX_SIZE-1] = 0;

	clif_displaymessage(fd, msg_txt(273)); // "Commands available:"

	for( i = 0; i < ARRAYLENGTH(atcommand_info); i++ )
	{
		unsigned int slen;

		if( gm_lvl < atcommand_info[i].level && stristr(command,"commands") )
			continue;
		if( gm_lvl < atcommand_info[i].level2 && stristr(command,"charcommands") )
			continue;

		slen = (unsigned int)strlen(atcommand_info[i].command);

		// flush the text buffer if this command won't fit into it
		if( slen + cur - line_buff >= CHATBOX_SIZE )
		{
			clif_displaymessage(fd,line_buff);
			cur = line_buff;
			memset(line_buff,' ',CHATBOX_SIZE);
			line_buff[CHATBOX_SIZE-1] = 0;
		}

		memcpy(cur,atcommand_info[i].command,slen);
		cur += slen+(10-slen%10);

		count++;
	}
	clif_displaymessage(fd,line_buff);

	sprintf(atcmd_output, msg_txt(274), count); // "%d commands found."
	clif_displaymessage(fd, atcmd_output);

	return 0;
}
