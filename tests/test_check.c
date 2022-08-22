///////////////////////////////////////////////////////////////////////////////
//
/// \file       test_check.c
/// \brief      Tests integrity checks
//
//  Authors:    Lasse Collin
//              Jia Tan
//
//  This file has been put into the public domain.
//  You can do whatever you want with this file.
//
///////////////////////////////////////////////////////////////////////////////

#include "tests.h"


// These must be specified as numbers so that the test works on EBCDIC
// systems too.
// static const uint8_t test_string[9] = "123456789";
// static const uint8_t test_unaligned[12] = "xxx123456789";
static const uint8_t test_string[9] = { 49, 50, 51, 52, 53, 54, 55, 56, 57 };
static const uint8_t test_unaligned[12]
		= { 120, 120, 120, 49, 50, 51, 52, 53, 54, 55, 56, 57 };

// 2 MB is more than enough for the tests. Actually a tiny value would
// work because we don't actually decompress the files, we only test
// decoding of the Stream Header fields.
#define TEST_CHECK_MEMLIMIT (2U << 20)

static size_t no_check_size;
static uint8_t *no_check_xz_data;

static size_t unsupported_check_size;
static uint8_t *unsupported_check_xz_data;

#ifdef HAVE_CHECK_CRC32
static size_t crc32_size;
static uint8_t *crc32_xz_data;
#endif

#ifdef HAVE_CHECK_CRC64
static size_t crc64_size;
static uint8_t *crc64_xz_data;
#endif

#ifdef HAVE_CHECK_SHA256
static size_t sha256_size;
static uint8_t *sha256_xz_data;
#endif


static void
test_lzma_crc32(void)
{
	// CRC32 is always enabled.
	assert_true(lzma_check_is_supported(LZMA_CHECK_CRC32));

	const uint32_t test_vector = 0xCBF43926;

	// Test 1
	assert_uint_eq(lzma_crc32(test_string, sizeof(test_string), 0),
			test_vector);

	// Test 2
	assert_uint_eq(lzma_crc32(test_unaligned + 3, sizeof(test_string), 0),
			test_vector);

	// Test 3
	uint32_t crc = 0;
	for (size_t i = 0; i < sizeof(test_string); ++i)
		crc = lzma_crc32(test_string + i, 1, crc);
	assert_uint_eq(crc, test_vector);
}


static void
test_lzma_crc64(void)
{
	// CRC64 can be disabled.
	if (!lzma_check_is_supported(LZMA_CHECK_CRC64))
		assert_skip("CRC64 support is disabled");

	// If CRC64 is disabled then lzma_crc64() will be missing.
	// Using an ifdef here avoids a linker error.
#ifdef HAVE_CHECK_CRC64
	const uint64_t test_vector = 0x995DC9BBDF1939FA;

	// Test 1
	assert_uint_eq(lzma_crc64(test_string, sizeof(test_string), 0),
			test_vector);

	// Test 2
	assert_uint_eq(lzma_crc64(test_unaligned + 3, sizeof(test_string), 0),
			test_vector);

	// Test 3
	uint64_t crc = 0;
	for (size_t i = 0; i < sizeof(test_string); ++i)
		crc = lzma_crc64(test_string + i, 1, crc);
	assert_uint_eq(crc, test_vector);
#endif
}


static void
test_lzma_supported_checks(void)
{
	static const int expected_check_ids[] = {
		LZMA_CHECK_NONE,
#ifdef HAVE_CHECK_CRC32
		LZMA_CHECK_CRC32,
#endif
#ifdef HAVE_CHECK_CRC64
		LZMA_CHECK_CRC64,
#endif
#ifdef HAVE_CHECK_SHA256
		LZMA_CHECK_SHA256,
#endif
	};

	for (int i = 0; i <= LZMA_CHECK_ID_MAX + 1; i++) {
		bool matched = false;
		for (unsigned int j = 0; j < ARRAY_SIZE(expected_check_ids);
				j++) {
			if (expected_check_ids[j] == i) {
				matched = true;
				break;
			}
		}

		if (matched)
			assert_true(lzma_check_is_supported(i));
		else
			assert_false(lzma_check_is_supported(i));
	}
}


static void
test_lzma_check_size(void)
{
	// Expected check sizes taken from src/liblzma/api/lzma/check.h
	static const uint32_t expected_check_sizes[] = {
			0, 4, 4, 4, 8, 8, 8, 16, 16, 16,
			32, 32, 32, 64, 64, 64
	};

	for (unsigned int i = 0; i < ARRAY_SIZE(expected_check_sizes); i++)
		assert_uint_eq(expected_check_sizes[i], lzma_check_size(i));

	assert_uint_eq(lzma_check_size(LZMA_CHECK_ID_MAX + 1), UINT32_MAX);
}


// Test the single threaded decoder for lzma_get_check
static void
test_lzma_get_check_st(void)
{
	const uint32_t flags = LZMA_TELL_ANY_CHECK |
			LZMA_TELL_UNSUPPORTED_CHECK |
			LZMA_TELL_NO_CHECK;

	uint8_t outbuf[128];
	lzma_stream strm = LZMA_STREAM_INIT;

	// Test a file with no integrity check:
	assert_lzma_ret(lzma_stream_decoder(&strm, TEST_CHECK_MEMLIMIT,
			flags), LZMA_OK);
	strm.next_in = no_check_xz_data;
	strm.avail_in = no_check_size;
	strm.next_out = outbuf;
	strm.avail_out = sizeof(outbuf);

	assert_lzma_ret(lzma_code(&strm, LZMA_RUN), LZMA_NO_CHECK);
	assert_lzma_check(lzma_get_check(&strm), LZMA_CHECK_NONE);
	assert_lzma_ret(lzma_code(&strm, LZMA_RUN), LZMA_STREAM_END);

	// Test a file with an unsupported integrity check type:
	assert_lzma_ret(lzma_stream_decoder(&strm, TEST_CHECK_MEMLIMIT,
			flags), LZMA_OK);
	strm.next_in = unsupported_check_xz_data;
	strm.avail_in = unsupported_check_size;
	strm.next_out = outbuf;
	strm.avail_out = sizeof(outbuf);

	assert_lzma_ret(lzma_code(&strm, LZMA_RUN), LZMA_UNSUPPORTED_CHECK);
	assert_lzma_ret(lzma_code(&strm, LZMA_RUN), LZMA_STREAM_END);

	// Test a file with CRC32 as the integrity check:
#ifdef HAVE_CHECK_CRC32
	assert_lzma_ret(lzma_stream_decoder(&strm, TEST_CHECK_MEMLIMIT,
			flags), LZMA_OK);
	strm.next_in = crc32_xz_data;
	strm.avail_in = crc32_size;
	strm.next_out = outbuf;
	strm.avail_out = sizeof(outbuf);

	assert_lzma_ret(lzma_code(&strm, LZMA_RUN), LZMA_GET_CHECK);
	assert_lzma_check(lzma_get_check(&strm), LZMA_CHECK_CRC32);
	assert_lzma_ret(lzma_code(&strm, LZMA_RUN), LZMA_STREAM_END);
#endif

	// Test a file with CRC64 as the integrity check:
#ifdef HAVE_CHECK_CRC64
	assert_lzma_ret(lzma_stream_decoder(&strm, TEST_CHECK_MEMLIMIT,
			flags), LZMA_OK);
	strm.next_in = crc64_xz_data;
	strm.avail_in = crc64_size;
	strm.next_out = outbuf;
	strm.avail_out = sizeof(outbuf);

	assert_lzma_ret(lzma_code(&strm, LZMA_RUN), LZMA_GET_CHECK);
	assert_lzma_check(lzma_get_check(&strm), LZMA_CHECK_CRC64);
	assert_lzma_ret(lzma_code(&strm, LZMA_RUN), LZMA_STREAM_END);
#endif

	// Test a file with SHA-256 as the integrity check:
#ifdef HAVE_CHECK_SHA256
	assert_lzma_ret(lzma_stream_decoder(&strm, TEST_CHECK_MEMLIMIT,
			flags), LZMA_OK);
	strm.next_in = sha256_xz_data;
	strm.avail_in = sha256_size;
	strm.next_out = outbuf;
	strm.avail_out = sizeof(outbuf);

	assert_lzma_ret(lzma_code(&strm, LZMA_RUN), LZMA_GET_CHECK);
	assert_lzma_check(lzma_get_check(&strm), LZMA_CHECK_SHA256);
	assert_lzma_ret(lzma_code(&strm, LZMA_RUN), LZMA_STREAM_END);
#endif

	lzma_end(&strm);
}


static void
test_lzma_get_check_mt(void)
{
	const uint32_t flags = LZMA_TELL_ANY_CHECK |
			LZMA_TELL_UNSUPPORTED_CHECK |
			LZMA_TELL_NO_CHECK;

	const lzma_mt options = {
		.flags = flags,
		.threads = 2,
		.timeout = 0,
		.memlimit_threading = TEST_CHECK_MEMLIMIT,
		.memlimit_stop = TEST_CHECK_MEMLIMIT
	};

	uint8_t outbuf[128];
	lzma_stream strm = LZMA_STREAM_INIT;

	// Test a file with no integrity check:
	assert_lzma_ret(lzma_stream_decoder_mt(&strm, &options), LZMA_OK);
	strm.next_in = no_check_xz_data;
	strm.avail_in = no_check_size;
	strm.next_out = outbuf;
	strm.avail_out = sizeof(outbuf);

	assert_lzma_ret(lzma_code(&strm, LZMA_RUN), LZMA_NO_CHECK);
	assert_lzma_check(lzma_get_check(&strm), LZMA_CHECK_NONE);
	assert_lzma_ret(lzma_code(&strm, LZMA_RUN), LZMA_STREAM_END);

	// Test a file with an unsupported integrity check type:
	assert_lzma_ret(lzma_stream_decoder_mt(&strm, &options), LZMA_OK);
	strm.next_in = unsupported_check_xz_data;
	strm.avail_in = unsupported_check_size;
	strm.next_out = outbuf;
	strm.avail_out = sizeof(outbuf);

	assert_lzma_ret(lzma_code(&strm, LZMA_RUN), LZMA_UNSUPPORTED_CHECK);
	assert_lzma_ret(lzma_code(&strm, LZMA_RUN), LZMA_STREAM_END);

	// Test a file with CRC32 as the integrity check:
#ifdef HAVE_CHECK_CRC32
	assert_lzma_ret(lzma_stream_decoder_mt(&strm, &options), LZMA_OK);
	strm.next_in = crc32_xz_data;
	strm.avail_in = crc32_size;
	strm.next_out = outbuf;
	strm.avail_out = sizeof(outbuf);

	assert_lzma_ret(lzma_code(&strm, LZMA_RUN), LZMA_GET_CHECK);
	assert_lzma_check(lzma_get_check(&strm), LZMA_CHECK_CRC32);
	assert_lzma_ret(lzma_code(&strm, LZMA_RUN), LZMA_STREAM_END);
#endif

	// Test a file with CRC64 as the integrity check:
#ifdef HAVE_CHECK_CRC64
	assert_lzma_ret(lzma_stream_decoder_mt(&strm, &options), LZMA_OK);
	strm.next_in = crc64_xz_data;
	strm.avail_in = crc64_size;
	strm.next_out = outbuf;
	strm.avail_out = sizeof(outbuf);

	assert_lzma_ret(lzma_code(&strm, LZMA_RUN), LZMA_GET_CHECK);
	assert_lzma_check(lzma_get_check(&strm), LZMA_CHECK_CRC64);
	assert_lzma_ret(lzma_code(&strm, LZMA_RUN), LZMA_STREAM_END);
#endif

	// Test a file with SHA-256 as the integrity check:
#ifdef HAVE_CHECK_SHA256
	assert_lzma_ret(lzma_stream_decoder_mt(&strm,&options), LZMA_OK);
	strm.next_in = sha256_xz_data;
	strm.avail_in = sha256_size;
	strm.next_out = outbuf;
	strm.avail_out = sizeof(outbuf);

	assert_lzma_ret(lzma_code(&strm, LZMA_RUN), LZMA_GET_CHECK);
	assert_lzma_check(lzma_get_check(&strm), LZMA_CHECK_SHA256);
	assert_lzma_ret(lzma_code(&strm, LZMA_RUN), LZMA_STREAM_END);
#endif

	lzma_end(&strm);
}


extern int
main(int argc, char **argv)
{
	tuktest_start(argc, argv);

	no_check_xz_data = tuktest_file_from_srcdir(
			"files/good-1-check-none.xz", &no_check_size);

	unsupported_check_xz_data = tuktest_file_from_srcdir(
			"files/unsupported-check.xz",
			&unsupported_check_size);

#ifdef HAVE_CHECK_CRC32
	crc32_xz_data = tuktest_file_from_srcdir(
			"files/good-1-check-crc32.xz", &crc32_size);
#endif

#ifdef HAVE_CHECK_CRC64
	crc64_xz_data = tuktest_file_from_srcdir(
			"files/good-1-check-crc64.xz", &crc64_size);
#endif

#ifdef HAVE_CHECK_SHA256
	sha256_xz_data = tuktest_file_from_srcdir(
			"files/good-1-check-sha256.xz", &sha256_size);
#endif

	tuktest_run(test_lzma_crc32);
	tuktest_run(test_lzma_crc64);
	tuktest_run(test_lzma_supported_checks);
	tuktest_run(test_lzma_check_size);
	tuktest_run(test_lzma_get_check_st);
	tuktest_run(test_lzma_get_check_mt);

	return tuktest_end();
}
