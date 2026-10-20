#ifndef _SYS_SYSMACROS_H_
#define _SYS_SYSMACROS_H_

/* device numbers: the major number in the high byte, the minor in the low */
#define major(dev)          ((unsigned) (((dev) >> 8) & 0xff))
#define minor(dev)          ((unsigned) ((dev) & 0xff))
#define makedev(maj, min)   ((dev_t) ((((maj) & 0xff) << 8) | ((min) & 0xff)))

#endif /* !_SYS_SYSMACROS_H_ */
