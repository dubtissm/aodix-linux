/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Aodix Linux - run loop services of the platform layer for plugin editors (VST3 IRunLoop), usable without
// including the win32 compatibility header
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#pragma once

namespace adxplat
{
	// file descriptor watches, the callback runs in the gui thread when the fd becomes readable
	typedef void (*fd_callback)(int fd,void* user);
	void			add_fd_watch(int fd,fd_callback cb,void* user);
	void			remove_fd_watch(int fd,void* user);
	void			remove_fd_watches(void* user);

	// periodic timers in the gui thread
	typedef void (*timer_callback)(void* user);
	unsigned long	timer_start(unsigned int interval_ms,timer_callback cb,void* user);
	void			timer_stop(unsigned long id);
}
