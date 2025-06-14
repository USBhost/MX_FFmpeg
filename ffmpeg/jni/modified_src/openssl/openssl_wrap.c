#include <dlfcn.h>
#include <stdio.h>
#include <stddef.h>

struct SSL;
struct SSL_CTX;
struct SSL_METHOD;
struct X509_STORE_CTX;
struct DH;
struct BIGNUM;
struct BN_CTX;
struct BIO;
struct BIO_METHOD;

#if __WORDSIZE == 32
	#define BN_ULONG	unsigned int
#else /* if __WORDSIZE == 64 */
	#define BN_ULONG	unsigned long long
#endif

#define indexOf(f) Idx_ ## f

#define load(from,f) \
	functions[indexOf(f)] = dlsym(from, #f); \
	if( !functions[indexOf(f)] ) \
		perror("Can't find symbol '" #f "'" );

#define load_ssl(f) 	load(libssl,f)
#define load_crypto(f)	load(libcrypto,f)

/* Variadic parameters are calling parameter types */
#define functionOf(return_type,f,...) ((return_type(*)(__VA_ARGS__))functions[indexOf(f)])

/* Variadic parameters are passing parameter values. */
#define callSafe(function_ptr,...) (function_ptr ? function_ptr(__VA_ARGS__) : 0);


enum {
	indexOf(SSL_library_init),
	indexOf(SSL_load_error_strings),
	indexOf(SSL_shutdown),
	indexOf(SSL_new),
	indexOf(SSL_free),
	indexOf(SSL_set_fd),
	indexOf(SSL_accept),
	indexOf(SSL_connect),
	indexOf(SSL_read),
	indexOf(SSL_write),
	indexOf(SSL_ctrl),
	indexOf(SSL_get_error),
	indexOf(SSL_set_bio),
	indexOf(TLSv1_client_method),
	indexOf(TLSv1_server_method),
	indexOf(SSL_CTX_free),
	indexOf(SSL_CTX_new),
	indexOf(SSL_CTX_load_verify_locations),
	indexOf(SSL_CTX_use_certificate_chain_file),
	indexOf(SSL_CTX_use_PrivateKey_file),
	indexOf(SSL_CTX_set_verify),
	indexOf(CRYPTO_get_locking_callback),
	indexOf(CRYPTO_num_locks),
	indexOf(CRYPTO_set_locking_callback),
	indexOf(DH_new),
	indexOf(DH_free),
	indexOf(DH_size),
	indexOf(DH_generate_key),
	indexOf(DH_compute_key),
	indexOf(BN_new),
	indexOf(BN_hex2bn),
	indexOf(BN_bn2bin),
	indexOf(BN_bin2bn),
	indexOf(BN_set_word),
	indexOf(BN_cmp),
	indexOf(BN_copy),
	indexOf(BN_sub_word),
	indexOf(BN_free),
	indexOf(BN_CTX_new),
	indexOf(BN_CTX_free),
	indexOf(BN_mod_exp),
	indexOf(BN_value_one),
	indexOf(BN_num_bits),
	indexOf(BIO_new),
	indexOf(BIO_clear_flags),
	indexOf(ERR_get_error),
	indexOf(ERR_error_string),
	NUM_FUNCTIONS
};

enum {
	false = 0,
	true = 1
};

static int initialized = false;

/** Boolean value if system library is boringssl. */
static int isBoring;

static void* libcrypto;
static void* libssl;
static void *functions[NUM_FUNCTIONS];


int SSL_library_init()
{
	if( initialized == false )
	{
#ifdef DEBUG
		fprintf(stderr, "SSL_library_init() called through openssl_wrap.c");
#endif

		initialized = true;

		libcrypto = dlopen( "libcrypto.so", RTLD_NOW );
		libssl = dlopen( "libssl.so", RTLD_NOW );

		load_ssl(SSL_library_init);
		load_ssl(SSL_load_error_strings);
		load_ssl(SSL_shutdown);
		load_ssl(SSL_new);
		load_ssl(SSL_free);
		load_ssl(SSL_set_fd);
		load_ssl(SSL_accept);
		load_ssl(SSL_connect);
		load_ssl(SSL_read);
		load_ssl(SSL_write);
		load_ssl(SSL_ctrl);
		load_ssl(SSL_get_error);
		load_ssl(SSL_set_bio);
		load_ssl(TLSv1_client_method);
		load_ssl(TLSv1_server_method);
		load_ssl(SSL_CTX_free);
		load_ssl(SSL_CTX_new);
		load_ssl(SSL_CTX_load_verify_locations);
		load_ssl(SSL_CTX_use_certificate_chain_file);
		load_ssl(SSL_CTX_use_PrivateKey_file);
		load_ssl(SSL_CTX_set_verify);
		load_crypto(CRYPTO_get_locking_callback);
		load_crypto(CRYPTO_num_locks);
		load_crypto(CRYPTO_set_locking_callback);
		load_crypto(DH_new);
		load_crypto(DH_free);
		load_crypto(DH_size);
		load_crypto(DH_generate_key);
		load_crypto(DH_compute_key);
		load_crypto(BN_new);
		load_crypto(BN_hex2bn);
		load_crypto(BN_bn2bin);
		load_crypto(BN_bin2bn);
		load_crypto(BN_set_word);
		load_crypto(BN_cmp);
		load_crypto(BN_copy);
		load_crypto(BN_sub_word);
		load_crypto(BN_free);
		load_crypto(BN_CTX_new);
		load_crypto(BN_CTX_free);
		load_crypto(BN_mod_exp);
		load_crypto(BN_value_one);
		load_crypto(BN_num_bits);
		load_crypto(BIO_new);
		load_crypto(BIO_clear_flags);
		load_crypto(ERR_get_error);
		load_crypto(ERR_error_string);

		isBoring = (functions[indexOf(CRYPTO_get_locking_callback)] == 0);
	}
	
	return callSafe( functionOf(int,SSL_library_init) );
}

void SSL_load_error_strings()
{
	callSafe(
			functionOf(void,SSL_load_error_strings) );
}

int SSL_shutdown(struct SSL *s)
{
	return callSafe(
			functionOf(int, SSL_shutdown, struct SSL*),
			s );
}

struct SSL *SSL_new(struct SSL_CTX *ctx)
{
	return callSafe(
			functionOf(struct SSL*, SSL_new, struct SSL_CTX *),
			ctx);
}

void SSL_free(struct SSL *ssl)
{
	callSafe(
			functionOf(void, SSL_free, struct SSL*),
			ssl);
}

int	SSL_set_fd(struct SSL *s, int fd)
{
	return callSafe(
			functionOf(int, SSL_set_fd, struct SSL *, int),
			s, fd)
}

int SSL_accept(struct SSL *ssl)
{
	return callSafe(
			functionOf(int, SSL_accept, struct SSL *),
			ssl);
}

int SSL_connect(struct SSL *ssl)
{
	return callSafe(
			functionOf(int, SSL_connect, struct SSL *),
			ssl);
}

int SSL_read(struct SSL *ssl,void *buf,int num)
{
	return callSafe(
			functionOf(int, SSL_read, struct SSL *,void *,int),
			ssl, buf, num);
}

int SSL_write(struct SSL *ssl,const void *buf,int num)
{
	return callSafe(
			functionOf(int, SSL_write, struct SSL *,const void *,int),
			ssl, buf, num);
}

/* 
	이 함수는 최근버전 ffmpeg에서는 사용하지 않는 것으로 보인다. Android N Preview 5의 libssl.so에는 누락되어있다. 
	@see https://developer.android.com/preview/behavior-changes.html#ndk
*/
long SSL_ctrl(struct SSL *ssl,int cmd, long larg, void *parg)
{
	return callSafe(
			functionOf(long, SSL_ctrl, struct SSL *,int , long , void *),
			ssl, cmd, larg, parg);
}

int	SSL_get_error(const struct SSL *s,int ret_code)
{
	return callSafe(
			functionOf(int, SSL_get_error, const struct SSL *,int ),
			s, ret_code);
}

void SSL_set_bio(struct SSL *s, struct BIO *rbio, struct BIO *wbio)
{
	callSafe( functionOf(void, SSL_set_bio, struct SSL *, struct BIO *, struct BIO *),
			s, rbio, wbio );
}

const struct SSL_METHOD *TLSv1_client_method()
{
	return callSafe(
			functionOf(const struct SSL_METHOD *, TLSv1_client_method));
}

const struct SSL_METHOD *TLSv1_server_method()
{
	return callSafe(
			functionOf(const struct SSL_METHOD *, TLSv1_server_method));
}

void SSL_CTX_free(struct SSL_CTX *ctx)
{
	callSafe( functionOf(void, SSL_CTX_free, struct SSL_CTX *), ctx);
}

struct SSL_CTX *SSL_CTX_new(const struct SSL_METHOD *meth)
{
	return callSafe(
			functionOf(struct SSL_CTX *, SSL_CTX_new, const struct SSL_METHOD *),
			meth);
}

int SSL_CTX_load_verify_locations(struct SSL_CTX *ctx, const char *CAfile, const char *CApath)
{
	return callSafe(
			functionOf(int, SSL_CTX_load_verify_locations, struct SSL_CTX *, const char *, const char *),
			ctx, CAfile, CApath);
}

int	SSL_CTX_use_certificate_chain_file(struct SSL_CTX *ctx, const char *file)
{
	return callSafe(
			functionOf(int, SSL_CTX_use_certificate_chain_file, struct SSL_CTX *, const char *),
			ctx, file);
}

int	SSL_CTX_use_PrivateKey_file(struct SSL_CTX *ctx, const char *file, int type)
{
	return callSafe(
			functionOf(int, SSL_CTX_use_PrivateKey_file, struct SSL_CTX *, const char *, int ),
			ctx, file, type);
}

void SSL_CTX_set_verify(struct SSL_CTX *ctx,int mode, int (*callback)(int, struct X509_STORE_CTX *))
{
	callSafe(
			functionOf(void, SSL_CTX_set_verify, struct SSL_CTX *,int , int (*)(int, struct X509_STORE_CTX *)),
			ctx, mode, callback);
}

static void* locking_callback;

void (*CRYPTO_get_locking_callback())(int,int,const char *, int)
{
	if( isBoring )
		return locking_callback;
	else
	{
		return callSafe(
				functionOf(void*, CRYPTO_get_locking_callback));
	}
}

void CRYPTO_set_locking_callback(void (*func)(int mode,int type, const char *file,int line))
{
	callSafe(
			functionOf(void, CRYPTO_set_locking_callback, void (*)(int ,int , const char *,int )),
			func);

	locking_callback = func;
}

int CRYPTO_num_locks()
{
	return callSafe(
			functionOf(int, CRYPTO_num_locks));
}

struct DH *DH_new()
{
	return callSafe(
			functionOf(struct DH *, DH_new));
}

void DH_free(struct DH *dh)
{
	callSafe(
			functionOf(void, DH_free, struct DH *),
			dh);
}

int	DH_size(const struct DH *dh)
{
	return callSafe(
			functionOf(int, DH_size, const struct DH *),
			dh);
}

int	DH_generate_key(struct DH *dh)
{
	return callSafe(
			functionOf(int, DH_generate_key, struct DH *),
			dh);
}

int	DH_compute_key(unsigned char *key,const struct BIGNUM *pub_key,struct DH *dh)
{
	return callSafe(
			functionOf(int, DH_compute_key, unsigned char *,const struct BIGNUM *,struct DH *),
			key, pub_key, dh);
}

struct BIGNUM *BN_new()
{
	return callSafe(
			functionOf(struct BIGNUM *, BN_new));
}

int BN_hex2bn(struct BIGNUM **a, const char *str)
{
	return callSafe(
			functionOf(int , BN_hex2bn, struct BIGNUM **, const char *),
			a, str);
}

int	BN_bn2bin(const struct BIGNUM *a, unsigned char *to)
{
	if( isBoring )
	{
		return (int)callSafe(
				functionOf(size_t, BN_bn2bin, const struct BIGNUM *, unsigned char *),
				a, to);
	}
	else
	{
		return callSafe(
				functionOf(int , BN_bn2bin, const struct BIGNUM *, unsigned char *),
				a, to);
	}
}

struct BIGNUM *BN_bin2bn(const unsigned char *s,int len,struct BIGNUM *ret)
{
	if( isBoring )
	{
		return callSafe(
				functionOf(struct BIGNUM *, BN_bin2bn, const unsigned char *, size_t, struct BIGNUM *),
				s, len, ret);
	}
	else
	{
		return callSafe(
				functionOf(struct BIGNUM *, BN_bin2bn, const unsigned char *,int ,struct BIGNUM *),
				s, len, ret);
	}
}

int	BN_set_word(struct BIGNUM *a, BN_ULONG w)
{
	return callSafe(
			functionOf(int, BN_set_word, struct BIGNUM *, BN_ULONG ),
			a, w);
}

int	BN_cmp(const struct BIGNUM *a, const struct BIGNUM *b)
{
	return callSafe(
			functionOf(int, BN_cmp, const struct BIGNUM *, const struct BIGNUM *),
			a, b);
}

struct BIGNUM *BN_copy(struct BIGNUM *a, const struct BIGNUM *b)
{
	return callSafe(
			functionOf(struct BIGNUM *, BN_copy, struct BIGNUM *, const struct BIGNUM *),
			a, b);
}

int	BN_sub_word(struct BIGNUM *a, BN_ULONG w)
{
	return callSafe(
			functionOf(int, BN_sub_word, struct BIGNUM *, BN_ULONG ),
			a, w);
}

void BN_free(struct BIGNUM *a)
{
	callSafe(
			functionOf(void, BN_free, struct BIGNUM *),
			a);
}

struct BN_CTX *BN_CTX_new()
{
	return callSafe(
			functionOf(struct BN_CTX *, BN_CTX_new));
}

void BN_CTX_free(struct BN_CTX *c)
{
	callSafe(
			functionOf(void, BN_CTX_free, struct BN_CTX *),
			c);
}

int	BN_mod_exp(struct BIGNUM *r, const struct BIGNUM *a, const struct BIGNUM *p, const struct BIGNUM *m, struct BN_CTX *ctx)
{
	return callSafe(
			functionOf(int, BN_mod_exp, struct BIGNUM *, const struct BIGNUM *, const struct BIGNUM *, const struct BIGNUM *, struct BN_CTX *),
			r, a, p, m, ctx);
}

const struct BIGNUM *BN_value_one()
{
	return callSafe(
			functionOf(const struct BIGNUM *, BN_value_one));
}

int	BN_num_bits(const struct BIGNUM *a)
{
	return callSafe(
			functionOf(int, BN_num_bits, const struct BIGNUM *),
			a);
}

struct BIO *BIO_new(struct BIO_METHOD *type)
{
	return callSafe(
			functionOf(struct BIO*, BIO_new, struct BIO_METHOD *),
			type);
}

void BIO_clear_flags(struct BIO *b, int flags)
{
	callSafe(
			functionOf(void, BIO_clear_flags, struct BIO *, int),
			b, flags);
}

unsigned long ERR_get_error()
{
	return callSafe(
			functionOf(unsigned long, ERR_get_error));
}

char *ERR_error_string(unsigned long e,char *buf)
{
	return callSafe(
			functionOf(char *, ERR_error_string, unsigned long ,char *),
			e, buf);
}
