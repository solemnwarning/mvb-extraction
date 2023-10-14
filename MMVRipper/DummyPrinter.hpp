#ifndef MMVRIPPER_DUMMYPRINTER_HPP
#define MMVRIPPER_DUMMYPRINTER_HPP

#include <map>
#include <queue>
#include <vector>

#include <winsock2.h>
#include <windows.h>

class DummyPrinter
{
private:
	volatile bool running;

	int server_socket;

	std::map<int, std::vector<unsigned char> > pending_jobs;
	typedef std::map<int, std::vector<unsigned char> >::iterator pj_iterator;

	std::queue< std::vector<unsigned char> > completed_jobs;

	HANDLE print_server_thread;
	static DWORD WINAPI print_server_main(LPVOID context);

	CRITICAL_SECTION lock;

	HANDLE print_job_completed_event;

public:
	DummyPrinter(unsigned short port);
	~DummyPrinter();

	bool wait_for_print(unsigned int seconds);
	std::vector<unsigned char> get_next_print();
};

#endif /* !MMVRIPPER_DUMMYPRINTER_HPP */
