#ifndef __TCP_CONG_H_
#define __TCP_CONG_H_

#include <unistd.h>
#include <stdint.h>

uint32_t GetCWND(uint16_t mss);

#endif

