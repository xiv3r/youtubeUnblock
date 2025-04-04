/*
  youtubeUnblock - https://github.com/Waujito/youtubeUnblock

  Copyright (C) 2024-2025 Vadim Vetrov <vetrovvd@gmail.com>

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
  
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
  
  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#include "types.h"
#include "tls.h"
#include "config.h"
#include "logging.h"
#include "utils.h"

#ifndef KERNEL_SPACE
#include <fcntl.h>
#include <unistd.h>
#endif

int bruteforce_analyze_sni_str(
	const struct section_config_t *section,
	const uint8_t *data, size_t dlen,
	struct tls_verdict *vrd
) {
	size_t offset, offlen;
	int ret;
	*vrd = (struct tls_verdict){0};

	if (dlen <= 1) {
		return 0;
	}

	if (section->all_domains) {
		vrd->target_sni = 1;
		vrd->sni_len = 0;
		vrd->sni_ptr = data + dlen / 2;
		vrd->target_sni_ptr = vrd->sni_ptr;
		vrd->target_sni_len = vrd->sni_len;
		return 0;
	}

	// It is safe for multithreading, so dp mutability is ok
	ret = trie_process_str((struct trie_container *)&section->sni_domains, data, dlen, 0, &offset, &offlen);
	if (ret) {
		vrd->target_sni = 1;
		vrd->sni_len = offlen;
		vrd->sni_ptr = data + offset;
		vrd->target_sni_ptr = vrd->sni_ptr;
		vrd->target_sni_len = vrd->sni_len;
	}
	
	return 0;
}
static int analyze_sni_str(
	const struct section_config_t *section,
	const char *sni_name, int sni_len, 
	struct tls_verdict *vrd
) {
	int ret;
	size_t offset, offlen;

	if (section->all_domains) {
		vrd->target_sni = 1;
		goto check_domain;
	}

	// It is safe for multithreading, so dp mutability is ok
	ret = trie_process_str((struct trie_container *)&section->sni_domains, 
			(const uint8_t *)sni_name, sni_len, TRIE_OPT_MAP_TO_END, &offset, &offlen);
	if (ret) {
		vrd->target_sni = 1;
		vrd->target_sni_ptr = (const uint8_t *)sni_name + offset;
		vrd->target_sni_len = offlen;
	}

check_domain:
	if (vrd->target_sni == 1) {

		// It is safe for multithreading, so dp mutability is ok
		ret = trie_process_str((struct trie_container *)&section->exclude_sni_domains, 
				(const uint8_t *)sni_name, sni_len, TRIE_OPT_MAP_TO_END, &offset, &offlen);
		if (ret) {
			vrd->target_sni = 0;
			lgdebug("Excluded SNI: %.*s", 
				vrd->sni_len, vrd->sni_ptr);
		}
	}

	return 0;
}

int analyze_tls_message(
	const struct section_config_t *section,
	const uint8_t *message_data, 
	size_t message_length,
	struct tls_verdict *tlsv
) {
	*tlsv = (struct tls_verdict){0};
	const uint8_t *handshakeProto = message_data;
	const uint8_t *data_end = message_data + message_length;

	if (handshakeProto + 1 >= data_end) 
		goto invalid;

	uint8_t handshakeType = *handshakeProto;

	if (handshakeType != TLS_HANDSHAKE_TYPE_CLIENT_HELLO)
		goto next;

	const uint8_t *msgPtr = handshakeProto;
	msgPtr += 1; 
	msgPtr += 3 + 2 + 32;

	if (msgPtr + 1 >= data_end) 
		goto invalid;
	uint8_t sessionIdLength = *msgPtr;
	msgPtr++;
	msgPtr += sessionIdLength;

	if (msgPtr + 2 >= data_end)
		goto invalid;
	uint16_t ciphersLength = ntohs(*(uint16_t *)msgPtr);
	msgPtr += 2;
	msgPtr += ciphersLength;

	if (msgPtr + 1 >= data_end)
		goto invalid;
	uint8_t compMethodsLen = *msgPtr;
	msgPtr++;
	msgPtr += compMethodsLen;

	if (msgPtr + 2 >= data_end)
		goto invalid;
	uint16_t extensionsLen = ntohs(*(uint16_t *)msgPtr);
	msgPtr += 2;

	const uint8_t *extensionsPtr = msgPtr;
	const uint8_t *extensions_end = extensionsPtr + extensionsLen;
	if (extensions_end > data_end) extensions_end = data_end;

	while (extensionsPtr < extensions_end) {
		const uint8_t *extensionPtr = extensionsPtr;
		if (extensionPtr + 4 >= extensions_end)
			goto invalid;

		uint16_t extensionType = 
			ntohs(*(uint16_t *)extensionPtr);
		extensionPtr += 2;

		uint16_t extensionLen = 
			ntohs(*(uint16_t *)extensionPtr);
		extensionPtr += 2;


		if (extensionPtr + extensionLen > extensions_end) 
			goto invalid;

		if (extensionType != TLS_EXTENSION_SNI) 
			goto nextExtension;

		const uint8_t *sni_ext_ptr = extensionPtr;

		if (sni_ext_ptr + 2 >= extensions_end)
			goto invalid;
		uint16_t sni_ext_dlen = ntohs(*(uint16_t *)sni_ext_ptr);

		sni_ext_ptr += 2;

		const uint8_t *sni_ext_end = sni_ext_ptr + sni_ext_dlen;
		if (sni_ext_end > extensions_end)
			goto invalid;
		
		if (sni_ext_ptr + 3 >= sni_ext_end)
			goto invalid;
		sni_ext_ptr++;
		uint16_t sni_len = ntohs(*(uint16_t *)sni_ext_ptr);
		sni_ext_ptr += 2;

		if (sni_ext_ptr + sni_len > sni_ext_end)
			goto invalid;

		const char *sni_name = (char *)sni_ext_ptr;

		tlsv->sni_ptr = (const uint8_t *)sni_name;
		tlsv->sni_len = sni_len;
		tlsv->target_sni_ptr = tlsv->sni_ptr;
		tlsv->target_sni_len = tlsv->sni_len;

		analyze_sni_str(section, sni_name, sni_len, tlsv);
		return TLS_MESSAGE_ANALYZE_FOUND;

nextExtension:
		extensionsPtr += 2 + 2 + extensionLen;
	}

next:
	return TLS_MESSAGE_ANALYZE_GOTO_NEXT;
invalid:
	return TLS_MESSAGE_ANALYZE_INVALID;
}

/**
 * Processes tls payload of the tcp request.
 * 
 * data Payload data of TCP.
 * dlen Length of `data`.
 */
struct tls_verdict analyze_tls_data(
	const struct section_config_t *section,
	const uint8_t *data, 
	size_t dlen) 
{
	struct tls_verdict vrd = {0};

	const uint8_t *data_end = data + dlen;
	const uint8_t *message_ptr = data;
	int ret;

	if (section->sni_detection == SNI_DETECTION_BRUTE) {
		bruteforce_analyze_sni_str(section, data, dlen, &vrd);
		goto out;
	}

	while (message_ptr + 5 < data_end) {
		uint8_t tls_content_type = *message_ptr;
		message_ptr++;

		uint8_t tls_vmajor = *message_ptr;
		if (tls_vmajor != 0x03) break;
		message_ptr++;

		// uint8_t tls_vminor = *message_ptr;
		message_ptr++;

		uint16_t message_length = ntohs(*(const uint16_t *)message_ptr);
		message_ptr += 2;


		const uint8_t *tls_message_data = message_ptr;
		// Since real length may be truncated use minimum of two
		size_t tls_message_length = min((int)message_length, (int)(data_end - message_ptr));

		if (tls_content_type != TLS_CONTENT_TYPE_HANDSHAKE) 
			goto nextMessage;

		ret = analyze_tls_message(
			section,
			tls_message_data,
			tls_message_length,
			&vrd
		);

		switch (ret) {
			case TLS_MESSAGE_ANALYZE_GOTO_NEXT:
				goto nextMessage;
			case TLS_MESSAGE_ANALYZE_FOUND: 
			case TLS_MESSAGE_ANALYZE_INVALID: 
			default:
				goto out;
			
		}
		
nextMessage:
		message_ptr += tls_message_length;
	}

out:
	return vrd;
}

int gen_fake_sni(struct fake_type type,
		const void *ipxh, size_t iph_len, 
		const struct tcphdr *tcph, size_t tcph_len,
		uint8_t **ubuf, size_t *ubuflen) {
	size_t data_len = type.fake_len;
	uint8_t *buf = NULL;
	int ret;

	if (type.type == FAKE_PAYLOAD_RANDOM && data_len == 0) {
		data_len = (size_t)randint() % 1200;
	}

	if (!ipxh || !tcph || !ubuf || !ubuflen)
		return -EINVAL;

	int ipxv = netproto_version(ipxh, iph_len);

	if (ipxv == IP6VERSION) {
		iph_len = sizeof(struct ip6_hdr);
	}

	size_t dlen = iph_len + tcph_len + data_len;
	size_t buffer_len = dlen + 50;
	buf = malloc(buffer_len);
	if (buf == NULL) {
		return -ENOMEM;
	}

	if (ipxv == IP4VERSION) {
		const struct iphdr *iph = ipxh;

		memcpy(buf, iph, iph_len);
		struct iphdr *niph = (struct iphdr *)buf;

		niph->protocol = IPPROTO_TCP;
	} else if (ipxv == IP6VERSION) {
		const struct ip6_hdr *iph = ipxh;

		memcpy(buf, iph, iph_len);
		struct ip6_hdr *niph = (struct ip6_hdr *)buf;

		niph->ip6_nxt = IPPROTO_TCP;
	} else {
		ret = -EINVAL;
		goto error;
	}

	memcpy(buf + iph_len, tcph, tcph_len);
	uint8_t *bfdptr = buf + iph_len + tcph_len;

	switch (type.type) {
		case FAKE_PAYLOAD_DATA:
			memcpy(bfdptr, type.fake_data, data_len);
			break;
		default: // FAKE_PAYLOAD_RANDOM
#ifdef KERNEL_SPACE
			get_random_bytes(bfdptr, data_len);
#else /* KERNEL_SPACE */
#if _NO_GETRANDOM
		{
			int ret = open("/dev/urandom", O_RDONLY);
			if (ret < 0) {
				lgerror(ret, "Unable to open /dev/urandom");
				return ret;
			}
			
			read(ret, bfdptr, data_len);
			close(ret);
		}
#else /* _NO_GETRANDOM */
			getrandom(bfdptr, data_len, 0);
#endif /* _NO_GETRANDOM */
#endif /* KERNEL_SPACE */
	}

	if (ipxv == IP4VERSION) {
		struct iphdr *niph = (struct iphdr *)buf;
		niph->tot_len = htons(dlen);
	} else if (ipxv == IP6VERSION) {
		struct ip6_hdr *niph = (struct ip6_hdr *)buf;
		niph->ip6_plen = htons(dlen - iph_len);
	}

	ret = fail_packet(type.strategy, buf, &dlen, buffer_len);
	if (ret < 0) {
		lgerror(ret, "fail_packet");
		goto error;
	}


	*ubuflen = dlen;
	*ubuf = buf;
	
	return 0;
error:
	free(buf);
	return ret;
}

