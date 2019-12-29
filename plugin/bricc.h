#define LOG(...) \
	do { \
		char buffer[256]; \
		snprintf(buffer, sizeof(buffer), ##__VA_ARGS__); \
		logg(buffer, strlen(buffer), "ux0:data/bricc.log", 2); \
} while (0)
	
#define LOG_START(...) \
	do { \
		char buffer[256]; \
		snprintf(buffer, sizeof(buffer), ##__VA_ARGS__); \
		logg(buffer, strlen(buffer), "ux0:data/bricc.log", 1); \
} while (0)

static int logg(void *buffer, int length, const char* logloc, int create)
{
	int fd;
	if (create == 0) {
		fd = ksceIoOpen(logloc, SCE_O_WRONLY | SCE_O_APPEND, 6);
	} else if (create == 1) {
		fd = ksceIoOpen(logloc, SCE_O_WRONLY | SCE_O_TRUNC | SCE_O_CREAT, 6);
	} else if (create == 2) {
		fd = ksceIoOpen(logloc, SCE_O_WRONLY | SCE_O_APPEND | SCE_O_CREAT, 6);
	}
	if (fd < 0)
		return 0;

	ksceIoWrite(fd, buffer, length);
	ksceIoClose(fd);
	return 1;
}