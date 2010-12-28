/*
 * IRCGW - Internet Relay Chat Gateway, src/gw_webirc.c
 * Copyright (C) 2009 Evilnet Development
 *
 * This file is part of IRCGW
 *
 * IRCGW is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * IRCGW is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with IRCGW.  If not, see <http://www.gnu.org/licenses/>.
 *
 * $Id$
 */
#include "gw_webirc.h"

char* expandaddr6(struct gwin6_addr *a) {
	static char ret[IPEXPMAXLEN];

	snprintf((char *)&ret, IPEXPMAXLEN, "%08x%08x%08x%08x", htonl(a->addr.addr32[0]),
		htonl(a->addr.addr32[1]), htonl(a->addr.addr32[2]),
		htonl(a->addr.addr32[3]));

	return ret;
}

char* expandaddr6colon(struct gwin6_addr *a) {
	static char ret[IPEXPMAXLEN];

	snprintf((char *)&ret, IPEXPMAXLEN, "%04x:%04x:%04x:%04x:%04x:%04x:%04x:%04x",
		htons(a->addr.addr16[0]), htons(a->addr.addr16[1]),
		htons(a->addr.addr16[2]), htons(a->addr.addr16[3]),
		htons(a->addr.addr16[4]), htons(a->addr.addr16[5]),
		htons(a->addr.addr16[6]), htons(a->addr.addr16[7]));

	return (char *)&ret;
}

char* get_rdns6(struct gwin6_addr ipaddr) {
	struct hostent *her;
	struct hostent *hef;
	int i = 0;
	int valid = 0;

	her = gethostbyaddr((const void *)&ipaddr, sizeof ipaddr, AF_INET6);
	
	if (her) {
		hef = gethostbyname2(her->h_name, AF_INET6);
		if (hef) {
			while ((hef->h_addr_list[i] != NULL) && !valid) {
				if (addrcmp((struct gwin6_addr *)&ipaddr, (struct gwin6_addr *)hef->h_addr_list[i], AF_INET6))
					valid = 1;
				i++;
			}
		}
	}
	
	if (valid && her)
		return her->h_name;
	else
		return "";
}

char* get_rdns(struct gwin_addr ipaddr) {
	struct hostent *her;
	struct hostent *hef;
	int i = 0;
	int valid = 0;
	
	her = gethostbyaddr((const void *)&ipaddr, sizeof ipaddr, AF_INET);
	
	if (her) {
		hef = gethostbyname2(her->h_name, AF_INET);
		if (hef) {
			while ((hef->h_addr_list[i] != NULL) && !valid) {
				if (addrcmp((struct gwin6_addr *)&ipaddr, (struct gwin6_addr *)hef->h_addr_list[i], AF_INET))
					valid = 1;
				i++;
			}
		}
	}
	
	if (valid && her)
		return her->h_name;
	else
		return "";
}

char* getwebircmsg(struct Client *cli) {
	static char msg[IRCMSGMAXLEN];
	char host[HOSTMAXLEN], hostpart[HOSTMAXLEN];
	char ip[IPADDRMAXLEN], ip6[IPADDRMAXLEN];
	int af = 0, rdnsdone = 0, i = 0, j = 31;
	char temp = 0;
	unsigned char hash1[16], hash2[16], hash3[16];
	MD5_CTX ctx1, ctx2, ctx3;

	assert(cli != NULL);
	assert(cli->listener != NULL);
	assert(cli->lsock != NULL);

	ip6[0] = 0;
	ip[0] = 0;
	af = cli->lsock->af;

	/* Make sure we actually need to run through this process */
	if (!cli->listener->wircpass || !LstIsWebIRC(cli->listener))
		return NULL;

	if (IsIP6(cli->lsock) && !(IsIP6to4(cli->lsock) && !LstIsWebIRCv6(cli->listener))) {
		/* Ipv6 client */

		/* Get presentation format IPv6 IP */
		inet_ntop(cli->lsock->af, &cli->lsock->addr6, (char *)&ip6, IPADDRMAXLEN);

		/* Get rDNS for IP, will decide what to use in case of no rDNS next */
		if (!LstIsNoRDNS(cli->listener))
			snprintf((char *)&hostpart, HOSTMAXLEN, "%s", get_rdns6(cli->lsock->addr6));
		if (hostpart[0] != 0)
			rdnsdone = 1;

		if (LstIsWebIRCv6(cli->listener)) {
			/* IPv6 WEBIRC supported */
			strncpy((char *)&ip, (char *)&ip6, IPADDRMAXLEN);

			/* Use presentation format IP as host name due to failed rDNS */
			if (hostpart[0] == 0)
				strncpy((char *)&hostpart, (char *)&ip, HOSTMAXLEN);
		} else {
			/* IPv6 WEBIRC NOT supported */

			/* Generate hash 1 (first 64 bits of IPv6 IP) */
			MD5_Init(&ctx1);
			for (i=0; i<8; i++) {
				MD5_Update(&ctx1, (unsigned const char*)&cli->lsock->addr6.addr.addr8[i], 1);
			}
			MD5_Final(hash1, &ctx1);

			/* Generate hash 2 (next 32 bits of IPv6 IP) */
			MD5_Init(&ctx2);
			for (i=8; i<12; i++) {
				MD5_Update(&ctx2, (unsigned const char*)&cli->lsock->addr6.addr.addr8[i], 1);
			}
			MD5_Final(hash2, &ctx2);

			/* Generate hash 3 (last 32 bits of IPv6 IP) */
			MD5_Init(&ctx3);
			for (i=12; i<16; i++) {
				MD5_Update(&ctx3, (unsigned const char*)&cli->lsock->addr6.addr.addr8[i], 1);
			}
			MD5_Final(hash3, &ctx3);
			
			/* Produce pseudo IPv4 IP address using bytes 3, 7 and 11 of hashes 1 2 and 3 respectively */
			snprintf((char *)&ip, IPADDRMAXLEN, "0.%d.%d.%d", hash1[3], hash2[7], hash3[11]);
			
			/* If rDNS failed decide which form of literal IPv6 IP to use */
			if (hostpart[0] == 0) {
				if (LstIsLiteralIPv6(cli->listener))
					snprintf((char *)&hostpart, HOSTMAXLEN, "%s", expandaddr6colon(&cli->lsock->addr6));
				else {
					snprintf((char *)&hostpart, HOSTMAXLEN, "%s", expandaddr6(&cli->lsock->addr6));
					for (i=0; i<16; i++) {
						temp = hostpart[i];
						hostpart[i] = hostpart[j];
						hostpart[j--] = temp;
					}
				}
			}
		}
	} else {
		/* IPv4 client or IPv6 using 6to4 (2002::/16) */

		/* Prepare for 6to4 IP to allow conversion */
		if (IsIP6to4(cli->lsock)) {
			af = AF_INET;
			cli->lsock->addr.addr.addr16[0] = cli->lsock->addr6.addr.addr16[1];
			cli->lsock->addr.addr.addr16[1] = cli->lsock->addr6.addr.addr16[2];
		}

		/* Get presentation format IPv4 IP */
		inet_ntop(af, &cli->lsock->addr, (char *)&ip, IPADDRMAXLEN);

		/* Get rDNS for IP, if fails use presentation format IP as host */
		if (!LstIsNoRDNS(cli->listener))
			snprintf((char *)&hostpart, HOSTMAXLEN, "%s", get_rdns(cli->lsock->addr));
		if (hostpart[0] == 0)
			strncpy((char *)&hostpart, (char *)&ip, HOSTMAXLEN);
		else
			rdnsdone = 1;
	}
	
	/* Finalize host string appending suffix if required and rDNS was successful */
	if (cli->listener->wircsuff && !LstIsNoSuffix(cli->listener) && !(rdnsdone && LstIsRDNSNoSuffix(cli->listener)))
		snprintf((char *)&host, HOSTMAXLEN, "%s.%s", hostpart, cli->listener->wircsuff);
	else
		strncpy((char *)&host, (char *)&hostpart, HOSTMAXLEN);

	/* Format and produce final message */
	if (ip6[0] != 0)
		snprintf((char *)&msg, IRCMSGMAXLEN, MSG_WEBIRC_EXT, cli->listener->wircpass, host, ip, ip6);
	else
		snprintf((char *)&msg, IRCMSGMAXLEN, MSG_WEBIRC, cli->listener->wircpass, host, ip);

	return (char *)&msg;
}

char* getwebircextramsg(struct Client *cli, char* type, char* data) {
	static char msg[IRCMSGMAXLEN];

	assert(cli != NULL);
	assert(cli->listener != NULL);

	if (!cli->listener->wircpass || !LstIsWebIRC(cli->listener))
		return NULL;

	if (!type || !data)
		return NULL;

	snprintf((char *)&msg, IRCMSGMAXLEN, MSG_WEBIRCEXTRA, cli->listener->wircpass, type, data);

	return (char *)&msg;
}

