#ifndef __JSL_LOG_H__
#define __JSL_LOG_H__ 1

enum dbcode {
	JSL_DBG_OFF = 0,
	JSL_DBG_1 = 1, // Critical
	JSL_DBG_2 = 2, // Error
	JSL_DBG_3 = 3, // Info
	JSL_DBG_4 = 4, // Debugging
};

extern int JSL_DEBUG_LEVEL;

#define jsl_log(level,...)                                    \
	do {                                                        \
		if(JSL_DEBUG_LEVEL < abs(level))			      							\
		{;}                                                       \
		else {                                                    \
			{ printf(__VA_ARGS__);}														\
		}                                                         \
	} while(0)

void jsl_set_debug(int level);

#endif // __JSL_LOG_H__
