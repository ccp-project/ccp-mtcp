#include <unistd.h>
#include "tcp_cong.h"
#include "tcp_stream.h"
#include "debug.h"

uint32_t GetCWND(uint16_t mss) {
	return 100000;
}
