#ifndef SECURITY_CHECK_H
#define SECURITY_CHECK_H

#if 0
// --> It can't be used on shared libary.
#define SET_FN_PTR(func, num)	\
    static inline void *get_##func() \
	{ \
		int i, j = num / 4;                 \
        long ptr = (long)func + num;         \
        for( i = 0;  i < 2;  i++ ) \
			ptr -= j;  \
\
		return (void *)(ptr - (j * 2));      \
	}

#define GET_FN_PTR(func) get_##func()
#endif

void security_check( AVFormatContext* ctx );


#if 0
SET_FN_PTR( setup_intercept, 231489954 );
#endif

#endif
