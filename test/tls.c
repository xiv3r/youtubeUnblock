static const char tls_chlo_message[] = "\001\000\002\000\003\003*{D\360FDTZ\305\231\272\006\240\246oa\365}ut\321\033\354\361}\334\227\342\215\257]\332\000\000\006\023\001\023\002\023\003\001\000\001\321\0009\000_\t\002@g\017\000\005\004\200`\000\000q'\004\200\001\026\210\a\004\200`\000\000\001\004\200\000u0\003\002E\300\006\004\200`\000\000\316E,\310\0160;\306\003g\201k\004\004\200\360\000\000\200\000GR\004\000\000\000\001 \004\200\001\000\000\200\377s\333\f\000\000\000\001\n\212\nJ\000\000\000\001\b\002@d\000\020\000\005\000\003\002h3\000+\000\003\002\003\004\000\n\000\b\000\006\000\035\000\027\000\030\000\033\000\003\002\000\002Di\000\005\000\003\002h3\000\r\000\024\000\022\004\003\b\004\004\001\005\003\b\005\005\001\b\006\006\001\002\001\000-\000\002\001\001\376\r\000\332\000\000\001\000\001|\000 \004\256\340\330}\337lC3\304gv\325}\rT\370O,i^\001\357\323\373?\205@3\023\354{\000\260\247cf\207\3276\312\205G\017\213Y\231\b\301~\225r\v\001X\026\335\254H\231\237\237\263\027b\b\327\0351W\000\177tc\213:^\f\362\340\225_\272\331\351\002\026rds\326\034\345*5!\221\265\206\270\240\375\nw\v\340 \003\340\307\230H\203#\212\371\364\257H\220\230L\230{\243\355\v'\325@\240EZ\306\230a\233;\033|=(\372P\232\216\215\203\374\234\222\375\004\3058l\275+?\f\306\335\342Q\313\"F\377G<2Jqb\033\033,|\302w\337bO\032\276\374\312X\364}\255xq\274\2348\247K\345t\327\345\322M\004\220\376*\344\365\0003\000&\000$\000\035\000 W\356I\271\201\350\263[cn\\H?\376s``\v\230\306?E=2\017u\306\027\nc{c\000\000\000\030\000\026\000\000\023abc.defghijklm.ndev";

static const char tls_bruteforce_message[] = "ahl  qlwer 12oi34j 1l2kjrdosij f982j jfa osdijwoeij rasdjf oiajsqw9erj pqwoijf lasdj foijyoutube.com";

#include "unity.h"
#include "unity_fixture.h"

#include "types.h"
#include <stdio.h>
#include "tls.h"
#include "config.h"
#include "logging.h"

static struct section_config_t sconf = default_section_config;

TEST_GROUP(TLSTest);

TEST_SETUP(TLSTest)
{
}

TEST_TEAR_DOWN(TLSTest)
{
}

TEST(TLSTest, Test_CHLO_message_detect)
{
	struct tls_verdict tlsv;
	int ret;
	ret = analyze_tls_message(&sconf, (const uint8_t *)tls_chlo_message, sizeof(tls_chlo_message) - 1, &tlsv);
	TEST_ASSERT_EQUAL(0, ret);
	TEST_ASSERT_GREATER_OR_EQUAL(19, tlsv.sni_len);
	TEST_ASSERT_EQUAL_STRING_LEN("abc.defghijklm.ndev", tlsv.sni_ptr, 19);
}

TEST(TLSTest, Test_Bruteforce_detects)
{
	struct tls_verdict tlsv;
	struct trie_container trie;
	int ret;
	ret = trie_init(&trie);
	ret = trie_add_string(&trie, (uint8_t *)"youtube.com", 11);
	sconf.sni_domains = trie;

	ret = bruteforce_analyze_sni_str(&sconf, (const uint8_t *)tls_bruteforce_message, sizeof(tls_bruteforce_message) - 1, &tlsv);
	TEST_ASSERT_EQUAL(0, ret);
	TEST_ASSERT_EQUAL(11, tlsv.sni_len);
	TEST_ASSERT_EQUAL_STRING_LEN("youtube.com", tlsv.sni_ptr, 11);
	TEST_ASSERT_EQUAL_PTR(tls_bruteforce_message + 
			sizeof(tls_bruteforce_message) - 12, tlsv.sni_ptr);
	trie_destroy(&trie);
}

TEST_GROUP_RUNNER(TLSTest)
{
	RUN_TEST_CASE(TLSTest, Test_CHLO_message_detect);
	RUN_TEST_CASE(TLSTest, Test_Bruteforce_detects);
}
