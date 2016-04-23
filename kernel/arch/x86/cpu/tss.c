#include <core/system.h>


#define RW_DATA	0x2
#define XR_CODE	0xA

#define BASE  0
#define LIMIT -1

#define DPL0 0
#define DPL3 3

struct tss_gdt_entry
{
	uint32_t limit_lo : 16;	/* Segment Limit 15:00 */
	uint32_t base_lo  : 16;	/* Base Address 15:00 */
	uint32_t base_mid : 8;	/* Base Address 23:16 */
	uint32_t type     : 4;	/* Segment Type */
	uint32_t s        : 1;	/* Descriptor type (0=system, 1=code) */
	uint32_t dpl      : 2;	/* Descriptor Privellage Level */
	uint32_t p        : 1;	/* Segment present */
	uint32_t limit_hi : 4;	/* Segment Limit 19:16 */
	uint32_t avl      : 1;	/* Avilable for use by system software */
	uint32_t l        : 1;	/* Long mode segment (64-bit only) */
	uint32_t db       : 1;	/* Default operation size / upper Bound */
	uint32_t g        : 1;	/* Granularity */
	uint32_t base_hi  : 8;	/* Base Address 31:24 */
} __attribute__((packed, aligned(8)))
