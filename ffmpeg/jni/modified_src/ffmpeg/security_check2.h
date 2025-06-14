#ifndef SECURITY_CHECK2_H
#define SECURITY_CHECK2_H

#define SET_FN_PTR(func, num)	\
    inline void *get_##func() \
	{ \
		int i, j = num / 4;                 \
        long ptr = (long)func + num;         \
        for( i = 0;  i < 2;  i++ ) \
			ptr -= j;  \
\
		return (void *)(ptr - (j * 2));      \
	}

#define GET_FN_PTR(func) get_##func()

#define STATUS_NOT_TESTED 	0
#define STATUS_SUCCESS 		1
#define STATUS_FAILURE 		-1

__attribute__((visibility("hidden")))
void security_check( int64_t* combined );

extern int security_status;

SET_FN_PTR( security_check, 0x0187C500 );

void setup_intercept( AVCodecContext* ctx );

#endif
