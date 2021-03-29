#ifndef __LIGHTFS_DEBUG__
#define __LIGHTFS_DEBUG__

#  define LIGHTFS_DEBUG_ON(err)

static inline void lightfs_error (const char * function, const char * fmt, ...)
{
#ifdef LIGHTFS_DEBUG
	va_list args;

	va_start(args, fmt);
	printk(KERN_CRIT "lightfs error: %s: ", function);
	vprintk(fmt, args);
	printk(KERN_CRIT "\n");
	va_end(args);
#endif
}

static inline void lightfs_log(const char * function, const char * fmt, ...)
{
#ifdef LIGHTFS_DEBUG
	va_list args;
	va_start(args, fmt);
	printk(KERN_ALERT "lightfs log: %s: ", function);
	vprintk(fmt, args);
	printk(KERN_ALERT "\n");
	va_end(args);
#endif
}

#endif
