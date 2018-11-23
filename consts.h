#ifdef _WIN32
    #define SLH "\\"
#else
    #define SLH "/"
#endif

#if defined __UINT32_MAX__ or UINT32_MAX
    #include <inttypes.h>
#else
	typedef unsigned char uint8;
	typedef unsigned short uint16;
	typedef unsigned long uint32;
	typedef unsigned long long uint64;

	typedef unsigned int uint;
#endif

#ifdef _WIN32
    #define WINVER 0x0500
#endif // _WIN32
